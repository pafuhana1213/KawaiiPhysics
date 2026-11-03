"""Compare recorded cells with the Python standard library.

--pair A B compares two cells in one recording; --other compares each cell of one recording with
the same cell of another, which measures run-to-run determinism.
"""

from __future__ import annotations

import argparse
import json
import math


def load_recording(path) -> dict:
    """Load a version-two cell recording as {'run': header, 'cells': {id: cell}}."""
    with open(path, encoding='utf-8') as source:
        lines = [json.loads(line) for line in source if line.strip()]
    if not lines or lines[0].get('type') != 'run' or lines[0].get('version') != 2:
        raise ValueError(f'{path} needs a version-two run header.')
    return {'run': lines[0], 'cells': {line['id']: line for line in lines[1:]}}


def _percentile(values, percentile):
    ordered = sorted(values)
    index = (len(ordered) - 1) * percentile / 100.0
    low, high = math.floor(index), math.ceil(index)
    return ordered[low] + (ordered[high] - ordered[low]) * (index - low)


def _quaternion_angle(a, b):
    # acos は 1 付近で桁が落ち、同じ回転でも 0.03 度ほど出る。正規化して半角の atan2 で求める。
    a_length = math.sqrt(sum(x * x for x in a))
    b_length = math.sqrt(sum(x * x for x in b))
    sign = 1.0 if sum(x * y for x, y in zip(a, b)) >= 0.0 else -1.0
    apart = sum((x / a_length - sign * y / b_length) ** 2 for x, y in zip(a, b))
    together = sum((x / a_length + sign * y / b_length) ** 2 for x, y in zip(a, b))
    return math.degrees(4.0 * math.atan2(math.sqrt(apart), math.sqrt(together)))


def compare_cells(first: dict, second: dict, skip_frames: int = 0) -> dict:
    """Return location and rotation differences between two cells with the same skeleton."""
    if [bone[0] for bone in first['bones']] != [bone[0] for bone in second['bones']]:
        raise ValueError('Cells need the same skeleton.')
    first_values, second_values = first['transforms'], second['transforms']
    if len(first_values) != len(second_values):
        raise ValueError('Cells need the same frame count.')
    names = [bone[0] for bone in first['bones']]
    bone_count = len(names)
    frames = len(first_values) // (7 * bone_count)
    distances, angles = [], []
    worst = {'distance': -1.0, 'bone': None, 'frame': None}
    for frame in range(skip_frames, frames):
        for bone in range(bone_count):
            offset = (frame * bone_count + bone) * 7
            distance = math.dist(first_values[offset:offset + 3], second_values[offset:offset + 3])
            distances.append(distance)
            angles.append(_quaternion_angle(first_values[offset + 3:offset + 7],
                                            second_values[offset + 3:offset + 7]))
            if distance > worst['distance']:
                worst = {'distance': distance, 'bone': names[bone], 'frame': frame}
    return {'max_cm': max(distances), 'p95_cm': _percentile(distances, 95),
            'max_degrees': max(angles), 'p95_degrees': _percentile(angles, 95),
            'worst': worst, 'identical': first_values == second_values}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('recording')
    parser.add_argument('--other')
    parser.add_argument('--pair', nargs=2, action='append', default=[], metavar=('A', 'B'))
    parser.add_argument('--skip-frames', type=int, default=0)
    arguments = parser.parse_args()
    recording = load_recording(arguments.recording)
    results = {}
    for left, right in arguments.pair:
        results[f'{left} vs {right}'] = compare_cells(
            recording['cells'][left], recording['cells'][right], arguments.skip_frames)
    if arguments.other:
        other = load_recording(arguments.other)
        for name, cell in recording['cells'].items():
            results[f'{name} across runs'] = compare_cells(
                cell, other['cells'][name], arguments.skip_frames)
    print(json.dumps(results, indent=2))


if __name__ == '__main__':
    main()
