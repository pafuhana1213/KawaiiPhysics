"""Measure run-to-run load noise of a heavy KawaiiPhysics scene on the current machine.

Conditions run in turn so that thermal drift hits each of them equally. The off conditions skip
node evaluation, so on minus off is the KawaiiPhysics cost. serial keeps evaluation on the game
thread, which makes wall time follow total CPU time; pinned also fixes the cores in use. Run it
with no other heavy work on the machine.
"""

from __future__ import annotations

import argparse
import ctypes
import json
import math
import os
import pathlib
import platform
import random
import statistics
import sys
from ctypes import wintypes

import compare_perf
import run_cells

MESH = '/Game/KawaiiPhysicsSample/Model/TAStyleKano/Mesh/test_kano1.test_kano1'
ANIM_CLASS = '/Game/KawaiiPhysicsSample/Examples/09_Showcase/ABP_9_Kano.ABP_9_Kano_C'
GRID_SPACING = 400


class _SystemPowerStatus(ctypes.Structure):
    _fields_ = [('ACLineStatus', wintypes.BYTE), ('BatteryFlag', wintypes.BYTE),
                ('BatteryLifePercent', wintypes.BYTE), ('SystemStatusFlag', wintypes.BYTE),
                ('BatteryLifeTime', wintypes.DWORD), ('BatteryFullLifeTime', wintypes.DWORD)]


def on_ac_power() -> bool:
    status = _SystemPowerStatus()
    if not ctypes.windll.kernel32.GetSystemPowerStatus(ctypes.byref(status)):
        raise ctypes.WinError()
    return status.ACLineStatus == 1


def make_manifest(instances: int, frames: int) -> dict:
    columns = math.ceil(math.sqrt(instances))
    return {'frames': frames, 'record': False, 'cells': [
        {'id': f'kano_{index:03d}', 'mesh': MESH, 'anim_class': ANIM_CLASS,
         'location': [(index % columns) * GRID_SPACING, (index // columns) * GRID_SPACING, 200],
         'motion': {'linear_amplitude': [60, 30, 0], 'linear_period': 1.3 + (index % 7) * 0.1,
                    'yaw_amplitude_degrees': 30, 'yaw_period': 1.7 + (index % 5) * 0.1}}
        for index in range(instances)]}


def summarize_run(path, warmup_frames: int) -> dict:
    with open(path, encoding='utf-8') as source:
        header = json.loads(source.readline())
    frames = sorted(header['frame_ms'][warmup_frames:])
    index = (len(frames) - 1) * 0.95
    return {'ok': header['ok'], 'median_ms': statistics.median(frames),
            'mean_ms': statistics.fmean(frames),
            'p95_ms': frames[round(index)],
            'calib_start_ms': min(header['calib_start_ms']),
            'calib_end_ms': min(header['calib_end_ms'])}


def describe(values) -> dict:
    mean = statistics.fmean(values)
    deviation = statistics.stdev(values) if len(values) > 1 else 0.0
    return {'mean': mean, 'min': min(values), 'max': max(values), 'stdev': deviation,
            'cv_percent': deviation / mean * 100 if mean else 0.0}


def summarize_cost(on_rows, off_rows, batch_size: int = 5, resamples: int = 2000) -> dict:
    """Describe the KawaiiPhysics cost (on minus off), paired by run to cancel thermal drift."""
    costs = [on['median_ms'] - off['median_ms'] for on, off in zip(on_rows, off_rows)]
    result = describe(costs)
    half = len(costs) // 2
    first, second = costs[:half], costs[half:]
    result['aa_difference_percent'] = (abs(statistics.median(first) - statistics.median(second)) /
                                       abs(result['mean']) * 100) if half else None
    # batch_size 回の中央値どうしを比べたときの差の広がりから、誤検知しにくい最小の変化量を見積もる。
    generator = random.Random(0)
    gaps = [statistics.median(generator.choices(costs, k=batch_size)) -
            statistics.median(generator.choices(costs, k=batch_size))
            for _ in range(resamples)]
    result['detectable_change_percent'] = (statistics.stdev(gaps) * 3 / abs(result['mean']) * 100
                                           if result['mean'] else None)
    return result


def summarize_results(paths: list[pathlib.Path]) -> dict:
    """Measure CSV metric noise and independent batch A/A comparisons."""
    documents = []
    resolved = set()
    identities = set()
    for path in paths:
        canonical = path.resolve()
        if canonical in resolved:
            raise ValueError(f'重複した入力パス: {path}')
        resolved.add(canonical)
        with path.open(encoding='utf-8') as source:
            document = json.load(source)
        problems = compare_perf.validate(document, str(path))
        if problems:
            raise ValueError('; '.join(problems))
        if document['positive_control'] is not None:
            raise ValueError(f'{path}: 陽性対照は A/A 集計に使えません')
        identity = (document['created'], json.dumps(document['runs'], sort_keys=True))
        if identity in identities:
            raise ValueError(f'{path}: created と runs が重複しています')
        identities.add(identity)
        if documents:
            first = documents[0]
            for section, fields in (('machine', ('machine_id', 'logical_cpus', 'power_plan')),
                                    ('engine', ('version', 'build_config'))):
                for field in fields:
                    if document[section][field] != first[section][field]:
                        raise ValueError(f'{path}: {section}.{field} が一致しません')
            if document['condition_key'] != first['condition_key']:
                raise ValueError(f'{path}: condition_key が一致しません')
            for field in ('commit', 'plugin_diff_sha256'):
                if document['provenance'][field] != first['provenance'][field]:
                    raise ValueError(f'{path}: provenance.{field} が一致しません')
        documents.append(document)
    if len(documents) < 2:
        raise ValueError('--from-results には 2 ファイル以上が必要です')
    # "Within 1%" is not transitive, so every pair that the A/A analysis compares must itself be
    # comparable: total amounts, and each fixture's audit amounts (a small fixture's change can
    # hide inside the 1% total), checked the same way the regression comparison does.
    for first_index in range(len(documents)):
        for second_index in range(first_index + 1, len(documents)):
            first, document = documents[first_index], documents[second_index]
            pair = f'{paths[first_index]} と {paths[second_index]}'
            old_amounts = first['summary']['amounts']
            new_amounts = document['summary']['amounts']
            for name in sorted(old_amounts.keys() | new_amounts.keys()):
                if compare_perf._amount_changed(old_amounts.get(name), new_amounts.get(name)):
                    raise ValueError(f'{pair}: summary.amounts.{name} が 1% 以内で一致しません'
                                     f'（{old_amounts.get(name)} / {new_amounts.get(name)}）')
            fixture_differences = compare_perf.fixture_workload_differences(first, document)
            if fixture_differences:
                raise ValueError(f'{pair}: fixture の量が違います: ' + '; '.join(fixture_differences))
    shared = set.intersection(*(set(doc['summary']['metrics']) for doc in documents))
    report = {'schema': 'kpv-perf-noise/1', 'files': [str(path) for path in paths], 'metrics': {}}
    for name in sorted(shared):
        entries = [doc['summary']['metrics'][name] for doc in documents]
        values = [value for entry in entries for value in entry['values']]
        mean = statistics.fmean(values)
        cv = statistics.stdev(values) / mean * 100 if mean and len(values) > 1 else 0.0
        three_sigma = []
        for path, doc, entry in zip(paths, documents, entries):
            n = doc['conditions']['runs_per_state']
            sigma = compare_perf.bootstrap_sigma(entry['values'], batch_size=n)
            three_sigma.append({'file': str(path), 'runs_per_state': n,
                                'percent': 3 * sigma / entry['median'] * 100
                                if entry['median'] > 0 else None})
        pairs = []
        for first in range(len(documents)):
            for second in range(first + 1, len(documents)):
                baseline = entries[first]
                current = entries[second]
                comparable = baseline['median'] > 0
                delta = ((current['median'] / baseline['median'] - 1) * 100
                         if comparable else None)
                threshold = (compare_perf.threshold_percent(baseline['values'],
                                                             baseline['median'], 2.0)
                             if comparable else None)
                pairs.append({'baseline': str(paths[first]), 'current': str(paths[second]),
                              'delta_percent': delta, 'threshold_percent': threshold,
                              'exceeds_threshold': abs(delta) > threshold if comparable else None})
        report['metrics'][name] = {'cv_percent': cv,
                                    'three_sigma_percent_by_file': three_sigma,
                                    'aa_pairs': pairs,
                                    'aa_false_positives': sum(pair['exceeds_threshold'] is True
                                                              for pair in pairs)}
    return report


def main():
    sys.stdout.reconfigure(encoding='utf-8', errors='replace', line_buffering=True)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output_directory', nargs='?')
    parser.add_argument('--from-results', nargs='+', type=pathlib.Path, metavar='RESULT')
    parser.add_argument('--out', type=pathlib.Path)
    parser.add_argument('--runs', type=int, default=10)
    parser.add_argument('--instances', type=int, default=100)
    parser.add_argument('--frames', type=int, default=600)
    parser.add_argument('--warmup-frames', type=int, default=120)
    parser.add_argument('--pinned-mask', type=lambda text: int(text, 0), default=0xFF,
                        help='Affinity mask for kp_on_pinned.')
    parser.add_argument('--timeout', type=int, default=300)
    arguments = parser.parse_args()

    if arguments.from_results:
        if arguments.output_directory:
            parser.error('output_directory と --from-results は同時に指定できません')
        try:
            report = summarize_results(arguments.from_results)
        except (OSError, UnicodeError, json.JSONDecodeError, ValueError) as error:
            parser.error(str(error))
        output = json.dumps(report, indent=2, sort_keys=True, ensure_ascii=False) + '\n'
        if arguments.out:
            arguments.out.parent.mkdir(parents=True, exist_ok=True)
            arguments.out.write_text(output, encoding='utf-8')
        print(output, end='')
        return
    if arguments.output_directory is None:
        parser.error('output_directory または --from-results が必要です')
    if arguments.out:
        parser.error('--out は --from-results と一緒に指定してください')

    directory = pathlib.Path(arguments.output_directory).resolve()
    directory.mkdir(parents=True, exist_ok=True)
    manifest = directory / 'manifest.json'
    manifest.write_text(json.dumps(make_manifest(arguments.instances, arguments.frames)),
                        encoding='utf-8')

    conditions = {}
    for mode, serial_cvars in (('parallel', []),
                               ('serial', ['a.ParallelAnimEvaluation=0', 'a.ParallelAnimUpdate=0'])):
        for state, state_cvars in (('on', []), ('off', ['a.AnimNode.KawaiiPhysics.Enable=0'])):
            cvars = serial_cvars + state_cvars
            conditions[f'{mode}_{state}'] = {
                'extra_arguments': [f"-dpcvars={','.join(cvars)}"] if cvars else []}
    for state in ('on', 'off'):
        conditions[f'pinned_{state}'] = {**conditions[f'serial_{state}'],
                                         'affinity_mask': arguments.pinned_mask}
    rows = []
    for run_index in range(arguments.runs):
        for name, options in conditions.items():
            if not on_ac_power():
                raise SystemExit('Stopped: the machine left AC power.')
            output = directory / f'{name}_{run_index:02d}.jsonl'
            outcome = run_cells.run(manifest, output, timeout=arguments.timeout, **options)
            if outcome['exit_code'] != 0 or not outcome['recorded']:
                raise SystemExit(f'{name} run {run_index} failed: {outcome}')
            row = {'condition': name, 'run': run_index, 'seconds': outcome['seconds'],
                   'severe_log': len(outcome['severe_log']),
                   **summarize_run(output, arguments.warmup_frames)}
            rows.append(row)
            print(f"{name} #{run_index}: median={row['median_ms']:.3f}ms "
                  f"p95={row['p95_ms']:.3f}ms calib={row['calib_start_ms']:.2f}/"
                  f"{row['calib_end_ms']:.2f}ms severe={row['severe_log']} "
                  f"({row['seconds']:.0f}s)")

    report = {'machine': {'processor': platform.processor(), 'logical_cpus': os.cpu_count()},
              'settings': vars(arguments) | {'pinned_mask': hex(arguments.pinned_mask)},
              'conditions': {}, 'runs': rows}
    for name in conditions:
        selected = [row for row in rows if row['condition'] == name]
        report['conditions'][name] = {
            field: describe([row[field] for row in selected])
            for field in ('median_ms', 'p95_ms', 'calib_start_ms', 'calib_end_ms')}
    report['kawaii_cost'] = {
        mode: summarize_cost([row for row in rows if row['condition'] == f'{mode}_on'],
                             [row for row in rows if row['condition'] == f'{mode}_off'])
        for mode in ('parallel', 'serial', 'pinned')}
    (directory / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps({'conditions': report['conditions'], 'kawaii_cost': report['kawaii_cost']},
                     indent=2))


if __name__ == '__main__':
    main()
