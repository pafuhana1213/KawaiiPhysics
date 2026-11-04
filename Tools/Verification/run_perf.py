"""Run alternating KawaiiPhysics CSV captures and write a comparable result JSON."""

from __future__ import annotations

import argparse
import copy
import csv
import datetime
import hashlib
import json
import math
import os
import pathlib
import re
import statistics
import subprocess
import sys
import winreg

import run_cells
from measure_noise import on_ac_power

ROOT = pathlib.Path(__file__).resolve().parents[2]
DEFAULT_SUITE = pathlib.Path(__file__).resolve().parent / 'suites' / 'performance.json'
METRIC_STATS = {
    'kp_eval_ms': 'Eval', 'kp_editor_sync_ms': 'EditorSync',
    'kp_subsystem_tick_ms': 'SubsystemTick', 'kp_simulate_ms': 'SimulateModifyBones',
    'kp_integrate_ms': 'Integrate', 'kp_collision_ms': 'Collision',
    'kp_bone_constraint_ms': 'BoneConstraint', 'kp_limits_and_length_ms': 'LimitsAndLength',
    'kp_update_limits_ms': 'UpdateLimits', 'kp_update_physics_settings_ms': 'UpdatePhysicsSettings',
    'kp_pose_update_ms': 'PoseUpdate', 'kp_apply_result_ms': 'ApplySimulateResult',
}


def expand_suite(suite: dict, positive_control: bool = False) -> list[dict]:
    """Place all fixtures on one stable, nonoverlapping grid."""
    grid = suite['grid']
    cells = []
    for group in suite['groups']:
        fixture = suite['fixtures'][group['fixture']]
        for local in range(group['count']):
            index = len(cells)
            offset = fixture.get('location_offset', [0, 0, 0])
            location = [grid['origin'][axis] + (index % grid['columns'] if axis == 0 else
                        index // grid['columns'] if axis == 1 else 0) * grid['spacing'][axis] + offset[axis]
                        for axis in range(3)]
            cell = {key: copy.deepcopy(value) for key, value in fixture.items()
                    if key not in ('expect', 'location_offset')}
            cell.update(id=f"{group['fixture']}_{local:03d}", location=location,
                        motion={'linear_amplitude': [60, 30, 0],
                                'linear_period': 1.3 + index % 7 * 0.1,
                                'yaw_amplitude_degrees': 30,
                                'yaw_period': 1.7 + index % 5 * 0.1})
            if positive_control:
                cell['overrides'] = {**cell.get('overrides', {}), 'TargetFramerate': '120'}
            cells.append(cell)
    if len({tuple(cell['location']) for cell in cells}) != len(cells):
        raise ValueError('Suite cell positions overlap.')
    return cells


def expected_amounts(suite: dict) -> dict[str, float]:
    expected: dict[str, float] = {}
    for group in suite['groups']:
        for name, minimum in suite['fixtures'][group['fixture']].get('expect', {}).items():
            expected[name] = expected.get(name, 0.0) + group['count'] * minimum
    return expected


def make_manifest(suite: dict, cells: list[dict], csv_path: pathlib.Path, state: str,
                  audit: bool = False) -> dict:
    settings = suite['audit'] if audit else suite
    manifest = {'frames': settings['frames'], 'record': False,
                'settings': suite['settings'], 'cells': cells,
                'csv_capture': {**settings['capture'],
                                'directory': str(csv_path.parent), 'filename': csv_path.name}}
    if state == 'off':
        manifest['disable_kawaii_physics'] = True
    return manifest


def make_cvars(cvars: list[str], state: str) -> list[str]:
    if state not in ('on', 'off'):
        raise ValueError('計測の state が on/off ではありません')
    return list(cvars)


def read_csv_capture(path: pathlib.Path) -> tuple[list[dict[str, float]], dict[str, str]]:
    """Read the data rows against the trailing header and return them with the metadata row."""
    csv.field_size_limit(min(sys.maxsize, 2**31 - 1))
    with path.open(newline='', encoding='utf-8-sig') as source:
        lines = list(csv.reader(source))
    # Stats that first appear mid-capture add columns at the end, so only the trailing header
    # (written with [HasHeaderRowAtEnd]) is complete; the leading header and the rows written
    # before the new stat appeared are prefixes of it.
    if len(lines) < 4:
        raise ValueError('CSV capture is empty or missing its trailing header and metadata.')
    first_header, header, trailer = lines[0], lines[-2], lines[-1]
    if not header or len(header) != len(set(header)):
        raise ValueError('CSV header is missing or duplicated.')
    if not first_header or header[:len(first_header)] != first_header:
        raise ValueError('CSV leading header is not a prefix of the trailing header.')
    rows: list[dict[str, float]] = []
    for fields in lines[1:-2]:
        if not len(first_header) <= len(fields) <= len(header) or fields[0] == header[0]:
            raise ValueError('CSV data row is truncated or malformed.')
        # Columns past the end of a shorter row did not exist yet in that frame and stay absent.
        row = {}
        for name, value in zip(header, fields):
            if name.startswith('KawaiiPhysics/'):
                try:
                    row[name] = float(value) if value else 0.0
                except ValueError as error:
                    raise ValueError(f'Invalid CSV value in {name}.') from error
                if not math.isfinite(row[name]):
                    raise ValueError(f'Nonfinite CSV value in {name}.')
        rows.append(row)
    if not rows or not trailer or not trailer[0].startswith('[HasHeaderRowAtEnd]'):
        raise ValueError('CSV capture is empty or missing its trailing header and metadata.')
    if len(trailer) % 2:
        raise ValueError('CSV metadata has an odd number of fields.')
    metadata = {key.strip('[]').lower(): value for key, value in zip(trailer[::2], trailer[1::2])}
    if 'engineversion' not in metadata or 'config' not in metadata:
        raise ValueError('CSV engineversion or config metadata is missing.')
    return rows, metadata


def aggregate_csv(rows: list[dict[str, float]], profile: str, state: str) -> tuple[dict, dict]:
    """Sum a stat across worker columns inside each frame before averaging."""
    if not rows:
        raise ValueError('No CSV frames.')
    columns = set().union(*(row.keys() for row in rows))
    if state == 'on' and not any(name.endswith('/Eval') and
                                  (name.startswith('KawaiiPhysics/GameThread/') if profile == 'serial'
                                   else name.startswith('KawaiiPhysics/')) for name in columns):
        raise ValueError('Eval timing column is missing for the selected profile.')

    def timing(name: str) -> float:
        matching = [column for column in columns if column.startswith('KawaiiPhysics/')
                    and not column.startswith('KawaiiPhysics/COUNTS/')
                    and '/Num' not in column and column.endswith('/' + name)]
        return statistics.fmean(sum(row.get(column, 0.0) for column in matching) for row in rows)

    metrics = {key: timing(stat) for key, stat in METRIC_STATS.items()}
    metrics['kp_runtime_ms'] = metrics['kp_eval_ms'] - metrics['kp_editor_sync_ms']
    amount_columns: dict[str, list[str]] = {}
    for column in columns:
        match = re.fullmatch(r'KawaiiPhysics/(?:AllWorkers/)?(Num[^/]+)', column)
        if match:
            amount_columns.setdefault(match.group(1), []).append(column)
    amounts = {name: statistics.fmean(sum(row.get(column, 0.0) for column in names)
                                      for row in rows)
               for name, names in amount_columns.items()}
    return metrics, amounts


def check_controls(runs: list[dict], expected: dict[str, float]) -> dict:
    problems = []
    negative_ok = True
    amounts_ok = True
    for run in runs:
        if run['state'] == 'off':
            observed = {name: run['metrics'].get(name, 0.0)
                        for name in ('kp_eval_ms', 'kp_simulate_ms')}
            observed.update(run['amounts'])
            for name, value in observed.items():
                if abs(value) > 0.001:
                    negative_ok = False
                    problems.append(f"OFF #{run['index']}: {name}={value:.6g} が 0.001 を超える")
        else:
            for name, minimum in expected.items():
                value = run['amounts'].get(name, 0.0)
                if value < minimum:
                    amounts_ok = False
                    problems.append(f"ON #{run['index']}: {name}={value:.6g} は下限 {minimum} 未満")
    return {'negative_ok': negative_ok, 'amounts_ok': amounts_ok, 'problems': problems}


def digest_bytes(parts) -> str:
    digest = hashlib.sha256()
    for part in parts:
        digest.update(part)
    return digest.hexdigest()


def condition_key(conditions: dict) -> str:
    return digest_bytes([json.dumps(conditions, sort_keys=True).encode('utf-8')])


PACKAGE_REFERENCE = re.compile(rb'/(?:Game|KawaiiPhysicsDev)/[A-Za-z0-9_/\-.]+')


def package_file(asset: str, root: pathlib.Path = ROOT) -> pathlib.Path:
    package = asset.split('.')[0]
    if package.startswith('/Game/'):
        base = root / 'Content'
        relative = package[len('/Game/'):]
    elif package.startswith('/KawaiiPhysicsDev/'):
        base = root / 'Plugins/KawaiiPhysicsDev/Content'
        relative = package[len('/KawaiiPhysicsDev/'):]
    else:
        raise ValueError(f'対象外のアセットパス: {asset}')
    return base / (relative + '.uasset')


def collect_asset_files(suite: dict, root: pathlib.Path = ROOT) -> list[pathlib.Path]:
    """Collect existing packages reachable from suite assets and the level."""
    pending = [fixture[key] for fixture in suite['fixtures'].values()
               for key in ('mesh', 'anim_class')]
    pending.append(suite['level'])
    found: set[pathlib.Path] = set()
    visited: set[str] = set()
    while pending:
        package = pending.pop().split('.')[0]
        if package in visited:
            continue
        visited.add(package)
        candidate = package_file(package, root)
        paths = ([candidate.with_suffix('.umap'), candidate] if package == suite['level']
                 else [candidate, candidate.with_suffix('.umap')])
        path = next((item for item in paths if item.is_file()), None)
        if path is None:
            continue
        found.add(path)
        pending.extend(match.decode('ascii').split('.')[0]
                       for match in PACKAGE_REFERENCE.findall(path.read_bytes()))
    return sorted(found, key=lambda path: path.relative_to(root).as_posix())


def asset_signature(paths: list[pathlib.Path], root: pathlib.Path = ROOT) -> str:
    return digest_bytes(part for path in paths
                        for part in (path.relative_to(root).as_posix().encode('utf-8'),
                                     b'\0', path.read_bytes()))


def validate_settings(actual: object, expected: dict, cvars: list[str]) -> None:
    if not isinstance(actual, dict):
        raise ValueError('ランナーの settings がありません')
    for name, value in expected.items():
        observed = actual.get(name)
        same_type = (isinstance(observed, bool) if isinstance(value, bool) else
                     isinstance(observed, (int, float)) and not isinstance(observed, bool)
                     if isinstance(value, (int, float)) else type(observed) is type(value))
        if name not in actual or not same_type or observed != value:
            raise ValueError(f'ランナーの settings.{name} が要求値と違います')
    for cvar in cvars:
        name, _, value = cvar.partition('=')
        field = {'a.ParallelAnimEvaluation': 'parallel_anim_evaluation',
                 'a.ParallelAnimUpdate': 'parallel_anim_update'}.get(name)
        if field and (field not in actual or actual[field] != int(value)):
            raise ValueError(f'ランナーの settings.{field} が要求値と違います')


def validate_disable(runner: dict, state: str) -> None:
    for field in ('kp_disabled_requested', 'kp_nodes', 'kp_disabled_nodes'):
        if field not in runner:
            raise ValueError(f'ランナーが古いため {field} がありません')
    requested = runner['kp_disabled_requested']
    if not isinstance(requested, bool) or requested != (state == 'off'):
        raise ValueError('ランナーの kp_disabled_requested が要求値と違います')
    nodes = runner['kp_nodes']
    if isinstance(nodes, bool) or not isinstance(nodes, int) or nodes <= 0:
        raise ValueError('ランナーの kp_nodes が正の整数ではありません')
    disabled = runner['kp_disabled_nodes']
    if isinstance(disabled, bool) or not isinstance(disabled, int):
        raise ValueError('ランナーの kp_disabled_nodes が整数ではありません')
    if disabled != (nodes if state == 'off' else 0):
        raise ValueError('ランナーの kp_disabled_nodes が要求値と違います')


def record_runner_build(engine: dict, runner: dict, expected: str | None = None) -> None:
    build = runner.get('build')
    if not isinstance(build, str) or not build:
        raise ValueError('ランナーの build がありません（ランナーが古い可能性があります）')
    if expected is not None and build != expected:
        raise ValueError(f'ランナーの build が条件と違います: {build} / {expected}')
    if engine['runner_build'] is None:
        engine['runner_build'] = build
    elif engine['runner_build'] != build:
        raise ValueError('計測中にランナーの build が変わりました')


def validate_effective_cvars(actual: object, requested: list[str]) -> None:
    if not isinstance(actual, dict):
        raise ValueError('ランナーの effective_cvars がありません')
    for cvar in requested:
        name, separator, expected = cvar.partition('=')
        if not separator or not name or name not in actual or actual[name] is None:
            raise ValueError(f'ランナーの effective_cvars.{name} がありません')
        observed = actual[name]
        if not isinstance(observed, str):
            raise ValueError(f'ランナーの effective_cvars.{name} が文字列ではありません')
        # Bool CVars report "true"/"false" through IConsoleVariable::GetString while the request
        # uses 1/0, so both spellings are compared as numbers.
        booleans = {'true': '1', 'false': '0'}
        try:
            expected_number = float(booleans.get(expected.strip().lower(), expected))
            observed_number = float(booleans.get(observed.strip().lower(), observed))
        except ValueError:
            matches = observed == expected
        else:
            matches = (math.isfinite(expected_number) and math.isfinite(observed_number)
                       and observed_number == expected_number)
        if not matches:
            raise ValueError(f'ランナーの effective_cvars.{name}={observed} が要求値 {expected} と違います')


def capture_window(runner: dict, frames: int, csv_rows: int) -> list[float]:
    if csv_rows != frames:
        raise ValueError('CSV の記録フレーム数が要求値と違います')
    start = runner['csv']['start_runner_frame']
    if not isinstance(start, int) or start < 0:
        raise ValueError('start_runner_frame が不正です')
    window = runner['frame_ms'][start:start + frames]
    if len(window) != csv_rows:
        raise ValueError('frame_ms の窓と CSV の記録フレーム数が違います')
    return window


def audit_fixture(suite: dict, group: dict, amounts: dict[str, float]) -> dict:
    expected = {name: minimum * group['count']
                for name, minimum in suite['fixtures'][group['fixture']].get('expect', {}).items()}
    ok = all(amounts.get(name, 0.0) >= minimum for name, minimum in expected.items())
    return {'count': group['count'], 'expected': expected, 'amounts': amounts, 'ok': ok}


def source_signatures(suite: dict, root: pathlib.Path = ROOT) -> dict:
    paths = collect_asset_files(suite, root)
    runner = root / 'Plugins/KawaiiPhysicsDev/Source/KawaiiPhysicsVerification/Private'
    return {'level_sha256': digest_bytes([package_file(suite['level'], root).with_suffix('.umap').read_bytes()]),
            'assets_sha256': asset_signature(paths, root), 'asset_count': len(paths),
            'runner_cpp_sha256': digest_bytes((runner / name).read_bytes() for name in
                ('KawaiiPhysicsVerificationSubsystem.h', 'KawaiiPhysicsVerificationSubsystem.cpp'))}


PACKAGE_SCHEMA = 'kpv-package/1'
PACKAGE_FIELDS = ('id', 'identity', 'created', 'source', 'executable', 'expected_build',
                  'runner_cpp_sha256', 'level_sha256', 'assets_sha256', 'asset_count',
                  'plugin_tree', 'toolchain', 'ddc', 'uat_log')
IDENTITY_FIELDS = ('commit', 'dirty', 'dirty_sha256', 'engine_version', 'build_arguments',
                   'suite_sha256', 'variant')


def load_package(directory: pathlib.Path) -> dict:
    package = json.loads((directory / 'package.json').read_text(encoding='utf-8'))
    if not isinstance(package, dict) or package.get('schema') != PACKAGE_SCHEMA:
        raise ValueError(f'package.json の schema が {PACKAGE_SCHEMA} ではありません')
    for field in PACKAGE_FIELDS:
        if field not in package:
            raise ValueError(f'package.json に {field} がありません')
    identity = package['identity']
    if not isinstance(identity, dict) or any(field not in identity for field in IDENTITY_FIELDS):
        raise ValueError('package.json の identity の必須キーがありません')
    for field in ('id', 'executable', 'expected_build', 'runner_cpp_sha256', 'level_sha256',
                  'assets_sha256'):
        if not isinstance(package[field], str) or not package[field]:
            raise ValueError(f'package.json の {field} が文字列ではありません')
    if (not isinstance(identity['commit'], str) or not identity['commit'] or
            not isinstance(identity['dirty'], bool) or
            (identity['dirty_sha256'] is not None and not isinstance(identity['dirty_sha256'], str)) or
            type(package['asset_count']) is not int or package['asset_count'] < 0):
        raise ValueError('package.json の identity または asset_count が不正です')
    executable = pathlib.Path(package['executable'])
    if executable.is_absolute() or executable.drive or '..' in executable.parts:
        raise ValueError('package.json の executable はパッケージ内の相対パスにしてください')
    if not (directory / executable).is_file():
        raise ValueError('package.json の executable がありません')
    return package


def package_provenance(package: dict, directory: pathlib.Path) -> dict:
    identity = package['identity']
    return {'commit': identity['commit'][:8], 'dirty': identity['dirty'],
            'plugin_diff_sha256': identity['dirty_sha256'], 'package_id': package['id'],
            'package_dir': str(directory)}


def toolchain_version(text: str | None) -> str | None:
    match = re.search(r'\d+\.\d+\.\d+', text or '')
    return match.group() if match else None


def conditions_for(suite: dict, suite_path: pathlib.Path, profile: str, runs: int,
                   package: dict | None = None) -> dict:
    scripts = [pathlib.Path(__file__), pathlib.Path(run_cells.__file__)]
    if package is None:
        signatures = source_signatures(suite)
        runner = ROOT / 'Plugins/KawaiiPhysicsDev/Source/KawaiiPhysicsVerification/Private'
        runner_hash = digest_bytes(path.read_bytes() for path in
            [runner / 'KawaiiPhysicsVerificationSubsystem.h',
             runner / 'KawaiiPhysicsVerificationSubsystem.cpp', *scripts])
    else:
        signatures = package
        runner_hash = digest_bytes([package['runner_cpp_sha256'].encode('utf-8'),
                                   *(path.read_bytes() for path in scripts)])
    return {'suite': suite_path.stem,
            'suite_sha256': digest_bytes([suite_path.read_bytes()]),
            'level': suite['level'], 'level_sha256': signatures['level_sha256'],
            'assets_sha256': signatures['assets_sha256'], 'asset_count': signatures['asset_count'],
            'runner_sha256': runner_hash, 'build': package['expected_build'] if package else 'editor',
            'toolchain': toolchain_version(package['toolchain']) if package else None,
            'profile': profile, 'fps': suite['fps'], 'frames': suite['frames'],
            'capture': suite['capture'], 'runs_per_state': runs,
            'settings': suite['settings'],
            'cvars': suite['profiles'][profile]['cvars'],
            'extra_arguments': [*(['-game'] if package is None else []),
                                '-nullrhi', '-unattended', '-nosplash', '-nosound',
                                '-nopause', '-deterministic', f"-FPS={suite['fps']}"],
            'affinity': None}


def machine_info() -> dict:
    with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,
                        r'HARDWARE\DESCRIPTION\System\CentralProcessor\0') as key:
        cpu = winreg.QueryValueEx(key, 'ProcessorNameString')[0].strip()
    output = subprocess.run(['powercfg', '/getactivescheme'], capture_output=True, text=True,
                            check=True).stdout
    match = re.search(r'\b[0-9a-fA-F]{8}(?:-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12}\b', output)
    if not match:
        raise ValueError('Cannot read the active power plan GUID.')
    return {'machine_id': re.sub(r'[^a-z0-9]+', '-', cpu.lower()).strip('-'),
            'cpu': cpu, 'logical_cpus': os.cpu_count(), 'power_plan': match.group().lower()}


def provenance() -> dict:
    def git(*arguments) -> bytes | None:
        try:
            return subprocess.run(['git', *arguments], cwd=ROOT, capture_output=True,
                                  check=True).stdout.strip()
        except (OSError, subprocess.CalledProcessError):
            return None
    commit = git('rev-parse', '--short', 'HEAD')
    status = git('status', '--porcelain', '--', 'Plugins/KawaiiPhysics')
    difference = git('diff', 'HEAD', '--', 'Plugins/KawaiiPhysics')
    return {'commit': commit.decode() if commit is not None else None,
            'dirty': bool(status) if status is not None else None,
            'plugin_diff_sha256': digest_bytes([difference]) if status and difference is not None else None}


def summarize(runs: list[dict], expected: dict[str, float]) -> dict:
    on = [run for run in runs if run['state'] == 'on']
    off = [run for run in runs if run['state'] == 'off']
    metrics = {}
    for name in ('kp_runtime_ms', *METRIC_STATS):
        values = [run['metrics'][name] for run in on]
        metrics[name] = {'unit': 'ms', 'gating': name == 'kp_runtime_ms',
                         'values': values, 'median': statistics.median(values)}
    costs = [a['frame_ms_median'] - b['frame_ms_median'] for a, b in zip(on, off)]
    metrics['frame_cost_ms'] = {'unit': 'ms', 'gating': False, 'values': costs,
                                'median': statistics.median(costs)}
    names = set().union(*(run['amounts'] for run in on))
    amounts = {name: statistics.median(run['amounts'].get(name, 0.0) for run in on)
               for name in names}
    return {'metrics': metrics, 'amounts': amounts,
            'calib_ms': statistics.median(run['calib_ms'] for run in runs),
            'controls': check_controls(runs, expected)}


def main(argv: list[str] | None = None) -> int:
    sys.stdout.reconfigure(encoding='utf-8', errors='replace', line_buffering=True)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output_directory')
    parser.add_argument('--profile', choices=('serial', 'parallel'), default='serial')
    parser.add_argument('--runs', type=int)
    parser.add_argument('--positive-control', action='store_true')
    parser.add_argument('--audit', dest='audit', action='store_true', default=True,
                        help='fixture ごとの監査を行う（既定）')
    parser.add_argument('--no-audit', dest='audit', action='store_false',
                        help='監査を省く。結果の audit は null になり、比較には使えません')
    parser.add_argument('--suite', type=pathlib.Path, default=DEFAULT_SUITE)
    parser.add_argument('--package', type=pathlib.Path)
    parser.add_argument('--timeout', type=int, default=600)
    arguments = parser.parse_args(argv)
    package_dir = arguments.package.resolve() if arguments.package is not None else None
    try:
        package = load_package(package_dir) if package_dir is not None else None
    except (OSError, ValueError, TypeError) as error:
        print(f'ERROR: パッケージを読めません: {error}', file=sys.stderr)
        return 1
    launch = {'executable': package_dir / package['executable']} if package is not None else {}
    suite_path = arguments.suite.resolve()
    suite = json.loads(suite_path.read_text(encoding='utf-8'))
    # cook するディレクトリはパッケージ作成時の suite から決まるため、別の suite では fixture が欠け得る。
    if package is not None and package['identity']['suite_sha256'] != digest_bytes([suite_path.read_bytes()]):
        print('ERROR: パッケージを作った suite と今の suite が違います。パッケージを作り直してください',
              file=sys.stderr)
        return 1
    # コンパイラの差を比較条件で見分けるため、版が読めないパッケージは計測しない（MSVC だけに対応）。
    if package is not None and toolchain_version(package['toolchain']) is None:
        print('ERROR: パッケージの toolchain から MSVC の版を読めません', file=sys.stderr)
        return 1
    count = arguments.runs if arguments.runs is not None else suite['runs_per_state']
    if count < 1 or arguments.timeout < 1:
        parser.error('--runs and --timeout must be positive')
    directory = pathlib.Path(arguments.output_directory).resolve()
    directory.mkdir(parents=True, exist_ok=True)
    conditions = conditions_for(suite, suite_path, arguments.profile, count, package)
    key = condition_key(conditions)
    result = {'schema': 'kpv-perf-result/1',
              'created': datetime.datetime.now().astimezone().isoformat(timespec='seconds'),
              'machine': machine_info(),
              'engine': {'root': str(package_dir if package is not None else run_cells.DEFAULT_ENGINE).replace('\\', '/'),
                         'version': None, 'build_config': None, 'runner_build': None},
              'conditions': conditions, 'condition_key': key,
              'positive_control': {'overrides': {'TargetFramerate': '120'}}
              if arguments.positive_control else None,
              'provenance': package_provenance(package, package_dir) if package is not None else provenance(),
              'audit': None, 'runs': [], 'summary': None}
    cells = expand_suite(suite, arguments.positive_control)
    failures = []
    if arguments.audit:
        audit = {'ok': True, 'fixtures': {}, 'problems': []}
        result['audit'] = audit
        offset = 0
        for group in suite['groups']:
            fixture = group['fixture']
            selected = cells[offset:offset + group['count']]
            offset += group['count']
            stem = f'audit_{fixture}'
            csv_path = directory / (stem + '.csv')
            manifest = make_manifest(suite, selected, csv_path, 'on', audit=True)
            cvars = make_cvars(conditions['cvars'], 'on')
            manifest_path = directory / (stem + '.json')
            manifest_path.write_text(json.dumps(manifest, indent=2), encoding='utf-8')
            output = directory / (stem + '.jsonl')
            try:
                if not on_ac_power():
                    raise ValueError('AC 電源から外れました')
                outcome = run_cells.run(manifest_path, output, level=suite['level'], fps=suite['fps'],
                                        timeout=arguments.timeout, cvars=cvars, **launch)
                if outcome['exit_code'] or not outcome['recorded'] or outcome['severe_log']:
                    raise ValueError('ランナー失敗または重大ログ')
                runner = json.loads(output.read_text(encoding='utf-8').splitlines()[0])
                if not runner['ok'] or runner.get('csv', {}).get('frames') != suite['audit']['capture']['frames']:
                    raise ValueError('CSV 記録メタデータが不正です')
                validate_settings(runner.get('settings'), suite['settings'], cvars)
                validate_disable(runner, 'on')
                record_runner_build(result['engine'], runner, conditions['build'])
                validate_effective_cvars(runner.get('effective_cvars'), cvars)
                rows, _ = read_csv_capture(pathlib.Path(runner['csv']['path']))
                capture_window(runner, suite['audit']['capture']['frames'], len(rows))
                _, amounts = aggregate_csv(rows, arguments.profile, 'on')
                entry = audit_fixture(suite, group, amounts)
                audit['fixtures'][fixture] = entry
                if not entry['ok']:
                    audit['problems'].append(f'{fixture}: 量が下限を満たしません')
            except (OSError, subprocess.SubprocessError, KeyError, TypeError, ValueError,
                    json.JSONDecodeError) as error:
                audit['fixtures'][fixture] = audit_fixture(suite, group, {})
                audit['problems'].append(f'{fixture}: {error}')
        audit['ok'] = not audit['problems']
        failures.extend(audit['problems'])
    for index in range(count * 2) if not failures else range(0):
        state = 'on' if index % 2 == 0 else 'off'
        if not on_ac_power():
            failures.append('AC 電源から外れたため計測を停止')
            break
        stem = f'{arguments.profile}_{index:02d}_{state}'
        csv_path = directory / (stem + '.csv')
        manifest = make_manifest(suite, cells, csv_path, state)
        manifest_path = directory / (stem + '.json')
        manifest_path.write_text(json.dumps(manifest, indent=2), encoding='utf-8')
        output = directory / (stem + '.jsonl')
        cvars = make_cvars(conditions['cvars'], state)
        try:
            outcome = run_cells.run(manifest_path, output, level=suite['level'], fps=suite['fps'],
                                    timeout=arguments.timeout, cvars=cvars, **launch)
        except (OSError, subprocess.SubprocessError) as error:
            print(f'{index + 1}/{count * 2} {state.upper()}: failed')
            failures.append(f'{stem}: {error}')
            break
        print(f"{index + 1}/{count * 2} {state.upper()}: exit={outcome['exit_code']} "
              f"seconds={outcome['seconds']:.1f}")
        if outcome['exit_code'] or not outcome['recorded'] or outcome['severe_log']:
            failures.append(f"{stem}: ランナー失敗または重大ログ")
            continue
        try:
            runner = json.loads(output.read_text(encoding='utf-8').splitlines()[0])
            if not runner['ok'] or runner.get('csv', {}).get('frames') != suite['capture']['frames']:
                raise ValueError('Runner capture metadata is missing or failed.')
            validate_settings(runner.get('settings'), suite['settings'], cvars)
            validate_disable(runner, state)
            record_runner_build(result['engine'], runner, conditions['build'])
            validate_effective_cvars(runner.get('effective_cvars'), cvars)
            actual_csv = pathlib.Path(runner['csv']['path'])
            rows, metadata = read_csv_capture(actual_csv)
            window = capture_window(runner, suite['capture']['frames'], len(rows))
            if result['engine']['version'] is None:
                result['engine']['version'] = metadata['engineversion']
                result['engine']['build_config'] = metadata['config']
            elif (result['engine']['version'], result['engine']['build_config']) != (metadata['engineversion'], metadata['config']):
                raise ValueError('Engine metadata changed within the batch.')
            metrics, amounts = aggregate_csv(rows, arguments.profile, state)
            calibration = [*runner['calib_start_ms'], *runner['calib_end_ms']]
            if not calibration:
                raise ValueError('Runner calibration is empty.')
            result['runs'].append({'index': index, 'state': state,
                                   'exit_code': outcome['exit_code'], 'seconds': outcome['seconds'],
                                   'csv_path': str(actual_csv).replace('\\', '/'),
                                   'capture_frames': len(rows),
                                   'frame_ms_median': statistics.median(window),
                                   'calib_ms': statistics.median(calibration),
                                   'metrics': metrics, 'amounts': amounts})
        except (OSError, KeyError, TypeError, ValueError, json.JSONDecodeError) as error:
            failures.append(f'{stem}: {error}')
    if len(result['runs']) == count * 2:
        result['summary'] = summarize(result['runs'], expected_amounts(suite))
        failures.extend(result['summary']['controls']['problems'])
    else:
        failures.append('全実行の有効な CSV と JSONL が揃わない')
        result['summary'] = {'metrics': {}, 'amounts': {}, 'calib_ms': 0.0,
                             'controls': {'negative_ok': False, 'amounts_ok': False,
                                          'problems': failures[:]}}
    result_path = directory / 'result.json'
    result_path.write_text(json.dumps(result, indent=2, sort_keys=True, allow_nan=False) + '\n', encoding='utf-8')
    for failure in failures:
        print(f'ERROR: {failure}', file=sys.stderr)
    print(result_path)
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
