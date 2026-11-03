"""Judge recorded matrix cells and relations without launching Unreal."""

from __future__ import annotations

import fnmatch
import math
import re
import statistics
from collections import Counter

import check_invariants
import compare_cells

BONE_NAME = re.compile(r'BoneName="?([^"),]+)')
# Two motionless cells always agree, so an equal relation needs at least this much motion.
MIN_MOTION_CM = 0.1


def _profile_violations(profile: dict, run: dict) -> list[str]:
    expected = profile.get('settings', {})
    actual = run.get('settings', {})
    if any(actual.get(key) != value for key, value in expected.items()):
        return ['profile_not_applied']
    for cvar in profile.get('cvars', []):
        key, separator, value = cvar.partition('=')
        field = {'a.ParallelAnimEvaluation': 'parallel_anim_evaluation',
                 'a.ParallelAnimUpdate': 'parallel_anim_update'}.get(key)
        if separator and field and actual.get(field) != int(value):
            return ['profile_not_applied']
    delta = run.get('delta_seconds', [])
    fps = profile.get('fps', 60)
    expected_delta = profile.get('settings', {}).get('time_dilation', 1) / fps
    finite = [value for value in delta if value is not None and math.isfinite(value)]
    if not finite or abs(statistics.median(finite) - expected_delta) >= expected_delta * 0.05:
        return ['profile_not_applied']
    return []


def _override_violations(cell: dict, requested: dict) -> list[str]:
    overrides = dict(requested.get('overrides', {}))
    # 記録は終了時の値なので、途中のイベントで変えた項目は最後のイベントの値と比べる。
    for event in sorted(requested.get('events', []), key=lambda item: item.get('frame', 0)):
        overrides.update(event.get('overrides', {}))
    space = overrides.get('SimulationSpace')
    base = overrides.get('SimulationBaseBone')
    if space is None and base is None:
        return []
    if base is not None:
        # 上書きは FBoneReference の文字列で指定し、記録にはボーン名だけが残る。
        match = BONE_NAME.search(str(base))
        base = match.group(1) if match else base
    if not cell.get('nodes'):
        return ['override_not_applied']
    for node in cell.get('nodes', []):
        if ((space is not None and not str(node.get('space', '')).endswith(str(space)))
                or (base is not None and node.get('base') != base)):
            return ['override_not_applied']
    return []


def _penetration_reduction(first: dict, second: dict) -> dict:
    a_nodes = {node['root']: node for node in first.get('node_records', [])}
    b_nodes = {node['root']: node for node in second.get('node_records', [])}
    if not a_nodes or not b_nodes or a_nodes.keys() != b_nodes.keys():
        raise ValueError('Matching node records are required for penetration reduction.')
    pa = max(check_invariants.node_penetration(a_nodes[root], b_nodes[root])[0]
             for root in a_nodes)
    pb = max(check_invariants.node_penetration(node)[0] for node in b_nodes.values())
    return {'penetration_a_cm': pa, 'penetration_b_cm': pb,
            'reduction_cm': pa - pb}


def _resampled(cell: dict, stride: int, offset: int, frames: int) -> dict:
    """Return the cell at every stride-th frame, so runs at different rates line up in time."""
    bone_count = len(cell['bones'])
    size = bone_count * 7
    picked = []
    for index in range(frames):
        start = (offset + index * stride) * size
        picked.extend(cell['transforms'][start:start + size])
    return {**cell, 'transforms': picked}


def _aligned_cells(first: dict, second: dict, relation: dict) -> tuple[dict, dict]:
    """Apply a relation's a_stride and a_offset, b_stride and b_offset to its two cells."""
    a_stride, a_offset = relation.get('a_stride', 1), relation.get('a_offset', 0)
    b_stride, b_offset = relation.get('b_stride', 1), relation.get('b_offset', 0)
    if a_stride == 1 and b_stride == 1 and not a_offset and not b_offset:
        return first, second
    size = len(first['bones']) * 7
    a_frames = (len(first['transforms']) // size - a_offset + a_stride - 1) // a_stride
    b_frames = (len(second['transforms']) // size - b_offset + b_stride - 1) // b_stride
    frames = min(a_frames, b_frames)
    return (_resampled(first, a_stride, a_offset, frames),
            _resampled(second, b_stride, b_offset, frames))


def _limit_angle_reduction(first: dict, second: dict) -> dict:
    a_nodes = {node['root']: node for node in first.get('node_records', [])}
    b_nodes = {node['root']: node for node in second.get('node_records', [])}
    if not a_nodes or not b_nodes or a_nodes.keys() != b_nodes.keys():
        raise ValueError('Matching node records are required for limit angle reduction.')
    excess_a = max(check_invariants.node_limit_angle_excess(a_nodes[root], b_nodes[root])[0]
                   for root in a_nodes)
    excess_b = max(check_invariants.node_limit_angle_excess(node)[0]
                   for node in b_nodes.values())
    return {'excess_a_degrees': excess_a, 'excess_b_degrees': excess_b,
            'reduction_degrees': excess_a - excess_b}


def judge(expanded: dict, recordings: dict, thresholds: dict | None = None) -> dict:
    """Return absolute, paired, and profile checks for supplied recordings."""
    requested = {cell['id']: cell for cell in expanded['cells']}
    result = {'cells': {}, 'relations': [], 'run_violations': []}
    for profile_name, recording in recordings.items():
        result['cells'][profile_name] = {}
        profile = expanded.get('profiles', {}).get(profile_name, {})
        for violation in _profile_violations(profile, recording['run']):
            result['run_violations'].append({'profile': profile_name,
                                             'violation': violation})
        limits = {**(thresholds or {}), **profile.get('thresholds', {})}
        for cell_id, cell in recording['cells'].items():
            if cell_id not in requested:
                continue
            checked = check_invariants.check_cell(cell, limits)
            if checked.get('max_motion_cm', 0) < check_invariants.STILL_CM and \
                    not expanded['tags'].get(cell_id, {}).get('allow_still'):
                checked['violations'].append('still')
            checked['violations'].extend(_override_violations(cell, requested[cell_id]))
            result['cells'][profile_name][cell_id] = checked
        for cell_id in requested:
            if cell_id not in result['cells'][profile_name]:
                result['run_violations'].append({'profile': profile_name,
                                                 'cell': cell_id,
                                                 'violation': 'cell_missing'})
    for relation in expanded['relations']:
        kind = relation['kind']
        a_profile = relation.get('a_profile', relation['profile'])
        b_profile = relation.get('b_profile', relation['profile'])
        a, b = relation['a'], relation['b']
        checked = {'id': relation['id'], 'kind': kind, 'a': a, 'b': b,
                   'profile': relation['profile'], 'ok': False, 'failure': None,
                   'metrics': {}, 'reason': relation['reason']}
        if a is None or b is None:
            checked['failure'] = 'relation_empty'
        elif (a not in recordings.get(a_profile, {}).get('cells', {}) or
              b not in recordings.get(b_profile, {}).get('cells', {})):
            checked['failure'] = 'cell_missing'
        else:
            first = recordings[a_profile]['cells'][a]
            second = recordings[b_profile]['cells'][b]
            try:
                if kind == 'penetration_reduced':
                    metrics = _penetration_reduction(first, second)
                    ok = metrics['reduction_cm'] >= relation['min_reduction_cm']
                    failure = 'not_sensitive'
                elif kind == 'limit_angle_reduced':
                    metrics = _limit_angle_reduction(first, second)
                    ok = metrics['reduction_degrees'] >= relation['min_reduction_degrees']
                    failure = 'not_sensitive'
                else:
                    aligned_first, aligned_second = _aligned_cells(first, second, relation)
                    metrics = compare_cells.compare_cells(aligned_first, aligned_second,
                                                          relation.get('skip_frames', 0))
                    if kind in ('equal', 'profile_equal'):
                        ok = (metrics['max_cm'] <= relation['max_cm'] and
                              metrics['max_degrees'] <= relation.get('max_degrees', float('inf')))
                        failure = 'not_equal'
                        if ok and result['cells'].get(a_profile, {}).get(a, {}).get(
                                'max_motion_cm', 0) < relation.get('min_motion_cm', MIN_MOTION_CM):
                            ok, failure = False, 'vacuous'
                    else:
                        # ダミーのように回転だけを変える機能があるので、どちらかが閾値を超えれば反応したとみなす。
                        ok = (metrics['max_cm'] >= relation.get('min_cm', float('inf')) or
                              metrics['max_degrees'] >= relation.get('min_degrees', float('inf')))
                        failure = 'not_sensitive'
                checked['metrics'] = metrics
                checked['failure'] = None if ok else failure
                checked['ok'] = ok
            except (ValueError, KeyError, ZeroDivisionError) as error:
                checked['failure'] = 'cell_missing'
                checked['metrics'] = {'error': str(error)}
        result['relations'].append(checked)
    return result


def apply_known_issues(judged: dict, entries: list[dict]) -> list[dict]:
    """Move matched check failures to known and return unmatched check entries."""
    used = set()
    for profile, cells in judged['cells'].items():
        for cell_id, cell in cells.items():
            cell['known_violations'] = []
            for violation in list(cell['violations']):
                matches = [index for index, entry in enumerate(entries)
                           if entry.get('check', '').startswith('cell:') and
                           fnmatch.fnmatchcase(f'{cell_id}:{violation}', entry['check'][5:])]
                if matches:
                    used.update(matches)
                    cell['violations'].remove(violation)
                    cell['known_violations'].append(violation)
    for relation in judged['relations']:
        relation['known'] = False
        if relation['ok']:
            continue
        matches = [index for index, entry in enumerate(entries)
                   if entry.get('check', '').startswith('relation:') and
                   fnmatch.fnmatchcase(relation['id'], entry['check'][9:])]
        if matches:
            used.update(matches)
            relation['known'] = True
            relation['ok'] = True
            relation['known_failure'] = relation['failure']
            relation['failure'] = None
    return [entry for index, entry in enumerate(entries)
            if 'check' in entry and index not in used]


def axis_report(judged: dict, tags: dict) -> dict:
    """Summarize failure counts and metrics for each axis value."""
    report = {}
    for tag in tags.values():
        for axis, value in tag.items():
            if axis == 'allow_still':
                continue
            report.setdefault(axis, {}).setdefault(value, {
                'cells': 0, 'failed_cells': 0, 'failed_relations': 0,
                'violations': {}, 'max_stretch_ratio': None,
                'max_step_degrees': None, 'max_penetration_cm': None})
    for cells in judged['cells'].values():
        for cell_id, cell in cells.items():
            for axis, value in tags.get(cell_id, {}).items():
                if axis not in report or value not in report[axis]:
                    continue
                item = report[axis][value]
                item['cells'] += 1
                item['failed_cells'] += bool(cell['violations'])
                counts = Counter(cell['violations'])
                for violation, count in counts.items():
                    item['violations'][violation] = item['violations'].get(violation, 0) + count
                for metric in ('max_stretch_ratio', 'max_step_degrees',
                               'max_penetration_cm'):
                    measured = cell.get(metric)
                    if measured is not None:
                        item[metric] = max(item[metric] or 0, measured)
    for relation in judged['relations']:
        if relation['ok']:
            continue
        touched = {(axis, value) for cell_id in (relation['a'], relation['b'])
                   for axis, value in tags.get(cell_id, {}).items()}
        for axis, value in touched:
            if axis in report and value in report[axis]:
                report[axis][value]['failed_relations'] += 1
    for values in report.values():
        for name, item in values.items():
            others = [other['failed_cells'] / other['cells'] for key, other in values.items()
                      if key != name and other['cells']]
            rate = item['failed_cells'] / item['cells'] if item['cells'] else 0
            item['biased'] = bool(others and item['cells'] and
                                  rate - statistics.mean(others) >= 0.2)
    return report
