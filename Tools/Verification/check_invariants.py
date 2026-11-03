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
    # Bone length restore runs after collision and there is no continuous collision, so some
    # penetration is normal; ABP_9_Kano reaches 9.1 cm. penetration_reduced proves the limits work.
    'max_penetration_cm': 12.0,
    'max_limit_angle_excess_degrees': 0.5,
}
MIN_REST_LENGTH_CM = 0.01
STILL_CM = 0.001


def _dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def _sub(a, b):
    return tuple(x - y for x, y in zip(a, b))


def _norm(a):
    return math.sqrt(_dot(a, a))


def _closest_point(point, start, end):
    direction = _sub(end, start)
    length2 = _dot(direction, direction)
    t = max(0.0, min(1.0, _dot(_sub(point, start), direction) / length2)) if length2 else 0.0
    return tuple(x + t * (y - x) for x, y in zip(start, end))


def _tapered_distance(limit, point):
    """Signed distance to a tapered capsule, matching the toolset geometry."""
    start, end = limit['start'], limit['end']
    r0, r1 = limit['radius0'], limit['radius1']
    axis = _sub(end, start)
    length2 = _dot(axis, axis)
    if length2 == 0:
        return _norm(_sub(point, start)) - max(r0, r1)
    radius_delta = r0 - r1
    cone2 = length2 - radius_delta * radius_delta
    if cone2 <= 0:
        return min(_norm(_sub(point, start)) - r0,
                   _norm(_sub(point, end)) - r1)
    inverse_length2 = 1.0 / length2
    offset = _sub(point, start)
    y = _dot(offset, axis)
    z = y - length2
    perpendicular = tuple(p * length2 - direction * y
                          for p, direction in zip(offset, axis))
    x2 = _dot(perpendicular, perpendicular)
    k = (math.copysign(1.0, radius_delta) * radius_delta * radius_delta * x2
         if radius_delta != 0 else 0.0)
    if math.copysign(1.0, z) * cone2 * z * z * length2 > k:
        return math.sqrt(x2 + z * z * length2) * inverse_length2 - r1
    if math.copysign(1.0, y) * cone2 * y * y * length2 < k:
        return math.sqrt(x2 + y * y * length2) * inverse_length2 - r0
    return ((math.sqrt(x2 * cone2 * inverse_length2) + y * radius_delta)
            * inverse_length2 - r0)


def _box_distance(limit, point):
    x, y, z, w = limit['rotation']
    length = math.sqrt(x*x + y*y + z*z + w*w)
    x, y, z, w = (v / length for v in (x, y, z, w))
    px, py, pz = _sub(point, limit['location'])
    tx, ty, tz = 2 * (-y*pz + z*py), 2 * (-z*px + x*pz), 2 * (-x*py + y*px)
    local = (px + w*tx - y*tz + z*ty,
             py + w*ty - z*tx + x*tz,
             pz + w*tz - x*ty + y*tx)
    q = [abs(p) - e for p, e in zip(local, limit['extent'])]
    return _norm([max(0.0, v) for v in q]) + min(max(q), 0.0)


def _point_clearance(limit, point, radius):
    """Return clearance from a geometric limit and the bone sphere."""
    kind = limit['limit_type']
    if kind == 'Spherical':
        distance = _norm(_sub(point, limit['location']))
        signed = (limit['radius0'] - distance if limit['inner_sphere']
                  else distance - limit['radius0'])
    elif kind == 'Capsule':
        signed = _norm(_sub(point, _closest_point(point, limit['start'],
                                                  limit['end']))) - limit['radius0']
    elif kind == 'TaperedCapsule':
        signed = _tapered_distance(limit, point)
    elif kind == 'Box':
        signed = _box_distance(limit, point)
    elif kind == 'Planar':
        signed = _dot(_sub(point, limit['location']), limit['plane_normal'])
    else:
        raise ValueError(f'Unknown limit_type: {kind}')
    return signed - radius


_LIMIT_TYPES = {1: 'Spherical', 2: 'Capsule', 3: 'Box', 4: 'Planar',
                5: 'TaperedCapsule'}


def _limits(flat):
    """Decode complete finite version-two limit samples."""
    for offset in range(0, len(flat), 24):
        sample = flat[offset:offset + 24]
        if len(sample) != 24 or any(v is None or not math.isfinite(v) for v in sample):
            continue
        if not sample[1] or sample[0] not in _LIMIT_TYPES:
            continue
        yield {'limit_type': _LIMIT_TYPES[sample[0]], 'inner_sphere': bool(sample[2]),
               'location': sample[3:6], 'rotation': sample[6:10],
               'start': sample[10:13], 'end': sample[13:16],
               'radius0': sample[16], 'radius1': sample[17],
               'extent': sample[18:21], 'plane_normal': sample[21:24]}


def node_penetration(node_record: dict, other_node: dict | None = None) -> tuple[float, dict]:
    """Return the worst penetration, optionally using another node's limits."""
    bones = node_record['bones']
    flat = node_record['bone_frames']
    limits = (other_node or node_record)['limit_frames']
    worst = 0.0
    detail = {}
    for frame in range(min(len(limits), len(flat) // (8 * len(bones)) if bones else 0)):
        for index, bone in enumerate(bones):
            if bone['skip'] or bone['dummy'] != 'None':
                continue
            sample = flat[(frame * len(bones) + index) * 8:
                          (frame * len(bones) + index + 1) * 8]
            if any(v is None or not math.isfinite(v) for v in sample):
                continue
            for limit in _limits(limits[frame]):
                penetration = max(0.0, -_point_clearance(limit, sample[:3], sample[6]))
                if penetration > worst:
                    worst = penetration
                    detail = {'bone': bone['name'],
                              'frame': frame + node_record.get('first_frame', 0),
                              'limit': limit['limit_type']}
    return worst, detail


def node_limit_angle_excess(node_record: dict, other_node: dict | None = None) -> tuple[float, dict]:
    """Return how far bones pass their limit angle, optionally using another node's limits."""
    bones = node_record['bones']
    flat = node_record['bone_frames']
    limit_flat = (other_node or node_record)['bone_frames']
    worst = 0.0
    detail = {}
    frames = min(len(flat), len(limit_flat)) // (8 * len(bones)) if bones else 0
    for frame in range(frames):
        for index, bone in enumerate(bones):
            parent = bone['angle_parent']
            if parent < 0 or parent >= len(bones):
                continue
            sample = flat[(frame * len(bones) + index) * 8:
                          (frame * len(bones) + index + 1) * 8]
            other = flat[(frame * len(bones) + parent) * 8:
                         (frame * len(bones) + parent + 1) * 8]
            limit = limit_flat[(frame * len(bones) + index) * 8 + 7]
            if (any(v is None or not math.isfinite(v) for v in sample + other)
                    or limit is None or limit <= 0):
                continue
            direction = _sub(sample[:3], other[:3])
            pose = _sub(sample[3:6], other[3:6])
            length, pose_length = _norm(direction), _norm(pose)
            if length < 1e-6 or pose_length < 1e-6:
                continue
            angle = math.degrees(math.acos(max(-1.0, min(1.0,
                _dot(direction, pose) / (length * pose_length)))))
            excess = max(0.0, angle - limit)
            if excess > worst:
                worst = excess
                detail = {'bone': bone['name'],
                          'frame': frame + node_record.get('first_frame', 0),
                          'limit': bone['angle_mode']}
    return worst, detail


def _node_metrics(cell: dict, result: dict, limits: dict):
    """Add penetration and angle measurements from optional node records."""
    records = cell.get('node_records')
    if records is None:
        return
    if not records:
        # 記録を頼んだのに 1 件も無いと、貫通量も角度も測れないまま 0 に見えてしまう。
        result['violations'].append('no_node_records')
        return
    result['max_penetration_cm'] = 0.0
    result['max_limit_angle_excess_degrees'] = 0.0
    for node in records:
        penetration, detail = node_penetration(node)
        if penetration > result['max_penetration_cm']:
            result['max_penetration_cm'] = penetration
            result['worst']['penetration'] = detail
        excess, detail = node_limit_angle_excess(node)
        if excess > result['max_limit_angle_excess_degrees']:
            result['max_limit_angle_excess_degrees'] = excess
            result['worst']['limit_angle'] = detail
    if result['max_penetration_cm'] > limits['max_penetration_cm']:
        result['violations'].append('penetrating')
    if result['max_limit_angle_excess_degrees'] > limits['max_limit_angle_excess_degrees']:
        result['violations'].append('limit_angle_exceeded')


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
              'max_penetration_cm': None, 'max_limit_angle_excess_degrees': None,
              'worst': {}, 'violations': []}
    if not frames or not bones:
        result['violations'].append('no_samples' if not frames else 'no_simulated_bones')
        _node_metrics(cell, result, limits)
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
    _node_metrics(cell, result, limits)
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
