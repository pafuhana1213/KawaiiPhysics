"""Analyze KawaiiPhysics trajectory recordings with the Python standard library."""

from __future__ import annotations

import argparse
import json
import math
from collections import defaultdict


def load_recording(path) -> dict:
    """Load and validate a version-one JSONL trajectory recording."""
    with open(path, encoding='utf-8') as source:
        lines = [json.loads(line) for line in source if line.strip()]
    if (not lines or not isinstance(lines[0], dict) or
            lines[0].get('type') != 'header' or lines[0].get('version') != 1 or
            not isinstance(lines[0].get('nodes'), list)):
        raise ValueError('Recording needs a version-one header.')
    if any(not isinstance(line, dict) or line.get('type') != 'frame'
           for line in lines[1:]):
        raise ValueError('Recording contains a non-frame line after its header.')
    if any(len(line.get('nodes', [])) != len(lines[0].get('nodes', [])) for line in lines[1:]):
        raise ValueError('Frame node count does not match the header.')
    return {'header': lines[0], 'frames': lines[1:]}


def _percentile(values, percentile):
    if not values:
        return None
    ordered = sorted(values)
    index = (len(ordered) - 1) * percentile / 100.0
    low, high = math.floor(index), math.ceil(index)
    return ordered[low] + (ordered[high] - ordered[low]) * (index - low)


def _mean(values):
    return sum(values) / len(values) if values else None


def _rms(values):
    return math.sqrt(sum(value * value for value in values) / len(values)) if values else None


def _distance(a, b):
    return math.dist(a[:3], b[:3])


def _angle(a, b):
    dot = sum(x * y for x, y in zip(a[3:7], b[3:7]))
    norm = math.sqrt(sum(x * x for x in a[3:7]) *
                     sum(x * x for x in b[3:7]))
    if not norm:
        return None
    return math.degrees(2 * math.acos(min(1.0, abs(dot / norm))))


def _pearson(a, b):
    if len(a) < 3:
        return None
    ma, mb = _mean(a), _mean(b)
    numerator = sum((x - ma) * (y - mb) for x, y in zip(a, b))
    da = sum((x - ma) ** 2 for x in a)
    db = sum((y - mb) ** 2 for y in b)
    return numerator / math.sqrt(da * db) if da and db else None


def _empty_accumulator():
    return {'deviation': [], 'lift': [], 'jitter': [], 'acceleration': [],
            'lift_frames': [], 'flare_frames': [], 'rise_frames': [],
            'angles': [], 'spikes': 0, 'flips': 0,
            'worst_flip': None, 'horizontal': [], 'vertical': [],
            'constraint': [], 'collapse_count': 0, 'collapse_samples': 0}


def _summary(data, rate):
    jitter = data['jitter']
    worst = max(jitter, key=lambda item: item[1])[0] if jitter else None
    jitter_mean = _mean([value for _, value in jitter])
    acceleration_mean = _mean([value for _, value in data['acceleration']])
    def stretch(values):
        return {'mean_abs': _mean([abs(value) for value in values]),
                'p95': _percentile([abs(value) for value in values], 95),
                'max': max(values) if values else None,
                'min': min(values) if values else None}
    return {'deviation': {'mean': _mean(data['deviation']),
                          'p95': _percentile(data['deviation'], 95),
                          'max': max(data['deviation']) if data['deviation'] else None},
            'lift': {'max': max(data['lift_frames']) if data['lift_frames'] else None,
                     'p50': _percentile(data['lift_frames'], 50),
                     'p90': _percentile(data['lift_frames'], 90)},
            'flare': {'p10': _percentile(data['flare_frames'], 10),
                      'p50': _percentile(data['flare_frames'], 50),
                      'p90': _percentile(data['flare_frames'], 90),
                      'max': max(data['flare_frames']) if data['flare_frames'] else None},
            'rise': {'p50': _percentile(data['rise_frames'], 50),
                     'p90': _percentile(data['rise_frames'], 90)},
            'jitter': {'mean': jitter_mean, 'worst_bone': worst,
                       'per_second3': jitter_mean * rate ** 3 if jitter_mean is not None and rate else None},
            'acceleration': {'mean': acceleration_mean,
                             'per_second2': acceleration_mean * rate ** 2
                             if acceleration_mean is not None and rate else None},
            'angular': {'p99': _percentile(data['angles'], 99),
                        'max': max(data['angles']) if data['angles'] else None,
                        'spikes': data['spikes'], 'flips': data['flips'],
                        'worst_flip': data['worst_flip']},
            'stretch': {'horizontal': stretch(data['horizontal']),
                        'vertical': stretch(data['vertical']),
                        'constraint': stretch(data['constraint'])},
            'collapse_fraction': (data['collapse_count'] / data['collapse_samples']
                                  if data['collapse_samples'] else None)}


def _add(accumulators, depth, field, value):
    accumulators['overall'][field].append(value)
    if depth >= 0:
        accumulators['rows'][depth][field].append(value)


def _speed_series(frames, get_position):
    speeds = {}
    previous = None
    for frame in frames:
        if frame.get('stale'):
            previous = None
            continue
        position = get_position(frame)
        number = frame['frame']
        if position is not None and previous is not None and number == previous[0] + 1:
            speeds[number] = _distance(position, previous[1])
        previous = (number, position) if position is not None else None
    return speeds


def _follow(frames, tips, extra_bones):
    tip_speeds = [_speed_series(frames, lambda frame, key=key:
                  frame['bones'].get(key) if frame['bones'] is not None else None)
                  for key in tips]
    skirt = {number: _mean([series[number] for series in tip_speeds
                            if number in series])
             for number in set().union(*(set(series) for series in tip_speeds))}
    result = {}
    for name in extra_bones:
        leg = _speed_series(frames, lambda frame, key=name: frame['extra'].get(key))
        best_lag, best_correlation = None, None
        for lag in range(16):
            pairs = [(speed, skirt[number + lag]) for number, speed in leg.items()
                     if number + lag in skirt]
            correlation = _pearson([pair[0] for pair in pairs],
                                   [pair[1] for pair in pairs])
            if correlation is not None and (best_correlation is None or
                                            correlation > best_correlation):
                best_lag, best_correlation = lag, correlation
        result[name] = {'best_lag': best_lag, 'correlation': best_correlation}
    return result


def compute_motion_metrics(recording: dict, spike_degrees: float = 25.0,
                           collapse_ratio: float = 0.3,
                           flip_degrees: float = 90.0) -> dict:
    """Measure valid motion, rest spacing, collapse, flips, and optional follow lag."""
    for name, value in (('spike_degrees', spike_degrees),
                        ('collapse_ratio', collapse_ratio),
                        ('flip_degrees', flip_degrees)):
        if not isinstance(value, (int, float)) or not math.isfinite(value) or value < 0:
            raise ValueError(f'{name} must be finite and nonnegative.')
    header, frames = recording['header'], recording['frames']
    rate = header.get('fixed_frame_rate')
    if rate is not None and (not isinstance(rate, (int, float)) or
                             not math.isfinite(rate) or rate <= 0):
        raise ValueError('fixed_frame_rate must be positive or null.')
    combined = {'overall': _empty_accumulator(),
                'rows': defaultdict(_empty_accumulator)}
    valid_frames = [frame for frame in frames if not frame.get('stale')]
    stale_frames = len(frames) - len(valid_frames)
    overall_tip_lifts = defaultdict(list)
    overall_flare = defaultdict(list)
    overall_rise = defaultdict(list)
    nodes = []
    for index, node in enumerate(header['nodes']):
        samples = [{'frame': frame['frame'], 'bones': frame['nodes'][index]['bones'],
                    'final': frame['nodes'][index].get('final', {}),
                    'extra': frame.get('extra', {}), 'stale': frame.get('stale', False)}
                   for frame in valid_frames]
        data = {'overall': _empty_accumulator(),
                'rows': defaultdict(_empty_accumulator)}
        meta = {bone['key']: bone for bone in node['bones']}
        rest = dict(node.get('rest') or {})
        if not rest:
            for sample in samples:
                if sample['bones'] is not None:
                    rest = {key: values[3:6] for key, values in sample['bones'].items()}
                    break
        columns = node.get('columns', [])
        horizontal = []
        for left in range(len(columns) if len(columns) >= 3 and header.get('closed', True)
                          else len(columns) - 1):
            right = (left + 1) % len(columns)
            for depth in range(min(len(columns[left]), len(columns[right]))):
                horizontal.append((columns[left][depth], columns[right][depth]))
        pairs = {'horizontal': horizontal,
                 'constraint': node.get('constraints', []),
                 'vertical': [(parent, child)
                              for column in columns
                              for parent, child in zip(column, column[1:])
                              if parent in meta and child in meta and
                              meta[child]['parent_key'] == parent and
                              meta[child]['depth'] >= 0]}
        tips = [column[-1] for column in columns if column]
        tip_columns = [(column[0], column[-1]) for column in columns
                       if column and column[0] in rest and column[-1] in rest]
        rest_radius = None
        if tip_columns:
            center = [_mean([rest[root][axis] for root, _ in tip_columns])
                      for axis in (0, 1)]
            rest_radius = _mean([math.dist(rest[tip][:2], center)
                                 for _, tip in tip_columns])
        for sample in samples:
            if sample['bones'] is None:
                continue
            current_columns = [(root, tip) for root, tip in tip_columns
                               if root in sample['bones'] and tip in sample['bones']]
            if current_columns:
                center = [_mean([sample['bones'][root][axis]
                                 for root, _ in current_columns]) for axis in (0, 1)]
                if rest_radius:
                    flare = _mean([math.dist(sample['bones'][tip][:2], center)
                                   for _, tip in current_columns]) / rest_radius
                    data['overall']['flare_frames'].append(flare)
                    overall_flare[sample['frame']].append(flare)
                rise = _mean([sample['bones'][tip][2] - rest[tip][2]
                              for _, tip in current_columns])
                data['overall']['rise_frames'].append(rise)
                overall_rise[sample['frame']].append(rise)
            for key, values in sample['bones'].items():
                depth = meta.get(key, {}).get('depth', -1)
                _add(data, depth, 'deviation', _distance(values[:3], values[3:6]))
                _add(data, depth, 'lift', values[2] - values[5])
            tip_lifts = [sample['bones'][key][2] - sample['bones'][key][5]
                         for key in tips if key in sample['bones']]
            if tip_lifts:
                lift = max(tip_lifts)
                data['overall']['lift_frames'].append(lift)
                overall_tip_lifts[sample['frame']].append(lift)
                tip_lifts_by_depth = defaultdict(list)
                for key in tips:
                    if key in sample['bones']:
                        depth = meta[key].get('depth', -1)
                        if depth >= 0:
                            tip_lifts_by_depth[depth].append(
                                sample['bones'][key][2] - sample['bones'][key][5])
                for depth, lifts in tip_lifts_by_depth.items():
                    data['rows'][depth]['lift_frames'].append(max(lifts))
            for kind, edges in pairs.items():
                for a, b in edges:
                    if a not in sample['bones'] or b not in sample['bones']:
                        continue
                    av, bv = sample['bones'][a], sample['bones'][b]
                    if a not in rest or b not in rest:
                        continue
                    rest_length = _distance(rest[a], rest[b])
                    if rest_length:
                        length = _distance(av, bv)
                        depth = max(meta[a]['depth'], meta[b]['depth'])
                        _add(data, depth, kind, length / rest_length - 1)
                        if kind == 'vertical':
                            data['overall']['collapse_samples'] += 1
                            data['overall']['collapse_count'] += (
                                length < (1 - collapse_ratio) * rest_length)
                            if depth >= 0:
                                data['rows'][depth]['collapse_samples'] += 1
                                data['rows'][depth]['collapse_count'] += (
                                    length < (1 - collapse_ratio) * rest_length)
        for key, bone in meta.items():
            sequence = [(sample['frame'], sample['bones'].get(key))
                        for sample in samples if sample['bones'] is not None and
                        key in sample['bones']]
            for order, field in ((2, 'acceleration'), (3, 'jitter')):
                values = []
                for position in range(order, len(sequence)):
                    window = sequence[position - order:position + 1]
                    if any(window[i + 1][0] != window[i][0] + 1
                           for i in range(order)):
                        continue
                    difference = [sum((-1) ** (order - j) * math.comb(order, j) *
                                      window[j][1][axis] for j in range(order + 1))
                                  for axis in range(3)]
                    values.append(math.sqrt(sum(value * value for value in difference)))
                rms = _rms(values)
                if rms is not None:
                    _add(data, bone.get('depth', -1), field, (key, rms))
            if bone['dummy_type'] != 'None':
                continue
            previous = None
            for sample in samples:
                value = sample['final'].get(bone['bone_name'])
                if value is not None and previous is not None and sample['frame'] == previous[0] + 1:
                    angle = _angle(value, previous[1])
                    if angle is not None:
                        _add(data, bone.get('depth', -1), 'angles', angle)
                        if angle > spike_degrees:
                            data['overall']['spikes'] += 1
                            if bone.get('depth', -1) >= 0:
                                data['rows'][bone['depth']]['spikes'] += 1
                        if angle > flip_degrees:
                            event = {'bone': key, 'frame': sample['frame'],
                                     'degrees': angle}
                            for accumulator in (data['overall'],
                                                data['rows'][bone['depth']]
                                                if bone.get('depth', -1) >= 0 else None):
                                if accumulator is None:
                                    continue
                                accumulator['flips'] += 1
                                if (accumulator['worst_flip'] is None or
                                        angle > accumulator['worst_flip']['degrees']):
                                    accumulator['worst_flip'] = event
                previous = (sample['frame'], value) if value is not None else None
        for field in ('deviation', 'lift', 'jitter', 'acceleration',
                      'angles', 'horizontal', 'vertical', 'constraint'):
            combined['overall'][field].extend(data['overall'][field])
        combined['overall']['spikes'] += data['overall']['spikes']
        combined['overall']['flips'] += data['overall']['flips']
        combined['overall']['collapse_count'] += data['overall']['collapse_count']
        combined['overall']['collapse_samples'] += data['overall']['collapse_samples']
        worst = data['overall']['worst_flip']
        if worst and (combined['overall']['worst_flip'] is None or
                      worst['degrees'] > combined['overall']['worst_flip']['degrees']):
            combined['overall']['worst_flip'] = {**worst, 'node': index}
        for depth, values in data['rows'].items():
            for field in ('deviation', 'lift', 'jitter', 'acceleration',
                          'lift_frames', 'angles', 'horizontal', 'vertical',
                          'constraint'):
                combined['rows'][depth][field].extend(values[field])
            combined['rows'][depth]['spikes'] += values['spikes']
            combined['rows'][depth]['flips'] += values['flips']
            combined['rows'][depth]['collapse_count'] += values['collapse_count']
            combined['rows'][depth]['collapse_samples'] += values['collapse_samples']
            worst = values['worst_flip']
            if worst and (combined['rows'][depth]['worst_flip'] is None or
                          worst['degrees'] > combined['rows'][depth]['worst_flip']['degrees']):
                combined['rows'][depth]['worst_flip'] = {**worst, 'node': index}
        summary = _summary(data['overall'], rate)
        summary.update(node=node['node'], frame_count=len(frames),
                       stale_frames=stale_frames,
                       skipped_frames=sum(sample['bones'] is None for sample in samples),
                       rows={str(depth): _summary(values, rate)
                             for depth, values in sorted(data['rows'].items())},
                       follow=_follow(samples, tips, header.get('extra_bones', []))
                       if tips and header.get('extra_bones') else {})
        nodes.append(summary)
    combined['overall']['lift_frames'] = [max(lifts) for lifts in overall_tip_lifts.values()]
    combined['overall']['flare_frames'] = [_mean(values) for values in overall_flare.values()]
    combined['overall']['rise_frames'] = [_mean(values) for values in overall_rise.values()]
    overall = _summary(combined['overall'], rate)
    all_tips = [(index, column[-1])
                for index, node in enumerate(header['nodes'])
                for column in node.get('columns', []) if column]
    follow_frames = [
        {'frame': frame['frame'],
         'bones': {f'{index}:{key}': frame['nodes'][index]['bones'][key]
                   for index, key in all_tips
                   if frame['nodes'][index]['bones'] is not None and
                   key in frame['nodes'][index]['bones']},
         'extra': frame.get('extra', {})}
        for frame in valid_frames]
    overall.update(frame_count=len(frames),
                   stale_frames=stale_frames,
                   skipped_frames=sum(any(item['bones'] is None for item in frame['nodes'])
                                  for frame in frames),
                   rows={str(depth): _summary(values, rate)
                         for depth, values in sorted(combined['rows'].items())},
                   follow=_follow(follow_frames,
                                  [f'{index}:{key}' for index, key in all_tips],
                                  header.get('extra_bones', []))
                   if all_tips and header.get('extra_bones') else {})
    return {'overall': overall, 'nodes': nodes, 'spike_degrees': spike_degrees,
            'flip_degrees': flip_degrees, 'collapse_ratio': collapse_ratio}


def compare_motion_metrics(candidate: dict, baseline: dict,
                           tolerance: dict | None = None) -> dict:
    """Compare main motion scalars; flags are tuning hints, not verdicts."""
    thresholds = {'stiffer_ratio': 0.6, 'stiffer_baseline_min': 1.0,
                  'stiffer_difference': 0.5,
                  'jittery_ratio': 1.5, 'jittery_min': 0.01,
                  'jittery_difference': 0.005,
                  'pops_ratio': 1.5, 'pops_difference': 3,
                  'flips_ratio': 1.0, 'flips_difference': 3,
                  'lifts_ratio': 1.3, 'lifts_difference': 1.0,
                  'stretches_ratio': 1.5, 'stretches_min': 0.05,
                  'collapses_ratio': 2.0, 'collapses_min': 0.01,
                  'collapses_difference': 0.005}
    thresholds.update(tolerance or {})
    paths = {'deviation_p95': ('deviation', 'p95'),
             'jitter_mean': ('jitter', 'mean'),
             'angular_p99': ('angular', 'p99'),
             'spikes': ('angular', 'spikes'),
             'flips': ('angular', 'flips'),
             'stretch_horizontal_p95': ('stretch', 'horizontal', 'p95'),
             'stretch_vertical_p95': ('stretch', 'vertical', 'p95'),
             'collapse_fraction': ('collapse_fraction',),
             'lift_p50': ('lift', 'p50'), 'lift_max': ('lift', 'max'),
             'flare_p90': ('flare', 'p90'), 'rise_p50': ('rise', 'p50')}
    def scalar(metrics, path):
        result = metrics['overall']
        for key in path:
            result = result[key]
        return result
    ratios = {}
    for name, path in paths.items():
        value, reference = scalar(candidate, path), scalar(baseline, path)
        ratios[name] = value / reference if value is not None and reference else None
    def greater(name, ratio, difference=0, minimum=0):
        value, reference = (scalar(metrics, paths[name])
                            for metrics in (candidate, baseline))
        return (value is not None and reference is not None and
                value > minimum and value > reference * ratio and
                value > reference + difference)
    deviation, base_deviation = (scalar(metrics, paths['deviation_p95'])
                                 for metrics in (candidate, baseline))
    flags = {
        'stiffer': (deviation is not None and base_deviation is not None and
                    base_deviation >= thresholds['stiffer_baseline_min'] and
                    deviation < base_deviation * thresholds['stiffer_ratio'] and
                    base_deviation - deviation > thresholds['stiffer_difference']),
        'jittery': greater('jitter_mean', thresholds['jittery_ratio'],
                          thresholds['jittery_difference'], thresholds['jittery_min']),
        'pops': greater('spikes', thresholds['pops_ratio'],
                        thresholds['pops_difference']),
        'flips': greater('flips', thresholds['flips_ratio'],
                         thresholds['flips_difference']),
        'lifts': greater('lift_p50', thresholds['lifts_ratio'],
                         thresholds['lifts_difference']),
        'stretches': any(greater(name, thresholds['stretches_ratio'],
                                 minimum=thresholds['stretches_min'])
                         for name in ('stretch_horizontal_p95',
                                      'stretch_vertical_p95')),
        'collapses': greater('collapse_fraction', thresholds['collapses_ratio'],
                             thresholds['collapses_difference'],
                             thresholds['collapses_min'])}
    return {'ratios': ratios, 'flags': flags, 'tolerance': thresholds}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('recording')
    parser.add_argument('--baseline')
    parser.add_argument('--node', type=int)
    arguments = parser.parse_args()
    metrics = compute_motion_metrics(load_recording(arguments.recording))
    if arguments.node is not None:
        metrics = metrics['nodes'][arguments.node]
    output = {'metrics': metrics}
    if arguments.baseline:
        baseline = compute_motion_metrics(load_recording(arguments.baseline))
        if arguments.node is not None:
            baseline = {'overall': baseline['nodes'][arguments.node]}
            candidate = {'overall': metrics}
        else:
            candidate = metrics
        output['comparison'] = compare_motion_metrics(candidate, baseline)
    print(json.dumps(output, indent=2))
