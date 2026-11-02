"""Check recorded cells against conditions that must hold for any KawaiiPhysics setup."""

from __future__ import annotations

import argparse
import json
import math
import sys

import compare_cells

# Tuned against the sample levels; a violation means the motion cannot be intended.
THRESHOLDS = {
    'max_stretch_ratio': 1.15,
    'min_stretch_ratio': 0.85,
    'max_step_cm': 100.0,
    'max_reach_cm': 2000.0,
    'quaternion_tolerance': 0.01,
}
MIN_REST_LENGTH_CM = 0.01
STILL_CM = 0.001


def _angle_between(first, second):
    return compare_cells._quaternion_angle(first, second)


def simulated_bones(cell: dict) -> list[int]:
    """Return indices of the real bones driven by the cell's KawaiiPhysics nodes."""
    index_by_name = {bone[0]: index for index, bone in enumerate(cell['bones'])}
    return sorted({index_by_name[name] for node in cell['nodes']
                   for name in node.get('bone_names', []) if name in index_by_name})


def check_cell(cell: dict, thresholds: dict | None = None) -> dict:
    """Measure one cell and list the conditions it breaks."""
    limits = {**THRESHOLDS, **(thresholds or {})}
    bone_count = len(cell['bones'])
    values = cell['transforms']
    frames = len(values) // (7 * bone_count) if bone_count else 0
    bones = simulated_bones(cell)
    result = {'frames': frames, 'simulated_bones': len(bones), 'nonfinite_values': 0,
              'max_stretch_ratio': None, 'min_stretch_ratio': None, 'max_step_cm': 0.0,
              'max_step_degrees': 0.0, 'max_reach_cm': 0.0, 'max_motion_cm': 0.0,
              'worst': {}, 'violations': []}
    if not frames or not bones:
        result['violations'].append('no_samples' if not frames else 'no_simulated_bones')
        return result

    reference = cell['ref_pose']
    simulated = set(bones)

    def transform(frame, bone):
        offset = (frame * bone_count + bone) * 7
        return values[offset:offset + 7]

    def note_worst(name, bone, frame):
        result['worst'][name] = {'bone': cell['bones'][bone][0], 'frame': frame}

    rest_lengths = {}
    for bone in bones:
        parent = cell['bones'][bone][1]
        # ノードのルートはポーズに従うだけなので、親までの距離はアニメ次第で変わってよい。
        if parent in simulated:
            length = math.dist(reference[bone * 7:bone * 7 + 3],
                               reference[parent * 7:parent * 7 + 3])
            if length >= MIN_REST_LENGTH_CM:
                rest_lengths[bone] = (parent, length)

    start = {}
    previous = {}
    for frame in range(frames):
        current = {}
        for bone in bones:
            sample = transform(frame, bone)
            if any(value is None for value in sample):
                result['nonfinite_values'] += 1
                previous.pop(bone, None)
                continue
            current[bone] = sample
            start.setdefault(bone, sample)
            reach = math.dist(sample[:3], start[bone][:3])
            if reach > result['max_reach_cm']:
                result['max_reach_cm'] = reach
                note_worst('reach', bone, frame)
            if bone in previous:
                step = math.dist(sample[:3], previous[bone][:3])
                if step > result['max_step_cm']:
                    result['max_step_cm'] = step
                    note_worst('step', bone, frame)
                angle = _angle_between(sample[3:], previous[bone][3:])
                if angle > result['max_step_degrees']:
                    result['max_step_degrees'] = angle
                    note_worst('step_degrees', bone, frame)
            if bone in rest_lengths:
                parent, length = rest_lengths[bone]
                parent_sample = current.get(parent)
                if parent_sample is None:
                    continue
                ratio = math.dist(sample[:3], parent_sample[:3]) / length
                if result['max_stretch_ratio'] is None or ratio > result['max_stretch_ratio']:
                    result['max_stretch_ratio'] = ratio
                    note_worst('stretch', bone, frame)
                if result['min_stretch_ratio'] is None or ratio < result['min_stretch_ratio']:
                    result['min_stretch_ratio'] = ratio
                    note_worst('compress', bone, frame)
            norm = math.sqrt(sum(value * value for value in sample[3:]))
            if abs(norm - 1.0) > limits['quaternion_tolerance'] and \
                    'bad_quaternion' not in result['violations']:
                result['violations'].append('bad_quaternion')
                note_worst('bad_quaternion', bone, frame)
        previous = current
        for bone, sample in current.items():
            result['max_motion_cm'] = max(result['max_motion_cm'],
                                          math.dist(sample[:3], start[bone][:3]))

    if result['nonfinite_values']:
        result['violations'].append('nonfinite')
    if result['max_stretch_ratio'] is not None and \
            result['max_stretch_ratio'] > limits['max_stretch_ratio']:
        result['violations'].append('stretched')
    if result['min_stretch_ratio'] is not None and \
            result['min_stretch_ratio'] < limits['min_stretch_ratio']:
        result['violations'].append('compressed')
    if result['max_step_cm'] > limits['max_step_cm']:
        result['violations'].append('jump')
    if result['max_reach_cm'] > limits['max_reach_cm']:
        result['violations'].append('exploded')
    result['still'] = result['max_motion_cm'] < STILL_CM
    return result


def check_recording(recording: dict, thresholds: dict | None = None) -> dict:
    """Return per-cell results, keyed by cell id."""
    return {name: {'anim_class': cell.get('anim_class', ''), **check_cell(cell, thresholds)}
            for name, cell in recording['cells'].items()}


def main():
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('recording')
    arguments = parser.parse_args()
    results = check_recording(compare_cells.load_recording(arguments.recording))
    print(json.dumps(results, indent=2, ensure_ascii=False))
    return 1 if any(result['violations'] for result in results.values()) else 0


if __name__ == '__main__':
    sys.exit(main())
