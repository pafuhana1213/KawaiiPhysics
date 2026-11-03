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


def main():
    sys.stdout.reconfigure(encoding='utf-8', errors='replace', line_buffering=True)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output_directory')
    parser.add_argument('--runs', type=int, default=10)
    parser.add_argument('--instances', type=int, default=100)
    parser.add_argument('--frames', type=int, default=600)
    parser.add_argument('--warmup-frames', type=int, default=120)
    parser.add_argument('--pinned-mask', type=lambda text: int(text, 0), default=0xFF,
                        help='Affinity mask for kp_on_pinned.')
    parser.add_argument('--timeout', type=int, default=300)
    arguments = parser.parse_args()

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
