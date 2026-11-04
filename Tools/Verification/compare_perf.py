"""Compare a KawaiiPhysics performance result with a saved baseline."""

from __future__ import annotations

import argparse
import datetime
import json
import math
import pathlib
import random
import re
import statistics
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
SCHEMA = 'kpv-perf-result/1'
EXIT_CODES = {'OK': 0, 'IMPROVED': 0, 'WARN': 1, 'NOISY': 2,
              'STALE': 3, 'WORKLOAD_CHANGED': 3, 'MACHINE_MISMATCH': 4,
              'INVALID': 5, 'NO_BASELINE': 6}


def bootstrap_sigma(values: list[float], batch_size: int | None = None,
                    resamples: int = 2000) -> float:
    """Return the standard deviation of two bootstrapped batch medians' gap."""
    generator = random.Random(0)
    size = len(values) if batch_size is None else batch_size
    gaps = [statistics.median(generator.choices(values, k=size)) -
            statistics.median(generator.choices(values, k=size))
            for _ in range(resamples)]
    return statistics.stdev(gaps)


def threshold_percent(values: list[float], median: float, floor_percent: float) -> float:
    return max(floor_percent, 3 * bootstrap_sigma(values) / median * 100)


def _same(first: float, second: float) -> bool:
    return math.isclose(first, second, rel_tol=1e-9, abs_tol=1e-12)


def summary_consistency_problems(data: dict, label: str) -> list[str]:
    """Recompute the summary from the runs so a stale or edited summary cannot pass.

    Called only after the structure has been validated."""
    problems = []
    runs = sorted(data['runs'], key=lambda run: run['index'])
    on = [run for run in runs if run['state'] == 'on']
    off = [run for run in runs if run['state'] == 'off']
    summary = data['summary']
    for name, metric in summary['metrics'].items():
        if name == 'frame_cost_ms':
            expected = [a['frame_ms_median'] - b['frame_ms_median'] for a, b in zip(on, off)]
        elif all(name in run['metrics'] for run in on):
            expected = [run['metrics'][name] for run in on]
        else:
            problems.append(f'{label}.summary.metrics.{name} が ON の実行に無い指標です')
            continue
        values = metric['values']
        if len(values) != len(expected) or not all(_same(a, b) for a, b in zip(values, expected)):
            problems.append(f'{label}.summary.metrics.{name}.values が実行ごとの値と一致しません')
        elif not _same(metric['median'], statistics.median(expected)):
            problems.append(f'{label}.summary.metrics.{name}.median が values の中央値と一致しません')
    names = set().union(*(run['amounts'] for run in on)) if on else set()
    if set(summary['amounts']) != names:
        problems.append(f'{label}.summary.amounts の量の集合が ON の実行と一致しません')
    else:
        for name in names:
            expected = statistics.median(run['amounts'].get(name, 0.0) for run in on)
            if not _same(summary['amounts'][name], expected):
                problems.append(f'{label}.summary.amounts.{name} が ON の実行の中央値と一致しません')
    return problems


def _amount_changed(old: object, new: object) -> bool:
    if not isinstance(old, (int, float)) or not isinstance(new, (int, float)):
        return True
    if old == 0:
        return new != 0
    return abs((new / old - 1) * 100) > 1


def fixture_workload_differences(baseline: dict, current: dict) -> list[str]:
    """List fixture-level audit amount differences beyond 1% (shared with the A/A analysis)."""
    differences = []
    old_fixtures = baseline['audit']['fixtures']
    new_fixtures = current['audit']['fixtures']
    for fixture in sorted(old_fixtures.keys() | new_fixtures.keys()):
        if fixture not in old_fixtures or fixture not in new_fixtures:
            differences.append(f'{fixture}: fixture の集合が違います')
            continue
        old_fixture = old_fixtures[fixture]['amounts']
        new_fixture = new_fixtures[fixture]['amounts']
        for name in sorted(old_fixture.keys() | new_fixture.keys()):
            old, new = old_fixture.get(name), new_fixture.get(name)
            if _amount_changed(old, new):
                differences.append(f'{fixture}.{name}: fixture の量が変わった'
                                   f'（基準値={old} / 現在={new}）')
    return differences


def _mapping(value: object, path: str, problems: list[str]) -> dict:
    if not isinstance(value, dict):
        problems.append(f'{path} がオブジェクトではありません')
        return {}
    return value


def _required(mapping: dict, names: tuple[str, ...], path: str,
              problems: list[str]) -> None:
    for name in names:
        if name not in mapping:
            problems.append(f'{path}.{name} がありません')


def _finite(value: object, path: str, problems: list[str]) -> None:
    if isinstance(value, dict):
        for key, item in value.items():
            _finite(item, f'{path}.{key}', problems)
    elif isinstance(value, list):
        for index, item in enumerate(value):
            _finite(item, f'{path}[{index}]', problems)
    elif isinstance(value, (int, float)) and not isinstance(value, bool):
        if not math.isfinite(value):
            problems.append(f'{path} が有限値ではありません')


def _number(value: object, path: str, problems: list[str]) -> float | None:
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        problems.append(f'{path} が有限の数値ではありません')
        return None
    return float(value)


def validate(document: object, label: str, baseline: bool = False,
             max_threshold_percent: float = 10.0, floor_percent: float = 2.0) -> list[str]:
    problems: list[str] = []
    data = _mapping(document, label, problems)
    _required(data, ('schema', 'created', 'machine', 'engine', 'conditions',
                     'condition_key', 'positive_control', 'provenance', 'runs', 'summary', 'audit'),
              label, problems)
    if data.get('schema') != SCHEMA:
        problems.append(f'{label}.schema が {SCHEMA} ではありません')
    _finite(data, label, problems)
    machine = _mapping(data.get('machine'), f'{label}.machine', problems)
    engine = _mapping(data.get('engine'), f'{label}.engine', problems)
    conditions = _mapping(data.get('conditions'), f'{label}.conditions', problems)
    summary = _mapping(data.get('summary'), f'{label}.summary', problems)
    metrics = _mapping(summary.get('metrics'), f'{label}.summary.metrics', problems)
    controls = _mapping(summary.get('controls'), f'{label}.summary.controls', problems)
    capture = _mapping(conditions.get('capture'), f'{label}.conditions.capture', problems)
    _required(machine, ('machine_id', 'cpu', 'logical_cpus', 'power_plan'),
              f'{label}.machine', problems)
    _required(engine, ('root', 'version', 'build_config'), f'{label}.engine', problems)
    _required(conditions, ('suite', 'suite_sha256', 'level', 'level_sha256',
                           'assets_sha256', 'asset_count', 'runner_sha256', 'profile', 'fps', 'frames',
                           'settings',
                           'capture', 'runs_per_state', 'cvars', 'extra_arguments', 'affinity'),
              f'{label}.conditions', problems)
    _required(capture, ('start_frame', 'frames'), f'{label}.conditions.capture', problems)
    _required(summary, ('metrics', 'amounts', 'calib_ms', 'controls'), f'{label}.summary', problems)
    _required(metrics, ('kp_runtime_ms',), f'{label}.summary.metrics', problems)
    _required(controls, ('negative_ok', 'amounts_ok', 'problems'),
              f'{label}.summary.controls', problems)
    machine_id = machine.get('machine_id')
    if not isinstance(machine_id, str) or not re.fullmatch(r'[a-z0-9]+(?:-[a-z0-9]+)*', machine_id):
        problems.append(f'{label}.machine.machine_id が正規化された名前ではありません')
    if conditions.get('profile') not in ('serial', 'parallel'):
        problems.append(f'{label}.conditions.profile が serial/parallel ではありません')
    if data.get('positive_control') is not None and not isinstance(data['positive_control'], dict):
        problems.append(f'{label}.positive_control がオブジェクトではありません')
    if baseline and data.get('positive_control') is not None:
        problems.append(f'{label}: 陽性対照の結果は基準値にできません')
    audit = data.get('audit')
    if not isinstance(audit, dict) or audit.get('ok') is not True:
        target = '基準値' if baseline else '現在の結果'
        problems.append(f'{label}: {target}に --audit 付きで成功した fixture 監査が必要です')
    else:
        fixtures = _mapping(audit.get('fixtures'), f'{label}.audit.fixtures', problems)
        for fixture, entry in fixtures.items():
            item = _mapping(entry, f'{label}.audit.fixtures.{fixture}', problems)
            amounts = _mapping(item.get('amounts'),
                               f'{label}.audit.fixtures.{fixture}.amounts', problems)
            for name, value in amounts.items():
                _number(value, f'{label}.audit.fixtures.{fixture}.amounts.{name}', problems)
    for key in ('negative_ok', 'amounts_ok'):
        if controls.get(key) is not True:
            problems.append(f'{label}.summary.controls.{key} が true ではありません')
    for key in ('runs_per_state',):
        value = conditions.get(key)
        if isinstance(value, bool) or not isinstance(value, int) or value < 1:
            problems.append(f'{label}.conditions.{key} が正の整数ではありません')
    frames = _number(capture.get('frames'), f'{label}.conditions.capture.frames', problems)
    if frames is not None and frames <= 0:
        problems.append(f'{label}.conditions.capture.frames が正ではありません')
    runs = data.get('runs')
    n = conditions.get('runs_per_state')
    if not isinstance(runs, list):
        problems.append(f'{label}.runs が配列ではありません')
    elif isinstance(n, int) and not isinstance(n, bool) and n > 0:
        if len(runs) != n * 2:
            problems.append(f'{label}.runs の数が runs_per_state × 2 と違います')
        counts = {'on': 0, 'off': 0}
        for index, run in enumerate(runs):
            row = _mapping(run, f'{label}.runs[{index}]', problems)
            _required(row, ('index', 'state', 'exit_code', 'seconds', 'csv_path',
                            'capture_frames', 'frame_ms_median', 'calib_ms',
                            'metrics', 'amounts'), f'{label}.runs[{index}]', problems)
            state = row.get('state')
            if isinstance(state, str) and state in counts:
                counts[state] += 1
            else:
                problems.append(f'{label}.runs[{index}].state が on/off ではありません')
            if row.get('exit_code') != 0 or isinstance(row.get('exit_code'), bool):
                problems.append(f'{label}.runs[{index}].exit_code が 0 ではありません')
            captured = _number(row.get('capture_frames'),
                               f'{label}.runs[{index}].capture_frames', problems)
            if frames is not None and captured is not None and captured < frames * 0.9:
                problems.append(f'{label}.runs[{index}] の記録が短すぎます')
            for field in ('seconds', 'frame_ms_median', 'calib_ms'):
                _number(row.get(field), f'{label}.runs[{index}].{field}', problems)
            for field in ('metrics', 'amounts'):
                values = _mapping(row.get(field), f'{label}.runs[{index}].{field}', problems)
                for key, value in values.items():
                    _number(value, f'{label}.runs[{index}].{field}.{key}', problems)
        if counts['on'] != n or counts['off'] != n:
            problems.append(f'{label}.runs の ON/OFF 件数が runs_per_state と違います')
    amounts = _mapping(summary.get('amounts'), f'{label}.summary.amounts', problems)
    for name, value in amounts.items():
        _number(value, f'{label}.summary.amounts.{name}', problems)
    calib = _number(summary.get('calib_ms'), f'{label}.summary.calib_ms', problems)
    if calib is not None and calib <= 0:
        problems.append(f'{label}.summary.calib_ms が正ではありません')
    for name, raw in metrics.items():
        metric = _mapping(raw, f'{label}.summary.metrics.{name}', problems)
        _required(metric, ('unit', 'gating', 'values', 'median'),
                  f'{label}.summary.metrics.{name}', problems)
        values = metric.get('values')
        if not isinstance(values, list) or not values:
            problems.append(f'{label}.summary.metrics.{name}.values が空か配列ではありません')
            continue
        if isinstance(n, int) and n > 0 and len(values) != n:
            problems.append(f'{label}.summary.metrics.{name}.values の数が runs_per_state と違います')
        converted = [_number(value, f'{label}.summary.metrics.{name}.values[{index}]', problems)
                     for index, value in enumerate(values)]
        median = _number(metric.get('median'), f'{label}.summary.metrics.{name}.median', problems)
        if not isinstance(metric.get('unit'), str) or not isinstance(metric.get('gating'), bool):
            problems.append(f'{label}.summary.metrics.{name} の unit/gating が不正です')
        # Only the gating metric decides whether a baseline is usable; reference metrics may be
        # tiny or zero (an unused stage) and are reported without a verdict instead.
        gating = metric.get('gating') is True
        if baseline and gating and median is not None and median <= 0:
            problems.append(f'{label}.summary.metrics.{name}.median が 0 以下です')
        if (baseline and gating and median is not None and median > 0 and
                all(value is not None for value in converted)):
            threshold = threshold_percent(converted, median, floor_percent)
            if threshold > max_threshold_percent:
                problems.append(f'{label}.summary.metrics.{name}: 基準値として採用できない'
                                f'（ばらつきの閾値 {threshold:.2f}% > {max_threshold_percent:.2f}%）')
    primary = metrics.get('kp_runtime_ms')
    if isinstance(primary, dict) and primary.get('gating') is not True:
        problems.append(f'{label}.summary.metrics.kp_runtime_ms.gating が true ではありません')
    if not problems:
        problems += summary_consistency_problems(data, label)
    return problems


def compare(current: dict, baseline: dict | None, floor_percent: float = 2.0,
            max_threshold_percent: float = 10.0, calib_percent: float = 5.0) -> dict:
    """Return a serializable verdict, including detail rows and diagnostic messages."""
    result = {'verdict': 'OK', 'exit_code': 0, 'differences': [], 'metrics': [],
              'amounts': [], 'warnings': []}
    problems = validate(current, '現在', floor_percent=floor_percent)
    if baseline is not None:
        problems += validate(baseline, '基準値', baseline=True,
                             max_threshold_percent=max_threshold_percent,
                             floor_percent=floor_percent)
    if problems:
        result.update(verdict='INVALID', exit_code=5, warnings=problems)
        return result
    if baseline is None:
        result.update(verdict='NO_BASELINE', exit_code=6, warnings=['基準値なし'])
        return result
    for name in baseline['summary']['metrics'].keys() & current['summary']['metrics'].keys():
        old = baseline['summary']['metrics'][name]
        new = current['summary']['metrics'][name]
        if old['unit'] != new['unit'] or old['gating'] != new['gating']:
            result.update(verdict='INVALID', exit_code=5,
                          warnings=[f'{name} の unit/gating が基準値と違います'])
            return result
    if current['positive_control'] is not None:
        before = baseline['summary']['amounts'].get('NumSubsteps')
        after = current['summary']['amounts'].get('NumSubsteps')
        if (not isinstance(before, (int, float)) or before <= 0 or
                not isinstance(after, (int, float)) or after < before * 1.5):
            result.update(verdict='INVALID', exit_code=5,
                          warnings=['陽性対照が効いていない: NumSubsteps が基準値の 1.5 倍未満です'])
            return result
        result['warnings'].append('陽性対照: WARN が出ることを期待します')
    for section, fields in (('machine', ('machine_id', 'logical_cpus', 'power_plan')),
                            ('engine', ('version', 'build_config'))):
        for field in fields:
            if baseline[section][field] != current[section][field]:
                result['differences'].append(f'{section}.{field}: 基準値={baseline[section][field]} / '
                                             f'現在={current[section][field]}')
    if result['differences']:
        result.update(verdict='MACHINE_MISMATCH', exit_code=4)
        return result
    if baseline['condition_key'] != current['condition_key']:
        for key in sorted(baseline['conditions'].keys() | current['conditions'].keys()):
            if baseline['conditions'].get(key) != current['conditions'].get(key):
                result['differences'].append(f'conditions.{key}: '
                                             f'基準値={baseline["conditions"].get(key)} / '
                                             f'現在={current["conditions"].get(key)}')
        if not result['differences']:
            result['differences'].append('condition_key が違います')
        result.update(verdict='STALE', exit_code=3)
        return result
    if current['positive_control'] is None:
        result['warnings'].extend(fixture_workload_differences(baseline, current))
        old_amounts = baseline['summary']['amounts']
        new_amounts = current['summary']['amounts']
        for name in sorted(old_amounts.keys() | new_amounts.keys()):
            old, new = old_amounts.get(name), new_amounts.get(name)
            delta = ((new / old - 1) * 100 if old else (0.0 if new == 0 else None)) if (
                old is not None and new is not None) else None
            changed = (old is None or new is None or delta is None or abs(delta) > 1)
            result['amounts'].append({'name': name, 'baseline': old, 'current': new,
                                      'delta_percent': delta, 'changed': changed})
            if changed:
                result['warnings'].append(f'{name}: 量が変わった（処理の中身が変わった可能性）')
        if result['warnings']:
            result.update(verdict='WORKLOAD_CHANGED', exit_code=3)
            return result
    calib_delta = (current['summary']['calib_ms'] /
                   baseline['summary']['calib_ms'] - 1) * 100
    baseline_metrics = baseline['summary']['metrics']
    current_metrics = current['summary']['metrics']
    for name in sorted(baseline_metrics.keys() & current_metrics.keys()):
        old, new = baseline_metrics[name], current_metrics[name]
        if old['median'] <= 0:
            # A reference metric that the baseline never exercised has no relative change.
            result['metrics'].append({'name': name, 'baseline': old['median'],
                                      'current': new['median'], 'unit': old['unit'],
                                      'delta_percent': None, 'threshold_percent': None,
                                      'verdict': 'N/A', 'gating': old['gating']})
            continue
        delta = (new['median'] / old['median'] - 1) * 100
        threshold = threshold_percent(old['values'], old['median'], floor_percent)
        verdict = ('WARN' if delta > threshold else
                   'IMPROVED' if delta < -threshold else 'OK')
        if verdict == 'WARN' and calib_delta > calib_percent and delta <= calib_delta + threshold:
            verdict = 'NOISY'
        result['metrics'].append({'name': name, 'baseline': old['median'],
                                  'current': new['median'], 'unit': old['unit'],
                                  'delta_percent': delta, 'threshold_percent': threshold,
                                  'verdict': verdict, 'gating': old['gating']})
    gated = [row['verdict'] for row in result['metrics'] if row['gating']]
    for verdict in ('WARN', 'NOISY', 'IMPROVED'):
        if verdict in gated:
            result.update(verdict=verdict, exit_code=EXIT_CODES[verdict])
            break
    return result


def render(result: dict) -> str:
    lines = [f'判定: {result["verdict"]}（終了コード {result["exit_code"]}）', '']
    lines += ['比較条件の違い:']
    lines += [f'- {item}' for item in result['differences']] or ['- なし']
    lines += ['', '| 指標 | 基準値 | 現在 | 差% | 閾値% | 判定 | gating |',
              '|---|---:|---:|---:|---:|---|---|']
    for row in result['metrics']:
        delta = f'{row["delta_percent"]:+.2f}' if row['delta_percent'] is not None else '-'
        threshold = f'{row["threshold_percent"]:.2f}' if row['threshold_percent'] is not None else '-'
        lines.append(f'| {row["name"]} | {row["baseline"]:.4f} | {row["current"]:.4f} | '
                     f'{delta} | {threshold} | {row["verdict"]} | {str(row["gating"]).lower()} |')
    lines += ['', '| 量 | 基準値 | 現在 | 差% |', '|---|---:|---:|---:|']
    for row in result['amounts']:
        delta = f'{row["delta_percent"]:+.2f}' if row['delta_percent'] is not None else '定義不能'
        old = f'{row["baseline"]:.4f}' if row['baseline'] is not None else '-'
        new = f'{row["current"]:.4f}' if row['current'] is not None else '-'
        lines.append(f'| {row["name"]} | {old} | {new} | {delta} |')
    lines += ['', '警告:']
    lines += [f'- {item}' for item in result['warnings']] or ['- なし']
    return '\n'.join(lines)


def _read(path: pathlib.Path) -> object:
    with path.open(encoding='utf-8') as source:
        return json.load(source)


def main(argv: list[str] | None = None) -> int:
    sys.stdout.reconfigure(encoding='utf-8')
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('current', type=pathlib.Path)
    parser.add_argument('--baseline', type=pathlib.Path)
    parser.add_argument('--update-baseline', action='store_true')
    parser.add_argument('--yes', action='store_true')
    parser.add_argument('--json-out', type=pathlib.Path)
    parser.add_argument('--floor-percent', type=float, default=2.0)
    parser.add_argument('--max-threshold-percent', type=float, default=10.0)
    parser.add_argument('--calib-percent', type=float, default=5.0)
    args = parser.parse_args(argv)
    if any(not math.isfinite(value) or value < 0 for value in
           (args.floor_percent, args.max_threshold_percent, args.calib_percent)):
        parser.error('閾値は 0 以上の有限値にしてください')
    try:
        current = _read(args.current)
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        result = {'verdict': 'INVALID', 'exit_code': 5, 'differences': [], 'metrics': [],
                  'amounts': [], 'warnings': [f'現在の結果を読めません: {error}']}
        current = None
    else:
        if args.baseline is None:
            machine = current.get('machine', {}) if isinstance(current, dict) else {}
            conditions = current.get('conditions', {}) if isinstance(current, dict) else {}
            machine_id = machine.get('machine_id') if isinstance(machine, dict) else None
            profile = conditions.get('profile') if isinstance(conditions, dict) else None
            if not isinstance(machine_id, str) or not isinstance(profile, str) or not machine_id or not profile:
                args.baseline = None
            else:
                args.baseline = ROOT / 'docs' / 'perf' / 'verification' / f'perf__{machine_id}__{profile}.json'
        baseline_read_error = None
        try:
            baseline = _read(args.baseline) if args.baseline and args.baseline.exists() else None
        except (OSError, UnicodeError, json.JSONDecodeError) as error:
            baseline, baseline_read_error = None, f'基準値を読めません: {error}'
        # An unreadable or no longer valid baseline (for example one written before a contract
        # change) must not block its own replacement; a plain comparison still reports INVALID.
        replaced_problems = [baseline_read_error] if baseline_read_error else []
        if args.update_baseline and baseline is not None:
            replaced_problems = validate(baseline, '古い基準値', baseline=True,
                                         max_threshold_percent=args.max_threshold_percent,
                                         floor_percent=args.floor_percent)
            if replaced_problems:
                baseline = None
        if baseline_read_error and not args.update_baseline:
            result = {'verdict': 'INVALID', 'exit_code': 5, 'differences': [], 'metrics': [],
                      'amounts': [], 'warnings': [baseline_read_error]}
        else:
            result = compare(current, baseline, args.floor_percent,
                             args.max_threshold_percent, args.calib_percent)
            # The new baseline must pass the baseline-only checks (gating spread) even when no
            # previous baseline existed, otherwise a noisy batch could become the reference.
            baseline_problems = (validate(current, '新しい基準値', baseline=True,
                                          max_threshold_percent=args.max_threshold_percent,
                                          floor_percent=args.floor_percent)
                                 if args.update_baseline and result['verdict'] != 'INVALID' else [])
            if baseline_problems:
                result.update(verdict='INVALID', exit_code=5)
                result['warnings'].extend(baseline_problems)
            if args.update_baseline and result['verdict'] != 'INVALID':
                if not args.yes:
                    result['warnings'].append('書き込みには --yes を付けて再実行してください')
                elif args.baseline is None:
                    result.update(verdict='INVALID', exit_code=5)
                    result['warnings'].append('基準値のパスを決められません')
                else:
                    if result['verdict'] == 'NO_BASELINE':
                        if replaced_problems:
                            result.update(verdict='BASELINE_REPLACED', exit_code=0)
                            result['warnings'] = [problem for problem in result['warnings']
                                                  if problem != '基準値なし']
                            result['warnings'].append('使えない古い基準値を置き換えます:')
                            result['warnings'].extend(f'  {problem}' for problem in replaced_problems)
                        else:
                            result.update(verdict='BASELINE_CREATED', exit_code=0)
                    else:
                        # The requester asked for this replacement explicitly; the comparison
                        # against the old baseline stays in the report but no longer fails the run.
                        result['warnings'].append(f'更新前の比較結果: {result["verdict"]}')
                        result.update(verdict='BASELINE_UPDATED', exit_code=0)
                    updated = dict(current)
                    updated['baseline'] = {'created': datetime.datetime.now(datetime.timezone.utc).isoformat(),
                                           'source': str(args.current)}
                    args.baseline.parent.mkdir(parents=True, exist_ok=True)
                    args.baseline.write_text(json.dumps(updated, indent=2, sort_keys=True,
                                                        ensure_ascii=False) + '\n', encoding='utf-8')
                    result['warnings'].append(f'基準値を更新しました: {args.baseline}')
    print(render(result))
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(result, indent=2, sort_keys=True,
                                            ensure_ascii=False) + '\n', encoding='utf-8')
    return result['exit_code']


if __name__ == '__main__':
    raise SystemExit(main())
