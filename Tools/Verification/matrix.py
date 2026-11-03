"""Expand a verification suite into cells and concrete comparison relations."""

from __future__ import annotations

import copy
import itertools


def _merge(first: dict, second: dict) -> dict:
    """Merge axis settings without mutating either input."""
    result = copy.deepcopy(first)
    for key, value in second.items():
        if key == 'by_fixture':
            continue
        if key == 'overrides':
            result[key] = {**result.get(key, {}), **copy.deepcopy(value)}
        elif key in ('events', 'external_forces'):
            result[key] = result.get(key, []) + copy.deepcopy(value)
        elif key == 'location_offset':
            before = result.get(key, [0, 0, 0])
            result[key] = [a + b for a, b in zip(before, value)]
        else:
            result[key] = copy.deepcopy(value)
    return result


def _matches(tags: dict, where: dict) -> bool:
    return all(tags.get(axis) in values for axis, values in where.items())


def expand_spec(spec: dict) -> dict:
    """Return concrete cells, their axis tags, and every requested relation pair."""
    axes = spec['axes']
    names = list(axes)
    cells, tags = [], {}
    grid = spec['grid']

    def location(index, offset):
        origin = grid['origin']
        spacing = grid['spacing']
        columns = grid['columns']
        return [origin[0] + (index % columns) * spacing[0] + offset[0],
                origin[1] + (index // columns) * spacing[1] + offset[1],
                origin[2] + offset[2]]

    for selection in spec['matrix']:
        choices = []
        for axis in names:
            values = selection.get(axis, [next(iter(axes[axis]))])
            for value in values:
                if value not in axes[axis]:
                    raise ValueError(f'Unknown {axis} value: {value}')
            choices.append(values)
        for combination in itertools.product(*choices):
            tag = dict(zip(names, combination))
            cell_id = '.'.join(combination)
            if cell_id in tags:
                raise ValueError(f'Duplicate cell id: {cell_id}')
            cell = {}
            for axis, value in tag.items():
                item = axes[axis][value]
                cell = _merge(cell, item)
                cell = _merge(cell, item.get('by_fixture', {}).get(tag.get('fixture'), {}))
            offset = cell.pop('location_offset', [0, 0, 0])
            cell['id'] = cell_id
            cell['location'] = location(len(cells), offset)
            cells.append(cell)
            tags[cell_id] = tag
    for extra in spec.get('extra_cells', []):
        cell_id = extra['id']
        if cell_id in tags:
            raise ValueError(f'Duplicate cell id: {cell_id}')
        cell = copy.deepcopy(extra['cell'])
        offset = cell.pop('location_offset', [0, 0, 0])
        cell['id'] = cell_id
        cell.setdefault('location', location(len(cells), offset))
        cells.append(cell)
        tags[cell_id] = copy.deepcopy(extra.get('tags', {}))

    relations = []
    for index, relation in enumerate(spec.get('relations', [])):
        if not relation.get('reason'):
            raise ValueError(f'Relation {index} needs a reason')
        kind = relation['kind']
        where = relation.get('where', {})
        excluded = relation.get('except', {})

        def selected(tag):
            return _matches(tag, where) and not any(tag.get(axis) in values
                                                    for axis, values in excluded.items())

        if kind in ('profile_equal', 'profile_differ'):
            pairs = [(cell_id, cell_id) for cell_id, tag in tags.items() if selected(tag)]
        elif 'vary' in relation:
            vary = relation['vary']
            axis, a_value, b_value = vary['axis'], vary['a'], vary['b']
            if axis not in axes or a_value not in axes[axis] or b_value not in axes[axis]:
                raise ValueError(f'Unknown relation axis value: {vary}')
            by_tag = {tuple(sorted(tag.items())): cell_id for cell_id, tag in tags.items()}
            pairs = []
            for cell_id, tag in tags.items():
                if tag.get(axis) != a_value or not selected(tag):
                    continue
                other = {**tag, axis: b_value}
                match = by_tag.get(tuple(sorted(other.items())))
                if match:
                    pairs.append((cell_id, match))
        else:
            pairs = [(relation['a'], relation['b'])]
        profiles = ([relation.get('a_profile', 'base')]
                    if kind.startswith('profile_') else relation.get('profiles', ['base']))
        if not pairs:
            pairs = [(None, None)]
        for profile in profiles:
            for pair_index, (a, b) in enumerate(pairs):
                relation_id = relation.get('id', f'{a}~{b}' if a is not None
                                           else f'relation_{index}')
                concrete = {key: copy.deepcopy(value) for key, value in relation.items()
                            if key not in ('vary', 'where', 'except', 'profiles')}
                concrete.update({'id': relation_id if 'id' not in relation or
                                 len(pairs) == 1 else f'{relation_id}:{pair_index}',
                                 'a': a, 'b': b,
                                 'profile': profile})
                if kind.startswith('profile_'):
                    concrete['a_profile'] = relation.get('a_profile', 'base')
                    concrete['b_profile'] = relation['b_profile']
                relations.append(concrete)
    return {'cells': cells, 'tags': tags, 'relations': relations,
            'profiles': copy.deepcopy(spec['profiles'])}


def profile_manifest(spec: dict, expanded: dict, profile_name: str) -> dict:
    """Build the runner manifest for one profile."""
    profile = spec['profiles'][profile_name]
    return {'frames': spec['frames'], 'record_nodes': spec.get('record_nodes', False),
            'settings': copy.deepcopy(profile.get('settings', {})),
            'cells': copy.deepcopy(expanded['cells'])}
