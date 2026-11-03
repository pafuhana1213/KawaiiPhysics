"""Make and compare compact, deterministic trajectory baselines."""

from __future__ import annotations

import gzip
import json
import math
import pathlib

import check_invariants


BASELINES = pathlib.Path(__file__).with_name('baselines')


def baseline_path(suite: str, profile: str) -> pathlib.Path:
    """Return the canonical compressed baseline path."""
    return BASELINES / f'{suite}__{profile}.json.gz'


def make_baseline(recording: dict, stride: int = 30) -> dict:
    """Sample real simulated bone positions at a fixed frame stride."""
    if stride <= 0:
        raise ValueError('stride must be positive')
    result = {}
    for cell_id, cell in recording['cells'].items():
        bone_count = len(cell['bones'])
        frames = len(cell['transforms']) // (7 * bone_count) if bone_count else 0
        bones = check_invariants.simulated_bones(cell)
        locations = []
        for frame in range(0, frames, stride):
            for bone in bones:
                offset = (frame * bone_count + bone) * 7
                locations.extend(round(value, 3) if value is not None else None
                                 for value in cell['transforms'][offset:offset + 3])
        result[cell_id] = {'bones': [cell['bones'][index][0] for index in bones],
                           'locations': locations, 'frames': frames}
    return result


def compare_baseline(baseline: dict, recording: dict, max_cm: float = 0.05) -> dict:
    """Compare sampled positions and detect missing or extra cells."""
    meta = baseline.get('meta', {}) if 'cells' in baseline else {}
    expected = baseline.get('cells', baseline)
    actual = recording['cells']
    result = {'cells': {}, 'missing': sorted(set(expected) - set(actual)),
              'extra': sorted(set(actual) - set(expected))}
    for cell_id in sorted(set(expected) & set(actual)):
        entry, cell = expected[cell_id], actual[cell_id]
        bone_count = len(cell['bones'])
        frames = len(cell['transforms']) // (7 * bone_count) if bone_count else 0
        samples = len(entry['locations']) // (3 * len(entry['bones'])) if entry['bones'] else 0
        stride = meta.get('stride', 30 if samples == math.ceil(entry['frames'] / 30)
                          else math.ceil(entry['frames'] / samples) if samples else 30)
        name_to_index = {bone[0]: index for index, bone in enumerate(cell['bones'])}
        worst = {'bone': None, 'frame': None}
        distance = 0.0
        ok = (frames == entry['frames'] and
              set(entry['bones']) == set(cell['bones'][index][0]
                                         for index in check_invariants.simulated_bones(cell)))
        if not all(name in name_to_index for name in entry['bones']):
            ok = False
        else:
            for sample_index in range(samples):
                frame = sample_index * stride
                if frame >= frames:
                    ok = False
                    break
                for bone_index, name in enumerate(entry['bones']):
                    offset = (frame * bone_count + name_to_index[name]) * 7
                    point = cell['transforms'][offset:offset + 3]
                    reference = entry['locations'][(sample_index * len(entry['bones']) +
                                                     bone_index) * 3:][:3]
                    if any(value is None for value in point + reference):
                        ok = False
                        continue
                    delta = math.dist(point, reference)
                    if delta > distance:
                        distance = delta
                        worst = {'bone': name, 'frame': frame}
        result['cells'][cell_id] = {'max_cm': distance, 'worst': worst,
                                    'ok': ok and distance <= max_cm}
    return result


def save(path, cells: dict, meta: dict) -> None:
    """Write stable gzip JSON, including engine, frame count, and stride."""
    path = pathlib.Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    data = json.dumps({'meta': meta, 'cells': cells}, sort_keys=True,
                      separators=(',', ':'), ensure_ascii=False).encode('utf-8')
    with path.open('wb') as target:
        with gzip.GzipFile(fileobj=target, mode='wb', filename='', mtime=0) as output:
            output.write(data)


def load(path) -> dict:
    """Read a compressed baseline and its metadata."""
    with gzip.open(path, 'rt', encoding='utf-8') as source:
        return json.load(source)
