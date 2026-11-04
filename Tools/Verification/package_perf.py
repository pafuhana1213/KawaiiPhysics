"""Build and cache a Win64 game package for KawaiiPhysics performance captures."""

from __future__ import annotations

import argparse
import copy
import datetime
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys

import run_cells
import run_perf

TARGET_PATHS = ('Plugins', 'Source', 'Config', 'Content', 'KawaiiPhysicsSample.uproject')
ENGINE_FIELDS = ('MajorVersion', 'MinorVersion', 'PatchVersion', 'Changelist', 'BranchName')
MAKEFILE_TOOLCHAIN = re.compile(r'Using Visual Studio [^\x00-\x1f]*?toolchain')
PROJECT_NAME = 'KawaiiPhysicsSample'
# The launcher engine ships Development and Shipping game libraries only, and Shipping has no CSV
# profiler, so Development is the default. Test needs a source-built engine.
CONFIG_BUILDS = {'Development': 'game_development', 'Test': 'game_test'}


def git(source: pathlib.Path, *arguments: str) -> bytes:
    return subprocess.run(['git', *arguments], cwd=source, capture_output=True, check=True).stdout


def build_arguments(suite: dict, config: str = 'Development') -> list[str]:
    if config not in CONFIG_BUILDS:
        raise ValueError(f'対応していない構成です: {config}')
    return ['-noP4', '-platform=Win64', f'-clientconfig={config}', '-build', '-cook', '-stage',
            '-pak', '-archive', f"-map={suite['level']}", '-unattended', '-utf8output']


def cook_directories(suite: dict, source: pathlib.Path) -> list[pathlib.Path]:
    source = source.resolve()
    directories = {run_perf.package_file(fixture[key], source).parent
                   for fixture in suite['fixtures'].values() for key in ('mesh', 'anim_class')}
    directories.add(source / 'Plugins/KawaiiPhysicsDev/Content/Levels')
    return sorted(directories, key=str)


def build_command(source: pathlib.Path, engine: pathlib.Path, work: pathlib.Path,
                  suite: dict, config: str = 'Development') -> list[str]:
    arguments = build_arguments(suite, config)
    return [str(engine / 'Engine/Build/BatchFiles/RunUAT.bat'), 'BuildCookRun',
            f'-project={source / "KawaiiPhysicsSample.uproject"}', *arguments[:8],
            f'-archivedirectory={work / "archive"}', arguments[8],
            '-cookdir=' + '+'.join(str(path) for path in cook_directories(suite, source)),
            *arguments[9:]]


def make_identity(source: pathlib.Path, engine: pathlib.Path, suite_path: pathlib.Path,
                  suite: dict, variant: str = '', config: str = 'Development') -> dict:
    commit = git(source, 'rev-parse', 'HEAD').decode('ascii').strip()
    difference = git(source, 'diff', 'HEAD', '--binary', '--', *TARGET_PATHS)
    untracked = git(source, 'ls-files', '--others', '--exclude-standard', '-z', '--', *TARGET_PATHS)
    names = sorted(name for name in untracked.split(b'\0') if name)
    parts = [difference]
    for name in names:
        content = (source / os.fsdecode(name)).read_bytes()
        parts.extend((len(name).to_bytes(8, 'big'), name,
                      len(content).to_bytes(8, 'big'), content))
    dirty = bool(difference or names)
    version = json.loads((engine / 'Engine/Build/Build.version').read_text(encoding='utf-8'))
    return {'commit': commit, 'dirty': dirty,
            'dirty_sha256': run_perf.digest_bytes(parts) if dirty else None,
            'engine_version': {key: version[key] for key in ENGINE_FIELDS},
            'build_arguments': build_arguments(suite, config),
            'suite_sha256': run_perf.digest_bytes([suite_path.read_bytes()]), 'variant': variant}


def package_id(identity: dict) -> str:
    return identity['commit'][:12] + '-' + run_perf.condition_key(identity)[:8]


def destination(out_root: pathlib.Path, identity: dict, allow_dirty: bool = False) -> pathlib.Path:
    if identity['dirty'] and not allow_dirty:
        raise ValueError('ビルド対象に未コミットの変更があります。--allow-dirty を指定してください')
    return out_root / ('dirty' if identity['dirty'] else '') / package_id(identity)


def find_executable(archive: pathlib.Path, config: str = 'Development') -> pathlib.Path:
    # Development の実行ファイルには構成の接尾辞が付かない。
    name = PROJECT_NAME + ('' if config == 'Development' else f'-Win64-{config}') + '.exe'
    matches = list(archive.glob(f'**/Binaries/Win64/{name}'))
    if len(matches) != 1:
        raise ValueError(f'{config} の実行ファイルが 1 件ではありません: {len(matches)} 件')
    return matches[0]


def smoke_manifest(suite: dict) -> dict:
    small = copy.deepcopy(suite)
    small['groups'] = [{'fixture': name, 'count': 1} for name in suite['fixtures']]
    return {'frames': 30, 'record': False, 'settings': suite['settings'],
            'cells': run_perf.expand_suite(small)}


def validate_smoke(outcome: dict, header: dict, fixture_count: int,
                   expected_build: str = 'game_development') -> None:
    if outcome['exit_code'] != 0 or not outcome['recorded'] or outcome['severe_log']:
        raise ValueError('スモーク確認でランナー失敗または重大ログがあります')
    if header.get('ok') is not True or header.get('build') != expected_build:
        raise ValueError('スモーク確認の ok または build が不正です')
    nodes = header.get('kp_nodes')
    if type(nodes) is not int or nodes < fixture_count:
        raise ValueError('スモーク確認の kp_nodes が fixture 数を満たしません')


def smoke(suite: dict, executable: pathlib.Path, work: pathlib.Path, timeout: int,
          expected_build: str = 'game_development') -> None:
    manifest, output = work / 'smoke.json', work / 'smoke.jsonl'
    manifest.write_text(json.dumps(smoke_manifest(suite), indent=2), encoding='utf-8')
    outcome = run_cells.run(manifest, output, executable=executable,
                            level=suite['level'], fps=suite['fps'], timeout=timeout)
    header = (json.loads(output.read_text(encoding='utf-8').splitlines()[0])
              if outcome['recorded'] else {})
    validate_smoke(outcome, header, len(suite['fixtures']), expected_build)


def run_uat(command: list[str], cwd: pathlib.Path, log, timeout: int) -> int:
    process = subprocess.Popen(command, cwd=cwd, stdout=log, stderr=subprocess.STDOUT)
    try:
        return process.wait(timeout=timeout)
    except (subprocess.TimeoutExpired, KeyboardInterrupt):
        # UAT は子プロセスを多数起動するので、木ごと止めないとビルドが残る。
        subprocess.run(['taskkill', '/T', '/F', '/PID', str(process.pid)], capture_output=True)
        process.wait()
        raise


def read_toolchain(source: pathlib.Path, config: str) -> str:
    """Return the MSVC toolchain line of the game target, or raise before anything is published."""
    # UBT はビルドする処理が無いと toolchain を表示せず、UAT のログには Editor の行も混ざる。
    # ゲームターゲットの Makefile には常に残るので、そこだけを読む。
    makefile = source / 'Intermediate/Build/Win64/x64' / PROJECT_NAME / config / 'Makefile.bin'
    match = (MAKEFILE_TOOLCHAIN.search(makefile.read_bytes().decode('utf-8', errors='replace'))
             if makefile.is_file() else None)
    if match is None or run_perf.toolchain_version(match.group()) is None:
        raise ValueError(f'ゲームターゲットの Makefile から MSVC の版を読めません'
                         f'（MSVC 以外には対応していません）: {makefile}')
    return match.group()


def create_package(source: pathlib.Path, engine: pathlib.Path, suite_path: pathlib.Path,
                   out_root: pathlib.Path, variant: str = '', allow_dirty: bool = False,
                   rebuild: bool = False, timeout: int = 3600,
                   config: str = 'Development') -> pathlib.Path:
    source, engine, suite_path, out_root = (path.resolve() for path in
                                           (source, engine, suite_path, out_root))
    suite = json.loads(suite_path.read_text(encoding='utf-8'))
    expected_build = CONFIG_BUILDS[config]
    identity = make_identity(source, engine, suite_path, suite, variant, config)
    target = destination(out_root, identity, allow_dirty)
    if (target / 'package.json').is_file() and not rebuild:
        cached = json.loads((target / 'package.json').read_text(encoding='utf-8'))
        if run_perf.toolchain_version(cached.get('toolchain')) is None:
            raise ValueError(f'キャッシュのパッケージに toolchain の版がありません。--rebuild で作り直してください: {target}')
        return target
    # Capture source metadata before UAT can generate files in the source tree.
    signatures = run_perf.source_signatures(suite, source)
    plugin_tree = git(source, 'rev-parse', 'HEAD:Plugins/KawaiiPhysics').decode('ascii').strip()
    stamp = datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
    work = out_root / '_work' / f'{package_id(identity)}-{stamp}'
    work.mkdir(parents=True, exist_ok=False)
    log_path = work / 'uat.log'
    with log_path.open('wb') as log:
        try:
            returncode = run_uat(build_command(source, engine, work, suite, config), source, log,
                                 timeout)
        except subprocess.TimeoutExpired:
            raise ValueError(f'UAT がタイムアウトしました（{timeout} 秒）: {log_path}') from None
    if returncode != 0:
        raise ValueError(f'UAT が失敗しました（終了コード {returncode}）: {log_path}')
    archive = work / 'archive'
    executable = find_executable(archive, config)
    smoke(suite, executable, work, timeout, expected_build)
    # ビルド中にソースが変わっていたら、取得済みのメタデータと食い違うので公開しない。
    if (make_identity(source, engine, suite_path, suite, variant, config) != identity
            or run_perf.source_signatures(suite, source) != signatures
            or git(source, 'rev-parse', 'HEAD:Plugins/KawaiiPhysics').decode('ascii').strip()
            != plugin_tree):
        raise ValueError('ビルド中にソースが変わりました。パッケージを作り直してください')
    toolchain = read_toolchain(source, config)
    package = {'schema': run_perf.PACKAGE_SCHEMA, 'id': package_id(identity), 'identity': identity,
               'created': datetime.datetime.now().astimezone().isoformat(timespec='seconds'),
               'source': str(source), 'executable': executable.relative_to(archive).as_posix(),
               'expected_build': expected_build,
               **signatures, 'plugin_tree': plugin_tree,
               'toolchain': toolchain,
               'ddc': {name: {'present': name in os.environ, 'value': os.environ.get(name)}
                       for name in ('UE-SharedDataCachePath', 'UE-LocalDataCachePath')},
               'uat_log': 'uat.log'}
    shutil.copy2(log_path, archive / 'uat.log')
    (archive / 'package.json').write_text(json.dumps(package, indent=2, sort_keys=True,
                                                    ensure_ascii=False) + '\n', encoding='utf-8')
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.exists():
        if not rebuild:
            raise ValueError(f'確定先が既にあります: {target}')
        target.rename(work / 'previous-package')
    try:
        archive.rename(target)
    except OSError:
        previous = work / 'previous-package'
        if previous.exists() and not target.exists():
            previous.rename(target)
        raise
    return target


def main(argv: list[str] | None = None) -> int:
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out-root', type=pathlib.Path, default=os.environ.get('KPV_PACKAGE_ROOT'))
    parser.add_argument('--source', type=pathlib.Path, default=run_perf.ROOT)
    parser.add_argument('--suite', type=pathlib.Path, default=run_perf.DEFAULT_SUITE)
    parser.add_argument('--variant', default='')
    parser.add_argument('--allow-dirty', action='store_true')
    parser.add_argument('--rebuild', action='store_true')
    parser.add_argument('--engine', type=pathlib.Path, default=run_cells.DEFAULT_ENGINE)
    parser.add_argument('--timeout', type=int, default=3600)
    parser.add_argument('--config', choices=tuple(CONFIG_BUILDS), default='Development',
                        help='Test はソースビルドのエンジンが必要です')
    args = parser.parse_args(argv)
    if args.out_root is None:
        parser.error('--out-root または KPV_PACKAGE_ROOT を指定してください')
    if args.timeout < 1:
        parser.error('--timeout は正の整数にしてください')
    try:
        target = create_package(args.source, args.engine, args.suite, args.out_root,
                                args.variant, args.allow_dirty, args.rebuild, args.timeout,
                                args.config)
    except (OSError, subprocess.SubprocessError, ValueError, KeyError, TypeError, IndexError) as error:
        print(f'ERROR: パッケージ作成に失敗しました: {error}', file=sys.stderr)
        return 1
    print(target)
    return 0


if __name__ == '__main__':
    sys.exit(main())
