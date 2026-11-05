"""Build a Win64 game package for KawaiiPhysics performance captures."""

from __future__ import annotations

import argparse
import copy
import datetime
import json
import pathlib
import re
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


def build_command(source: pathlib.Path, engine: pathlib.Path, out: pathlib.Path,
                  suite: dict, config: str = 'Development') -> list[str]:
    arguments = build_arguments(suite, config)
    return [str(engine / 'Engine/Build/BatchFiles/RunUAT.bat'), 'BuildCookRun',
            f'-project={source / "KawaiiPhysicsSample.uproject"}', *arguments[:8],
            f'-archivedirectory={out}', arguments[8],
            '-cookdir=' + '+'.join(str(path) for path in cook_directories(suite, source)),
            *arguments[9:]]


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


def read_toolchain(source: pathlib.Path, config: str) -> str | None:
    """ゲームターゲットの MSVC 行を返す。読めなければ None にする。"""
    # UBT はビルドする処理が無いと toolchain を表示せず、UAT のログには Editor の行も混ざる。
    # ゲームターゲットの Makefile には常に残るので、そこだけを読む。
    makefile = source / 'Intermediate/Build/Win64/x64' / PROJECT_NAME / config / 'Makefile.bin'
    try:
        match = MAKEFILE_TOOLCHAIN.search(makefile.read_bytes().decode('utf-8', errors='replace'))
    except OSError:
        return None
    return match.group() if match and run_perf.toolchain_version(match.group()) else None


def create_package(source: pathlib.Path, engine: pathlib.Path, suite_path: pathlib.Path,
                   out: pathlib.Path, timeout: int = 3600,
                   config: str = 'Development') -> pathlib.Path:
    source, engine, suite_path, out = (path.resolve() for path in
                                      (source, engine, suite_path, out))
    if out.exists():
        raise ValueError(f'出力先が既にあります: {out}')
    out.mkdir(parents=True, exist_ok=False)
    suite = json.loads(suite_path.read_text(encoding='utf-8'))
    expected_build = CONFIG_BUILDS[config]
    # UAT がソースツリーにファイルを生成する前に記録する。
    commit = git(source, 'rev-parse', 'HEAD').decode('ascii').strip()
    dirty = bool(git(source, 'status', '--porcelain', '--', *TARGET_PATHS).strip())
    version = json.loads((engine / 'Engine/Build/Build.version').read_text(encoding='utf-8'))
    signatures = run_perf.source_signatures(suite, source)
    suite_sha256 = run_perf.digest_bytes([suite_path.read_bytes()])
    log_path = out / 'uat.log'
    with log_path.open('wb') as log:
        try:
            returncode = run_uat(build_command(source, engine, out, suite, config), source, log,
                                 timeout)
        except subprocess.TimeoutExpired:
            raise ValueError(f'UAT がタイムアウトしました（{timeout} 秒）: {log_path}') from None
    if returncode != 0:
        raise ValueError(f'UAT が失敗しました（終了コード {returncode}）: {log_path}')
    executable = find_executable(out, config)
    smoke(suite, executable, out, timeout, expected_build)
    package = {'schema': run_perf.PACKAGE_SCHEMA,
               'created': datetime.datetime.now().astimezone().isoformat(timespec='seconds'),
               'source': str(source), 'commit': commit, 'dirty': dirty, 'config': config,
               'expected_build': expected_build, 'executable': executable.relative_to(out).as_posix(),
               'engine_version': {key: version[key] for key in ENGINE_FIELDS},
               'suite_sha256': suite_sha256, **signatures,
               'toolchain': read_toolchain(source, config), 'uat_log': 'uat.log'}
    (out / 'package.json').write_text(json.dumps(package, indent=2, sort_keys=True,
                                                ensure_ascii=False) + '\n', encoding='utf-8')
    return out


def main(argv: list[str] | None = None) -> int:
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=pathlib.Path, required=True)
    parser.add_argument('--source', type=pathlib.Path, default=run_perf.ROOT)
    parser.add_argument('--suite', type=pathlib.Path, default=run_perf.DEFAULT_SUITE)
    parser.add_argument('--engine', type=pathlib.Path, default=run_cells.DEFAULT_ENGINE)
    parser.add_argument('--timeout', type=int, default=3600)
    parser.add_argument('--config', choices=tuple(CONFIG_BUILDS), default='Development',
                        help='Test はソースビルドのエンジンが必要です')
    args = parser.parse_args(argv)
    if args.timeout < 1:
        parser.error('--timeout は正の整数にしてください')
    try:
        target = create_package(args.source, args.engine, args.suite, args.out,
                                args.timeout, args.config)
    except (OSError, subprocess.SubprocessError, ValueError, KeyError, TypeError, IndexError) as error:
        print(f'ERROR: パッケージ作成に失敗しました: {error}\n出力先: {args.out.resolve()}',
              file=sys.stderr)
        return 1
    print(target)
    return 0


if __name__ == '__main__':
    sys.exit(main())
