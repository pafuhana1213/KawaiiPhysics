from __future__ import annotations

import json
import math
import os
import re
import time
from collections import defaultdict
from decimal import Decimal, InvalidOperation

import unreal

import toolset_registry


_COMPLIANCE_TYPES = frozenset(
    ('Concrete', 'Wood', 'Leather', 'Tendon', 'Rubber', 'Muscle', 'Fat'))


def _geom_dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def _geom_sub(a, b):
    return tuple(x - y for x, y in zip(a, b))


def _geom_norm(a):
    return math.sqrt(_geom_dot(a, a))


def _geom_lerp(a, b, t):
    return tuple(x + t * (y - x) for x, y in zip(a, b))


def _geom_closest_point_segment(point, start, end):
    """Return the closest point on a segment, including a zero-length segment."""
    direction = _geom_sub(end, start)
    length2 = _geom_dot(direction, direction)
    t = max(0.0, min(1.0, _geom_dot(_geom_sub(point, start), direction) / length2)) if length2 else 0.0
    return _geom_lerp(start, end, t)


def _geom_segment_distance(a, b, c, d):
    """Return the minimum distance between two possibly degenerate segments."""
    u, v, w = _geom_sub(b, a), _geom_sub(d, c), _geom_sub(a, c)
    aa, bb, cc = _geom_dot(u, u), _geom_dot(u, v), _geom_dot(v, v)
    dd, ee = _geom_dot(u, w), _geom_dot(v, w)
    distances = [
        _geom_norm(_geom_sub(p, _geom_closest_point_segment(p, c, d)))
        for p in (a, b)] + [
        _geom_norm(_geom_sub(p, _geom_closest_point_segment(p, a, b)))
        for p in (c, d)]
    denom = aa * cc - bb * bb
    if denom > 1e-15:
        s = (bb * ee - cc * dd) / denom
        t = (aa * ee - bb * dd) / denom
        if 0.0 <= s <= 1.0 and 0.0 <= t <= 1.0:
            distances.append(_geom_norm(_geom_sub(_geom_lerp(a, b, s),
                                                   _geom_lerp(c, d, t))))
    return min(distances)


def _geom_convex_minimum(fn):
    left, right = 0.0, 1.0
    golden = (math.sqrt(5.0) - 1.0) / 2.0
    c, d = right - golden * (right - left), left + golden * (right - left)
    fc, fd = fn(c), fn(d)
    for _ in range(24):
        if fc <= fd:
            right, d, fd = d, c, fc
            c = right - golden * (right - left)
            fc = fn(c)
        else:
            left, c, fc = c, d, fd
            d = left + golden * (right - left)
            fd = fn(d)
    return min(fn(0.0), fn(1.0), fc, fd)


def _geom_tapered_distance(limit, point):
    start, end = limit['start'], limit['end']
    r0, r1 = limit['radius0'], limit['radius1']
    axis = _geom_sub(end, start)
    length2 = _geom_dot(axis, axis)
    if length2 == 0:
        return _geom_norm(_geom_sub(point, start)) - max(r0, r1)
    radius_delta = r0 - r1
    cone2 = length2 - radius_delta * radius_delta
    if cone2 <= 0:
        return min(_geom_norm(_geom_sub(point, start)) - r0,
                   _geom_norm(_geom_sub(point, end)) - r1)
    inverse_length2 = 1.0 / length2
    offset = _geom_sub(point, start)
    y = _geom_dot(offset, axis)
    z = y - length2
    perpendicular = tuple(p * length2 - direction * y
                          for p, direction in zip(offset, axis))
    x2 = _geom_dot(perpendicular, perpendicular)
    k = (math.copysign(1.0, radius_delta) * radius_delta * radius_delta * x2
         if radius_delta != 0 else 0.0)
    if math.copysign(1.0, z) * cone2 * z * z * length2 > k:
        return math.sqrt(x2 + z * z * length2) * inverse_length2 - r1
    if math.copysign(1.0, y) * cone2 * y * y * length2 < k:
        return math.sqrt(x2 + y * y * length2) * inverse_length2 - r0
    return ((math.sqrt(x2 * cone2 * inverse_length2) + y * radius_delta)
            * inverse_length2 - r0)


def _geom_box_distance(limit, point):
    x, y, z, w = limit['rotation']
    length = math.sqrt(x*x + y*y + z*z + w*w)
    x, y, z, w = (v / length for v in (x, y, z, w))
    px, py, pz = _geom_sub(point, limit['location'])
    # Inverse quaternion rotation, using its conjugate.
    tx, ty, tz = 2 * (-y*pz + z*py), 2 * (-z*px + x*pz), 2 * (-x*py + y*px)
    local = (px + w*tx - y*tz + z*ty,
             py + w*ty - z*tx + x*tz,
             pz + w*tz - x*ty + y*tx)
    q = [abs(p) - e for p, e in zip(local, limit['extent'])]
    return _geom_norm([max(0.0, v) for v in q]) + min(max(q), 0.0)


def _geom_point_clearance(limit, point, radius):
    """Return geometric clearance, separate from the solver's previous-frame crossing test."""
    kind = limit['limit_type']
    if kind == 'Spherical':
        distance = _geom_norm(_geom_sub(point, limit['location']))
        signed = (limit['radius0'] - distance if limit['inner_sphere']
                  else distance - limit['radius0'])
    elif kind == 'Capsule':
        signed = _geom_norm(_geom_sub(point, _geom_closest_point_segment(
            point, limit['start'], limit['end']))) - limit['radius0']
    elif kind == 'TaperedCapsule':
        signed = _geom_tapered_distance(limit, point)
    elif kind == 'Box':
        signed = _geom_box_distance(limit, point)
    elif kind == 'Planar':
        signed = _geom_dot(_geom_sub(point, limit['location']), limit['plane_normal'])
    else:
        raise ValueError(f'Unknown limit_type: {kind}')
    return signed - radius


def _geom_segment_penetration(limit, a, b):
    """Return maximum penetration of a segment centerline into a limit."""
    kind = limit['limit_type']
    if kind == 'Spherical' and not limit['inner_sphere']:
        bound_center, bound_radius = limit['location'], limit['radius0']
    elif kind in ('Capsule', 'TaperedCapsule'):
        start, end = limit['start'], limit['end']
        bound_center = _geom_lerp(start, end, 0.5)
        bound_radius = (_geom_norm(_geom_sub(end, start)) * 0.5 +
                        max(limit['radius0'], limit.get('radius1', limit['radius0'])))
    elif kind == 'Box':
        bound_center, bound_radius = limit['location'], _geom_norm(limit['extent'])
    else:
        bound_center = None
    if (bound_center is not None and
            _geom_norm(_geom_sub(bound_center, _geom_closest_point_segment(
                bound_center, a, b))) >= bound_radius):
        return 0.0
    if kind == 'Spherical':
        if limit['inner_sphere']:
            signed = limit['radius0'] - max(
                _geom_norm(_geom_sub(a, limit['location'])),
                _geom_norm(_geom_sub(b, limit['location'])))
        else:
            signed = _geom_norm(_geom_sub(limit['location'],
                _geom_closest_point_segment(limit['location'], a, b))) - limit['radius0']
    elif kind == 'Capsule':
        signed = _geom_segment_distance(a, b, limit['start'], limit['end']) - limit['radius0']
    elif kind == 'Planar':
        signed = min(_geom_point_clearance(limit, p, 0.0) for p in (a, b))
    else:
        signed = _geom_convex_minimum(
            lambda t: _geom_point_clearance(limit, _geom_lerp(a, b, t), 0.0))
    return max(0.0, -signed)


def _build_ring_constraints(children_by_bone, ring_root_bones, min_depth=1,
                            max_depth=-1, closed=True, compliance_type='',
                            exclude_from_subdivision=False):
    """Build adjacent column pairs from a read-only bone hierarchy."""
    if (not ring_root_bones or any(not isinstance(b, str) or not b.strip()
                                   for b in ring_root_bones)
            or len(set(ring_root_bones)) != len(ring_root_bones)):
        raise ValueError('ring_root_bones must be nonempty and unique.')
    if min_depth < 0 or (max_depth != -1 and max_depth < min_depth):
        raise ValueError('Invalid depth range.')
    if compliance_type != '' and compliance_type not in _COMPLIANCE_TYPES:
        raise ValueError(f'Invalid compliance_type: {compliance_type}')
    columns, branched = [], []
    for root in ring_root_bones:
        if root not in children_by_bone:
            raise ValueError(f'Bone not found: {root}')
        bones = [root]
        while True:
            children = children_by_bone.get(bones[-1], [])
            if len(children) > 1:
                branched.append({'root': root, 'bone': bones[-1], 'depth': len(bones)-1})
                break
            if not children:
                break
            bones.append(children[0])
        columns.append({'root': root, 'bones': bones})
    lengths = [len(c['bones']) for c in columns]
    last_depth = max(lengths) - 1 if max_depth == -1 else max_depth
    neighbors = [(i, i+1) for i in range(len(columns)-1)]
    if closed and len(columns) >= 3:
        neighbors.append((len(columns)-1, 0))
    pairs, missing = [], []
    for depth in range(min_depth, last_depth+1):
        for i, j in neighbors:
            if depth >= lengths[i] or depth >= lengths[j]:
                missing.append({'root1': columns[i]['root'], 'root2': columns[j]['root'], 'depth': depth})
                continue
            pair = {'bone1': columns[i]['bones'][depth],
                    'bone2': columns[j]['bones'][depth], 'depth': depth,
                    'exclude_from_subdivision': bool(exclude_from_subdivision)}
            if compliance_type:
                pair['compliance_type'] = compliance_type
            pairs.append(pair)
    if not pairs:
        raise ValueError(f'No constraint pairs were built: columns={len(columns)}, missing={missing}. '
                         'At least two columns with bones at a requested depth are required.')
    def quoted(name):
        return name.replace('\\', '\\\\').replace('"', '\\"')
    entries = []
    for pair in pairs:
        entries.append('(Bone1=(BoneName="%s"),Bone2=(BoneName="%s"),'
                       'bOverrideCompliance=%s,ComplianceType=%s,'
                       'bExcludeFromSubdivision=%s)' % (
                           quoted(pair['bone1']), quoted(pair['bone2']),
                           'True' if compliance_type else 'False',
                           compliance_type or 'Leather',
                           'True' if exclude_from_subdivision else 'False'))
    return {'pairs': pairs, 'count': len(pairs), 'columns': columns,
            'missing': missing, 'branched': branched,
            'unequal_lengths': len(set(lengths)) > 1,
            'import_text': '(' + ','.join(entries) + ')'}


def _bone_children_from_mesh(skeletal_mesh):
    modifier_type = getattr(unreal, 'SkeletonModifier', None)
    if modifier_type is None:
        raise RuntimeError('unreal.SkeletonModifier is required to read the bone hierarchy.')
    modifier = modifier_type()
    if not modifier.set_skeletal_mesh(skeletal_mesh):
        raise RuntimeError('SkeletonModifier could not read the SkeletalMesh skeleton.')
    return modifier


def _collect_children(modifier, roots):
    result = {}
    pending = list(roots)
    while pending:
        bone = pending.pop()
        if bone in result:
            continue
        # get_parent_name also checks that the name resolves in this skeleton.
        modifier.get_parent_name(bone)
        children = [str(name) for name in modifier.get_children_names(bone, False)]
        result[bone] = children
        pending.extend(children)
    return result


def _constraint_pair_key(pair):
    return frozenset((pair['bone1'], pair['bone2']))


def _validate_constraint_pairs(pairs_json):
    try:
        values = json.loads(pairs_json)
    except (TypeError, ValueError) as error:
        raise ValueError(f'pairs_json is not valid JSON: {error}')
    pairs = values.get('pairs') if isinstance(values, dict) else values
    if not isinstance(pairs, list):
        raise ValueError('pairs_json must contain a pairs array.')
    result, seen = [], set()
    for index, pair in enumerate(pairs):
        if not isinstance(pair, dict):
            raise ValueError(f'Pair {index} must be an object.')
        a, b = pair.get('bone1'), pair.get('bone2')
        if not isinstance(a, str) or not a.strip() or not isinstance(b, str) or not b.strip() or a == b:
            raise ValueError(f'Pair {index} needs two distinct nonempty bone names.')
        compliance = pair.get('compliance_type', '')
        if compliance not in ('',) and compliance not in _COMPLIANCE_TYPES:
            raise ValueError(f'Invalid compliance_type in pair {index}: {compliance}')
        exclude = pair.get('exclude_from_subdivision', False)
        if not isinstance(exclude, bool):
            raise ValueError(f'Pair {index} exclude_from_subdivision must be bool.')
        normalized = {'bone1': a, 'bone2': b,
                      'exclude_from_subdivision': exclude}
        if compliance:
            normalized['compliance_type'] = compliance
        key = _constraint_pair_key(normalized)
        if key in seen:
            raise ValueError(f'Duplicate pair: {a}, {b}')
        seen.add(key)
        result.append(normalized)
    return result


def _constraint_data_to_pair(data):
    a = str(data.get_editor_property('bone_reference1').get_editor_property('bone_name'))
    b = str(data.get_editor_property('bone_reference2').get_editor_property('bone_name'))
    result = {'bone1': a, 'bone2': b,
              'exclude_from_subdivision': bool(data.get_editor_property('exclude_from_subdivision'))}
    if data.get_editor_property('override_compliance'):
        result['compliance_type'] = data.get_editor_property('compliance_type').name.title()
    return result


def _constraint_pair_to_data(pair):
    item = unreal.ModifyBoneConstraintData()
    for field, name in (('bone_reference1', pair['bone1']),
                        ('bone_reference2', pair['bone2'])):
        ref = unreal.BoneReference()
        ref.set_editor_property('bone_name', unreal.Name(name))
        item.set_editor_property(field, ref)
    item.set_editor_property('override_compliance', 'compliance_type' in pair)
    if 'compliance_type' in pair:
        item.set_editor_property('compliance_type',
                                 getattr(unreal.XPBDComplianceType, pair['compliance_type'].upper()))
    item.set_editor_property('exclude_from_subdivision', pair['exclude_from_subdivision'])
    return item


def _radius_chain_rates(children_by_bone, local_lengths, roots_and_excludes,
                        dummy_length):
    """Mirror the per-root cumulative local-length normalization in C++."""
    chains = []
    for root, excludes in roots_and_excludes:
        if root not in children_by_bone:
            raise ValueError(f'Bone not found: {root}')
        if root in excludes:
            continue
        records = []
        def walk(bone, depth, length):
            records.append({'bone': bone, 'depth': depth, 'length': length})
            children = [child for child in children_by_bone.get(bone, [])
                        if child not in excludes]
            if not children and dummy_length > 0:
                records.append({'bone': None, 'depth': depth + 1,
                                'length': length + dummy_length})
            for child in children:
                walk(child, depth + 1, length + local_lengths.get(child, 0.0))
        walk(root, 0, 0.0)
        maximum = max(record['length'] for record in records)
        for record in records:
            record['length_rate'] = record['length'] / maximum if maximum > 0 else 0.0
        chains.append({'root': root, 'records': records})
    return chains


def _radius_keys_and_bones(chains, radius_by_depth, dummy_length):
    if not radius_by_depth or any(not isinstance(v, (int, float)) or not math.isfinite(v) or v < 0
                                  for v in radius_by_depth):
        raise ValueError('radius_by_depth must contain finite nonnegative radii.')
    radius = max(radius_by_depth)
    if radius <= 0:
        raise ValueError('radius_by_depth must have a positive maximum.')
    if not chains:
        raise ValueError('No bones remain after exclusions.')
    warnings = []
    per_depth = defaultdict(list)
    real_counts = []
    for chain in chains:
        real = [record for record in chain['records'] if record['bone'] is not None]
        real_counts.append(len(real))
        for record in real:
            per_depth[record['depth']].append(record['length_rate'])
    if len(set(real_counts)) > 1:
        warnings.append('Chains have unequal bone counts; average LengthRates are used.')
    key_values = defaultdict(list)
    for depth, rates in sorted(per_depth.items()):
        if max(rates) - min(rates) > 0.01:
            warnings.append(f'LengthRate spread exceeds 0.01 at depth {depth}; average is used.')
        if depth >= len(radius_by_depth):
            warnings.append(f'Depth {depth} exceeds radius_by_depth; last radius is used.')
        mean = sum(rates) / len(rates)
        key_values[mean].append(radius_by_depth[min(depth, len(radius_by_depth)-1)] / radius)
    dummy_depths = [record['depth'] for chain in chains for record in chain['records']
                    if record['bone'] is None]
    if dummy_depths:
        key_values[1.0].append(radius_by_depth[min(max(dummy_depths), len(radius_by_depth)-1)] / radius)
    max_depth = max(record['depth'] for chain in chains for record in chain['records'])
    unused = len(radius_by_depth) - max_depth - 1
    if unused > 0:
        warnings.append(f'radius_by_depth has {unused} unused entries (depth > {max_depth}).')
    keys = []
    for rate, values in sorted(key_values.items()):
        if len(values) > 1:
            warnings.append(f'Multiple curve keys at LengthRate {rate:.6f}; values are averaged.')
        keys.append({'time': rate, 'value': sum(values) / len(values)})
    def evaluate(rate):
        if rate <= keys[0]['time']:
            return keys[0]['value']
        for left, right in zip(keys, keys[1:]):
            if rate <= right['time']:
                t = (rate-left['time']) / (right['time']-left['time'])
                return left['value'] + t * (right['value']-left['value'])
        return keys[-1]['value']
    bones = []
    for chain in chains:
        for record in chain['records']:
            if record['bone'] is None:
                continue
            requested = radius_by_depth[min(record['depth'], len(radius_by_depth)-1)]
            predicted = radius * evaluate(record['length_rate'])
            bones.append({'bone': record['bone'], 'chain_root': chain['root'],
                          'depth': record['depth'], 'length_rate': record['length_rate'],
                          'requested': requested, 'predicted': predicted,
                          'error': predicted-requested})
    return radius, keys, bones, warnings


def _import_field(text, field):
    match = re.search(r'(?<![A-Za-z0-9_])' + re.escape(field) + r'\s*=\s*', text)
    if not match:
        return None
    start = match.end()
    if start < len(text) and text[start] == '(':
        depth = 0
        for i in range(start, len(text)):
            depth += (text[i] == '(') - (text[i] == ')')
            if depth == 0:
                return text[start:i+1]
    return text[start:].split(',', 1)[0].split(')', 1)[0].strip()


def _import_bone_names(text):
    names = []
    for match in re.finditer(r'BoneName\s*=\s*', text or ''):
        start = match.end()
        if start < len(text) and text[start] == '"':
            chars = []
            index = start + 1
            while index < len(text):
                char = text[index]
                if char == '"':
                    break
                if char == '\\' and index + 1 < len(text) and text[index + 1] in ('"', '\\'):
                    index += 1
                    char = text[index]
                chars.append(char)
                index += 1
            name = ''.join(chars)
        else:
            name = re.split(r'[,\s)]', text[start:], maxsplit=1)[0]
        if name:
            names.append(name)
    return names


def _import_struct_items(text):
    if not text or text == '()':
        return []
    items, depth, start = [], 0, None
    for i, char in enumerate(text):
        if char == '(':
            if depth == 1:
                start = i
            depth += 1
        elif char == ')':
            depth -= 1
            if depth == 1 and start is not None:
                items.append(text[start:i+1])
    return items


# FKawaiiPhysicsSettingsMultiplier の C++ フィールド名 -> Python プロパティ名
_SETTINGS_MULTIPLIER_FIELDS = {
    'Damping': 'damping',
    'Stiffness': 'stiffness',
    'WorldDampingLocation': 'world_damping_location',
    'WorldDampingRotation': 'world_damping_rotation',
    'Radius': 'radius',
    'LimitAngle': 'limit_angle',
}

# add_collision_limit の shape（小文字） -> (正規の shape 名, FAnimNode_KawaiiPhysics の配列プロパティ名)
_COLLISION_LIMIT_SHAPES = {
    'sphere': ('Sphere', 'SphericalLimits'),
    'capsule': ('Capsule', 'CapsuleLimits'),
    'taperedcapsule': ('TaperedCapsule', 'TaperedCapsuleLimits'),
    'box': ('Box', 'BoxLimits'),
    'planar': ('Planar', 'PlanarLimits'),
}

_ANIM_NODE_FUNCTION_EVENTS = {
    'initial_update': unreal.KawaiiPhysicsAnimNodeFunctionEvent.INITIAL_UPDATE,
    'become_relevant': unreal.KawaiiPhysicsAnimNodeFunctionEvent.BECOME_RELEVANT,
    'update': unreal.KawaiiPhysicsAnimNodeFunctionEvent.UPDATE,
}

# FAnimNode_KawaiiPhysics の編集可能な主要 UPROPERTY。ExternalForces は専用 JSON API で取得する。
_GRAPH_NODE_SETTINGS_PROPERTIES = (
    'RootBone', 'ExcludeBones', 'AdditionalRootBones', 'DummyBoneLength',
    'BoneSubdivisionCount', 'bBoneSubdivisionCollisionOnly',
    'bBoneSubdivisionDensifyByRadius', 'BoneConstraintSubdivisionCount',
    'BoneConstraintSubdivisionFeedbackScale', 'BoneForwardAxis',
    'PhysicsSettings', 'SimulationSpace', 'SimulationBaseBone',
    'TargetFramerate', 'bNeedWarmUp', 'WarmUpFrames',
    'bUseWarmUpWhenResetDynamics', 'TeleportDistanceThreshold',
    'TeleportRotationThreshold', 'PlanarConstraint', 'SkelCompMoveScale',
    'bUpdatePhysicsSettingsInGame', 'ResetBoneTransformWhenBoneNotFound',
    'DampingCurveData', 'StiffnessCurveData',
    'WorldDampingLocationCurveData', 'WorldDampingRotationCurveData',
    'RadiusCurveData', 'LimitAngleCurveData', 'SphericalLimits',
    'CapsuleLimits', 'TaperedCapsuleLimits', 'BoxLimits', 'PlanarLimits',
    'LimitsDataAsset', 'PhysicsAssetForLimits', 'MirrorDataTableForLimits',
    'bSkipMirroredBoneWithExistingCollision', 'bSharedCollisionSource',
    'bUseSharedCollision', 'SharedCollisionGroupTag',
    'BoneConstraintGlobalComplianceType',
    'BoneConstraintIterationCountBeforeCollision',
    'BoneConstraintIterationCountAfterCollision',
    'bAutoAddChildDummyBoneConstraint', 'BoneConstraints',
    'BoneConstraintsDataAsset', 'SyncBones', 'Gravity', 'bUseLegacyGravity',
    'bUseDefaultGravityZProjectSetting', 'bUseWorldSpaceGravity',
    'bEnableWind', 'WindScale', 'WindDirectionNoiseAngle',
    'SimpleExternalForce', 'bUseWorldSpaceSimpleExternalForce',
    'CustomExternalForces', 'bAllowWorldCollision',
    'bOverrideCollisionParams', 'CollisionChannelSettings',
    'bIgnoreSelfComponent', 'IgnoreBones', 'IgnoreBoneNamePrefix',
    'bUseSimpleWorldCollision', 'SimpleWorldCollisionGatherInterval',
    'SimpleWorldCollisionObjectTypes',
    'SimpleWorldCollisionConvexFallbackShape',
    'bOverrideSimpleWorldCollisionGatherRadius',
    'SimpleWorldCollisionGatherRadius',
    'bSimpleWorldCollisionGroundCollision',
    'SimpleWorldCollisionSkeletalMeshCollision',
    'SimpleWorldCollisionSource', 'SimpleWorldCollisionSharedTag',
    'KawaiiPhysicsTag',
)

# ESphericalLimitType の値（小文字） -> ImportText の列挙子名
_SPHERICAL_LIMIT_TYPES = {
    'outer': 'Outer',
    'inner': 'Inner',
}

# start_bone_sampler の space -> GetSocketTransform の座標空間
_BONE_SAMPLER_SPACES = {
    'world': unreal.RelativeTransformSpace.RTS_WORLD,
    'component': unreal.RelativeTransformSpace.RTS_COMPONENT,
}

# start_bone_sampler / get_bone_sampler_result / stop_bone_sampler が共有する採取状態
_BONE_SAMPLER: dict | None = None

# Independent from the socket/bone position sampler.
_COLLISION_PENETRATION_SAMPLER: dict | None = None


def _make_preset_apply_options(
        apply_bone_assignment: bool,
        apply_tag: bool) -> unreal.KawaiiPhysicsPresetApplyOptions:
    options = unreal.KawaiiPhysicsPresetApplyOptions()
    # C++ プロパティ名 bApply... は Python では apply... に変換される。
    options.set_editor_property('apply_bone_assignment', apply_bone_assignment)
    options.set_editor_property('apply_tag', apply_tag)
    return options


def _raise_for_invalid_object(value: unreal.Object | None, name: str) -> None:
    if value is None:
        raise ValueError(f'{name} must not be None.')


def _raise_for_invalid_handle(
        handle: unreal.KawaiiPhysicsGraphNodeHandle,
        name: str) -> None:
    if handle is None:
        raise ValueError(f'{name} must not be None.')
    if not unreal.KawaiiPhysicsEditorLibrary.is_graph_node_handle_valid(handle):
        raise ValueError(f'{name} is not a valid KawaiiPhysics graph node handle.')


def _asset_package_path(asset_path: str) -> str:
    dot_index = asset_path.find('.')
    return asset_path[:dot_index] if dot_index >= 0 else asset_path


def _split_asset_path(asset_path: str) -> tuple[str, str]:
    package_path = _asset_package_path(asset_path).rstrip('/')
    folder_path, asset_name = os.path.split(package_path)
    if not folder_path or not asset_name:
        raise ValueError(f'Invalid asset path: {asset_path}')
    return folder_path, asset_name


def _make_tag_container(filter_tag_names: list[str]) -> unreal.GameplayTagContainer:
    if not filter_tag_names:
        return unreal.GameplayTagContainer()

    result = unreal.KawaiiPhysicsEditorLibrary.make_gameplay_tag_container_from_names(
        [unreal.Name(tag_name) for tag_name in filter_tag_names])
    if result is None:
        raise ValueError(
            f'No valid gameplay tags were resolved from: {filter_tag_names}')
    return result


def _resolve_gameplay_tag(tag_name: str) -> unreal.GameplayTag:
    container = unreal.KawaiiPhysicsEditorLibrary.make_gameplay_tag_container_from_names(
        [unreal.Name(tag_name)])
    if container is None:
        raise ValueError(f'No valid gameplay tags were resolved from: {tag_name}')
    tags = container.get_editor_property('gameplay_tags')
    if len(tags) == 0:
        raise ValueError(f'No valid gameplay tags were resolved from: {tag_name}')
    return tags[0]


def _parse_graph_node_properties(properties_json: str) -> list[tuple[str, str]]:
    # 値は ImportText 形式の文字列へ揃える。bool は Python の True/False、数値は str() で変換する
    if not properties_json:
        raise ValueError('properties_json must not be empty.')
    try:
        values = json.loads(properties_json)
    except ValueError as error:
        raise ValueError(f'properties_json is not valid JSON: {error}')
    if not isinstance(values, dict):
        raise ValueError('properties_json must be a JSON object.')

    properties = []
    for name, value in values.items():
        if not name:
            raise ValueError('properties_json must not contain an empty property name.')
        if isinstance(value, bool):
            text = 'True' if value else 'False'
        elif isinstance(value, (int, float)):
            text = str(value)
        elif isinstance(value, str):
            text = value
        else:
            raise ValueError(
                f'{name} must be a string (ImportText format), number or bool.')
        properties.append((name, text))
    return properties


_IGNORED_IMPORT_TEXT_FIELDS = {
    'SyncBones': frozenset({
        'ChildTargets', 'IsShowPreviewBone', 'PreviewBone',
        'TranslationBySyncBone', 'ScaleByLengthRateCurve',
        'LengthRateFromSyncTargetRoot', 'ModifyBoneIndex',
        'InitialPoseLocation', 'DeltaDistance', 'ScaledDeltaDistance',
    }),
}


def _normalize_import_text(value: str | None, property_name: str | None = None):
    """Normalize ImportText tokens and ignore configured fields for a property."""
    if value is None:
        return None
    tokens = []
    atom = []

    def flush_atom():
        if not atom:
            return
        token = ''.join(atom)
        atom.clear()
        try:
            number = Decimal(token)
            if number.is_finite():
                if number == 0:
                    number = Decimal(0)
                token = str(number.normalize())
        except InvalidOperation:
            if token.lower() in ('true', 'false'):
                token = token.lower()
        tokens.append(token)

    index = 0
    while index < len(value):
        char = value[index]
        if char == '"':
            flush_atom()
            start = index
            index += 1
            while index < len(value):
                if value[index] == '\\':
                    index += 2
                elif value[index] == '"':
                    index += 1
                    break
                else:
                    index += 1
            tokens.append(value[start:index])
            continue
        if char.isspace() or char in '(),=':
            flush_atom()
            if not char.isspace():
                tokens.append(char)
        else:
            atom.append(char)
        index += 1
    flush_atom()
    ignored = _IGNORED_IMPORT_TEXT_FIELDS.get(property_name, ())
    if not ignored:
        return tuple(tokens)
    filtered = []
    index = 0
    while index < len(tokens):
        if (tokens[index] in ignored and index + 2 < len(tokens) and
                tokens[index + 1] == '=' and
                (index == 0 or tokens[index - 1] in ('(', ','))):
            index += 2
            if tokens[index] == '(':
                depth = 0
                while index < len(tokens):
                    if tokens[index] == '(':
                        depth += 1
                    elif tokens[index] == ')':
                        depth -= 1
                    index += 1
                    if depth == 0:
                        break
            else:
                index += 1
            if index < len(tokens) and tokens[index] == ',':
                index += 1
            elif filtered and filtered[-1] == ',':
                filtered.pop()
            continue
        filtered.append(tokens[index])
        index += 1
    return tuple(filtered)


def _import_text_satisfies(actual: str, requested: str,
                           property_name: str | None = None) -> bool:
    """Compare ImportText values, allowing requested struct fields to be partial."""
    def split_top_level(value, delimiter):
        parts, start, depth, quoted, escaped = [], 0, 0, False, False
        for index, char in enumerate(value):
            if escaped:
                escaped = False
            elif quoted and char == '\\':
                escaped = True
            elif char == '"':
                quoted = not quoted
            elif not quoted:
                if char == '(':
                    depth += 1
                elif char == ')':
                    depth -= 1
                elif char == delimiter and depth == 0:
                    parts.append(value[start:index].strip())
                    start = index + 1
        parts.append(value[start:].strip())
        return parts

    def parse(value):
        value = value.strip()
        if value.startswith('(') and value.endswith(')'):
            items = split_top_level(value[1:-1], ',')
            if items == ['']:
                return []
            fields = [split_top_level(item, '=') for item in items]
            if any(len(field) > 1 for field in fields):
                return {field[0]: parse('='.join(field[1:])) for field in fields}
            return [parse(item) for item in items]
        if len(value) >= 2 and value[0] == value[-1] == '"':
            return re.sub(r'\\(["\\])', r'\1', value[1:-1])
        return value

    ignored = _IGNORED_IMPORT_TEXT_FIELDS.get(property_name, ())

    def satisfies(left, right):
        if isinstance(left, dict) or isinstance(right, dict):
            return (isinstance(left, dict) and isinstance(right, dict) and
                    all(key in left and satisfies(left[key], value)
                        for key, value in right.items() if key not in ignored))
        if isinstance(left, list) or isinstance(right, list):
            return (isinstance(left, list) and isinstance(right, list) and
                    len(left) == len(right) and
                    all(satisfies(a, b) for a, b in zip(left, right)))
        try:
            a, b = Decimal(left), Decimal(right)
            if a.is_finite() and b.is_finite():
                quantum = Decimal('0.000001')
                return a.quantize(quantum) == b.quantize(quantum)
        except InvalidOperation:
            pass
        if left.lower() in ('true', 'false') and right.lower() in ('true', 'false'):
            return left.lower() == right.lower()
        return left.split('::')[-1] == right.split('::')[-1]

    return satisfies(parse(actual), parse(requested))


def _set_graph_node_property_verified(handle, name: str, text: str) -> None:
    before = _graph_node_property_or_none(handle, name)
    if before is None:
        raise RuntimeError(f'Unable to read KawaiiPhysics graph node property: {name}')
    ok = unreal.KawaiiPhysicsEditorLibrary.set_graph_node_property_from_string(
        handle, unreal.Name(name), text)
    if not ok:
        raise RuntimeError(f'Unable to set KawaiiPhysics graph node property: {name}')
    after = _graph_node_property_or_none(handle, name)
    if after is None:
        raise RuntimeError(f'Unable to read back KawaiiPhysics graph node property: {name}')
    if (_normalize_import_text(after, name) == _normalize_import_text(before, name)
            and not _import_text_satisfies(after, text, name)):
        raise RuntimeError(
            f'KawaiiPhysics graph node property {name} was not applied; '
            f'read-back value: {after}')


def _parse_graph_node_settings_snapshot(snapshot_json: str, node_index: int):
    if not snapshot_json:
        raise ValueError('Settings snapshot JSON must not be empty.')
    try:
        snapshot = json.loads(snapshot_json)
    except ValueError as error:
        raise ValueError(f'Settings snapshot is not valid JSON: {error}')
    if isinstance(snapshot, list):
        matches = [item for item in snapshot if isinstance(item, dict)
                   and item.get('index') == node_index]
        if len(matches) != 1:
            raise ValueError(f'Settings snapshot must contain exactly one node with index {node_index}.')
        snapshot = matches[0]
    if not isinstance(snapshot, dict):
        raise ValueError('Settings snapshot must be a node or settings JSON object.')
    if 'settings' in snapshot:
        if 'index' in snapshot and snapshot['index'] != node_index:
            raise ValueError(f'Settings snapshot index does not match node_index {node_index}.')
        settings = snapshot['settings']
        forces = snapshot.get('external_forces')
        has_forces = 'external_forces' in snapshot
        key = snapshot.get('key')
    else:
        settings, forces, has_forces, key = snapshot, None, False, None
    if not isinstance(settings, dict):
        raise ValueError('Settings snapshot settings must be a JSON object.')
    unknown = set(settings) - set(_GRAPH_NODE_SETTINGS_PROPERTIES)
    if unknown:
        raise ValueError(f'Unknown settings properties: {sorted(unknown)}')
    for name, value in settings.items():
        if value is not None and not isinstance(value, str):
            raise ValueError(f'Settings snapshot property {name} must be an ImportText string or null.')
    if has_forces and not isinstance(forces, list):
        raise ValueError('Settings snapshot external_forces must be a JSON array.')
    return settings, forces, has_forces, key


def _set_graph_node_properties_impl(
        handle: unreal.KawaiiPhysicsGraphNodeHandle,
        properties: list[tuple[str, str]]) -> list[str]:
    # tool_call ラッパは例外を握るため、add_kawaii_physics_node からもこの素の関数を使う
    _raise_for_invalid_handle(handle, 'handle')
    set_names = []
    for name, text in properties:
        _set_graph_node_property_verified(handle, name, text)
        set_names.append(name)
    return set_names


def _raise_for_invalid_shared_publisher_handle(
        handle: unreal.KawaiiPhysicsSharedPublisherGraphNodeHandle,
        name: str) -> None:
    if handle is None:
        raise ValueError(f'{name} must not be None.')
    if not unreal.KawaiiPhysicsEditorLibrary.is_shared_publisher_graph_node_handle_valid(handle):
        raise ValueError(
            f'{name} is not a valid KawaiiPhysics Shared Publisher graph node handle.')


def _set_shared_publisher_node_property_impl(
        handle: unreal.KawaiiPhysicsSharedPublisherGraphNodeHandle,
        property_name: str,
        value: str) -> None:
    # tool_call ラッパは例外を握るため、add_shared_publisher_node からもこの素の関数を使う
    _raise_for_invalid_shared_publisher_handle(handle, 'handle')
    if not property_name:
        raise ValueError('property_name must not be empty.')
    if value is None:
        raise ValueError('value must not be None.')

    ok = unreal.KawaiiPhysicsEditorLibrary.set_shared_publisher_node_property_from_string(
        handle,
        unreal.Name(property_name),
        value,
    )
    if not ok:
        raise RuntimeError(
            f'Unable to set KawaiiPhysics Shared Publisher node property: {property_name}')


def _set_anim_graph_input_animation_impl(
        anim_blueprint: unreal.AnimBlueprint,
        animation: unreal.AnimSequenceBase,
        graph_name: str) -> None:
    _raise_for_invalid_object(anim_blueprint, 'anim_blueprint')
    _raise_for_invalid_object(animation, 'animation')
    ok = unreal.KawaiiPhysicsEditorLibrary.set_anim_graph_input_animation(
        anim_blueprint,
        animation,
        graph_name,
    )
    if not ok:
        raise RuntimeError(
            'Unable to set the AnimGraph input animation. Check that the '
            'animation skeleton matches the AnimBlueprint target skeleton, that '
            'the graph exists, and see the output log for details.')


def _make_root_bone_setting(bone_name: str) -> unreal.KawaiiPhysicsRootBoneSetting:
    if not bone_name:
        raise ValueError('additional_root_bones must not contain an empty bone name.')
    bone_reference = unreal.BoneReference()
    bone_reference.set_editor_property('bone_name', unreal.Name(bone_name))
    setting = unreal.KawaiiPhysicsRootBoneSetting()
    setting.set_editor_property('root_bone', bone_reference)
    setting.set_editor_property('override_exclude_bones', [])
    # C++ プロパティ名 bUseOverrideExcludeBones は Python では use_override_exclude_bones に変換される。
    setting.set_editor_property('use_override_exclude_bones', False)
    return setting


def _collect_graph_nodes_impl(
        anim_blueprint: unreal.AnimBlueprint,
        filter_tag_names: list[str],
        filter_exact_match: bool) -> list[unreal.KawaiiPhysicsGraphNodeHandle]:
    # tool_call ラッパは例外を握って既定値を返すため、ツール同士の呼び合いはこの素の関数を経由する
    _raise_for_invalid_object(anim_blueprint, 'anim_blueprint')
    filter_tags = _make_tag_container(filter_tag_names)
    handles = unreal.KawaiiPhysicsEditorLibrary.collect_kawaii_physics_graph_nodes(
        anim_blueprint,
        filter_tags,
        filter_exact_match,
    )
    if handles is None:
        raise RuntimeError('Unable to collect KawaiiPhysics graph nodes.')
    return list(handles)


def _validate_placement_requests_impl(
        anim_blueprint: unreal.AnimBlueprint,
        requests: list[unreal.KawaiiPhysicsNodePlacementRequest]) -> list[str]:
    _raise_for_invalid_object(anim_blueprint, 'anim_blueprint')
    result = unreal.KawaiiPhysicsEditorLibrary.validate_placement_requests(
        anim_blueprint,
        requests,
    )
    if result is None:
        raise RuntimeError(
            'Unexpected return value from validate_placement_requests.')
    return [str(error) for error in result]


def _validate_requests_or_raise(
        anim_blueprint: unreal.AnimBlueprint,
        requests: list[unreal.KawaiiPhysicsNodePlacementRequest]) -> list[str]:
    errors = _validate_placement_requests_impl(anim_blueprint, requests)
    blocking_errors = [
        error for error in errors
        if not str(error).startswith('Warning:')
    ]
    if blocking_errors:
        raise ValueError(
            'Invalid KawaiiPhysics placement requests: ' +
            '; '.join(blocking_errors))
    return errors


def _unpack_bool_out(result: object, default: object) -> object:
    # C++ の bool 戻り値 + out 引数は (bool, out) のタプルで返るが、
    # グルーが bool を剥がして out 単体（失敗時は None）を返す場合もあるため両対応する。
    if isinstance(result, tuple):
        if len(result) != 2:
            raise RuntimeError(f'Unexpected return value: {result}')
        if isinstance(result[0], bool) and not result[0]:
            return default
        return result[1]
    if result is None:
        return default
    return result


def _unpack_bool_result(result: object) -> bool:
    # UPARAM(ref) 引数を持つ bool 関数は (bool, ref の値) のタプル、または bool を剥がした
    # ref の値単体（失敗時は None）で返る。タプルは常に真になるため bool を取り出して判定する。
    if isinstance(result, bool):
        return result
    if result is None:
        return False
    if isinstance(result, tuple):
        return bool(result[0]) if result and isinstance(result[0], bool) else True
    return True


def _unpack_float_out(result: object) -> float | None:
    # bool 戻り値 + float& 出力 + UPARAM(ref) 引数（GetAlphaOnComponent の FilterTags 等）は
    # (bool, float, ref の値)・(float, ref の値)・float・None のいずれかで返るため float 要素を探す。
    if result is None:
        return None
    values = result if isinstance(result, tuple) else (result,)
    if values and isinstance(values[0], bool) and not values[0]:
        return None
    for value in values:
        if isinstance(value, float):
            return value
    return None


def _unpack_count_and_out(result: object) -> tuple[int, object]:
    # int32 戻り値 + out 引数は (count, out) のタプルで返る。
    if not isinstance(result, tuple) or len(result) != 2:
        raise RuntimeError(f'Unexpected return value: {result}')
    return int(result[0]), result[1]


def _resolve_world(prefer_pie: bool) -> unreal.World:
    world = None
    if prefer_pie:
        editor_subsystem = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
        if editor_subsystem is not None:
            world = editor_subsystem.get_game_world()
    if world is None:
        # UnrealEditorSubsystem.get_editor_world() は PIE 中にプレイモードのエラーを出して None を返すため、
        # PIE 中でもエディタワールドを返す C++ 側の関数を使う
        world = unreal.KawaiiPhysicsEditorLibrary.get_editor_world_ignoring_play_mode()
    if world is None:
        raise RuntimeError('No world available.')
    return world


def _get_alpha_on_component(
        component: unreal.SkeletalMeshComponent,
        filter_tags: unreal.GameplayTagContainer,
        filter_exact_match: bool) -> float | None:
    return _unpack_float_out(
        unreal.KawaiiPhysicsLibrary.get_alpha_on_component(
            component,
            filter_tags,
            filter_exact_match,
        ))


def _set_alpha_on_component(
        component: unreal.SkeletalMeshComponent,
        alpha: float,
        filter_tags: unreal.GameplayTagContainer,
        filter_exact_match: bool) -> bool:
    return _unpack_bool_result(
        unreal.KawaiiPhysicsLibrary.set_alpha_on_component(
            component,
            alpha,
            filter_tags,
            filter_exact_match,
        ))


def _actor_label(actor: unreal.Actor) -> str:
    try:
        return str(actor.get_actor_label())
    except Exception:
        # ランタイムワールドの Actor では get_actor_label() が使えないことがある
        return str(actor.get_name())


def _does_actor_match_label(actor: unreal.Actor, actor_label: str) -> bool:
    if actor is None:
        return False
    if _actor_label(actor) == actor_label:
        return True
    # PIE の get_name() は UEDPIE_0_ 接頭辞や連番が付くため完全一致だけを見る
    return str(actor.get_name()) == actor_label


def _find_actors_by_label(
        world: unreal.World,
        actor_label: str) -> list[unreal.Actor]:
    return [
        actor
        for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor)
        if _does_actor_match_label(actor, actor_label)
    ]


def _skeletal_mesh_components(
        actor: unreal.Actor) -> list[unreal.SkeletalMeshComponent]:
    if actor is None:
        return []
    return list(actor.get_components_by_class(unreal.SkeletalMeshComponent))


def _find_skeletal_mesh_components_by_label(
        actor_label: str,
        prefer_pie: bool) -> list[unreal.SkeletalMeshComponent]:
    if not actor_label:
        raise ValueError('actor_label must not be empty.')

    world = _resolve_world(prefer_pie)
    components = []
    for actor in _find_actors_by_label(world, actor_label):
        components.extend(_skeletal_mesh_components(actor))
    return components


def _anim_class_name(component: unreal.SkeletalMeshComponent) -> str:
    anim_class = None
    try:
        anim_class = component.get_editor_property('anim_class')
    except Exception:
        anim_class = None
    if anim_class is None:
        anim_instance = component.get_anim_instance()
        if anim_instance is not None:
            anim_class = anim_instance.get_class()
    return '' if anim_class is None else str(anim_class.get_name())


def _handle_to_dict(handle: unreal.KawaiiPhysicsTransientHandle) -> dict[str, int]:
    if handle is None:
        return {'id': 0}
    # Id は Blueprint 非公開の UPROPERTY で get_editor_property では読めないため、ExportText から取り出す
    match = re.search(r'Id=(-?\d+)', handle.export_text())
    return {'id': int(match.group(1)) if match else 0}


def _handle_to_json(handle: unreal.KawaiiPhysicsTransientHandle) -> str:
    return json.dumps(_handle_to_dict(handle))


def _handle_from_json(text: str) -> unreal.KawaiiPhysicsTransientHandle:
    if not text:
        raise ValueError('handle_json must not be empty.')
    try:
        value = json.loads(text)
    except ValueError as error:
        raise ValueError(f'handle_json is not valid JSON: {error}')

    # {"id": 123} と素の 123 のどちらも受け付ける
    if isinstance(value, dict):
        value = value.get('id')
    if not isinstance(value, int) or isinstance(value, bool):
        raise ValueError('handle_json must be {"id": <int>} or an integer.')

    handle = unreal.KawaiiPhysicsTransientHandle()
    handle.import_text(f'(Id={value})')
    return handle


def _graph_node_key(
        handle: unreal.KawaiiPhysicsGraphNodeHandle,
        index: int) -> str:
    # NodeGuid が読めればそれをキーにし、読めない環境では安定な代替キーへフォールバックする
    try:
        node = handle.get_editor_property('node')
        if node is not None:
            try:
                guid = node.get_editor_property('node_guid')
                parts = [
                    int(guid.get_editor_property(name)) & 0xFFFFFFFF
                    for name in ('a', 'b', 'c', 'd')
                ]
                return ''.join(f'{part:08X}' for part in parts)
            except Exception:
                return str(node.get_name())
    except Exception:
        pass
    return f'node{index}'


def _graph_node_property_or_none(
        handle: unreal.KawaiiPhysicsGraphNodeHandle,
        property_name: str) -> str | None:
    value = unreal.KawaiiPhysicsEditorLibrary.get_graph_node_property_as_string(
        handle,
        unreal.Name(property_name),
    )
    return None if value is None else str(value)


def _make_settings_multiplier(
        settings_json: str) -> unreal.KawaiiPhysicsSettingsMultiplier:
    if not settings_json:
        raise ValueError('settings_json must not be empty.')
    try:
        values = json.loads(settings_json)
    except ValueError as error:
        raise ValueError(f'settings_json is not valid JSON: {error}')
    if not isinstance(values, dict):
        raise ValueError('settings_json must be a JSON object.')

    settings_scale = unreal.KawaiiPhysicsSettingsMultiplier()
    for name, value in values.items():
        property_name = _SETTINGS_MULTIPLIER_FIELDS.get(name)
        if property_name is None and name in _SETTINGS_MULTIPLIER_FIELDS.values():
            property_name = name
        if property_name is None:
            raise ValueError(
                f'Unknown FKawaiiPhysicsSettingsMultiplier field: {name}. '
                f'Valid fields: {", ".join(_SETTINGS_MULTIPLIER_FIELDS)}')
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            raise ValueError(f'{name} must be a number.')
        settings_scale.set_editor_property(property_name, float(value))
    return settings_scale


def _make_gust_direction(direction: list[float]) -> unreal.Vector:
    if not direction:
        # ゼロベクトルは既存 ProceduralWind の風向き・空間・ボーンフィルタを継承する
        return unreal.Vector(0.0, 0.0, 0.0)
    if len(direction) != 3:
        raise ValueError('direction must have 3 elements or be empty.')
    return unreal.Vector(
        float(direction[0]),
        float(direction[1]),
        float(direction[2]),
    )


def _make_float3(values: list[float], name: str) -> list[float]:
    if values is None or len(values) != 3:
        raise ValueError(f'{name} must have 3 elements.')
    result = []
    for value in values:
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            raise ValueError(f'{name} must contain only numbers.')
        result.append(float(value))
    return result


def _make_rotator(pitch_yaw_roll: list[float]) -> unreal.Rotator:
    # unreal.Rotator の位置引数は (roll, pitch, yaw) 順のため属性で設定する
    rotator = unreal.Rotator()
    rotator.pitch = float(pitch_yaw_roll[0])
    rotator.yaw = float(pitch_yaw_roll[1])
    rotator.roll = float(pitch_yaw_roll[2])
    return rotator


def _clean_float(value: float) -> float:
    # 行列演算の丸め誤差と -0.0 を落とし、ImportText 文字列を安定させる
    rounded = round(float(value), 4)
    return 0.0 if rounded == 0.0 else rounded


def _compute_collision_limit_offset(
        bone_transform: unreal.Transform | None,
        location: list[float],
        rotation: list[float]) -> tuple[list[float], list[float]]:
    """Converts a component-space limit transform into the DrivingBone-local offset.

    bone_transform is the DrivingBone component-space reference pose (None
    keeps the values in component space, as for a Planar limit without a
    DrivingBone). location is [x, y, z]; rotation is [pitch, yaw, roll] or
    empty for identity. Returns (OffsetLocation [x, y, z], OffsetRotation
    [pitch, yaw, roll]).
    """
    target_location = _make_float3(location, 'location')
    target_rotation = _make_float3(rotation, 'rotation') if rotation else [0.0, 0.0, 0.0]

    # ランタイムは FTransform(OffsetRotation, OffsetLocation) * BoneCS で配置するため、
    # オフセットは目標トランスフォームの DrivingBone 相対になる
    # Kismet の Transform 系関数は Python では unreal.Transform のメソッドとして公開される
    target = unreal.Transform(
        unreal.Vector(*target_location),
        _make_rotator(target_rotation),
        unreal.Vector(1.0, 1.0, 1.0),
    )
    offset = target if bone_transform is None else target.make_relative(bone_transform)
    offset_location = offset.translation
    offset_rotation = offset.rotation.rotator()
    return (
        [
            _clean_float(offset_location.x),
            _clean_float(offset_location.y),
            _clean_float(offset_location.z),
        ],
        [
            _clean_float(offset_rotation.pitch),
            _clean_float(offset_rotation.yaw),
            _clean_float(offset_rotation.roll),
        ],
    )


def _format_import_float(value: float) -> str:
    return f'{float(value):.6f}'


def _format_import_vector(values: list[float]) -> str:
    return '(X={},Y={},Z={})'.format(*[_format_import_float(value) for value in values])


def _make_collision_limit_text(
        shape: str,
        driving_bone: str,
        offset_location: list[float],
        offset_rotation: list[float],
        radius: float,
        radius1: float,
        length: float,
        extent: list[float],
        limit_type: str) -> str:
    fields = []
    if driving_bone:
        fields.append(f'DrivingBone=(BoneName="{driving_bone}")')
    fields.append('OffsetLocation=' + _format_import_vector(offset_location))
    fields.append('OffsetRotation=(Pitch={},Yaw={},Roll={})'.format(
        *[_format_import_float(value) for value in offset_rotation]))
    if shape == 'Sphere':
        fields.append('Radius=' + _format_import_float(radius))
        fields.append('LimitType=' + limit_type)
    elif shape == 'Capsule':
        fields.append('Radius=' + _format_import_float(radius))
        fields.append('Length=' + _format_import_float(length))
    elif shape == 'TaperedCapsule':
        fields.append('Radius0=' + _format_import_float(radius))
        fields.append('Radius1=' + _format_import_float(radius1))
        fields.append('Length=' + _format_import_float(length))
    elif shape == 'Box' and extent:
        fields.append('Extent=' + _format_import_vector(extent))
    return '(' + ','.join(fields) + ')'


def _count_import_text_array_elements(array_text: str) -> int:
    # 構造体配列の ImportText は "((...),(...))" 形式のため、深さ 2 へ入る括弧の数が要素数になる
    depth = 0
    count = 0
    in_quote = False
    escaped = False
    for character in array_text or '':
        if in_quote:
            if escaped:
                escaped = False
            elif character == '\\':
                escaped = True
            elif character == '"':
                in_quote = False
            continue
        if character == '"':
            in_quote = True
        elif character == '(':
            depth += 1
            if depth == 2:
                count += 1
        elif character == ')':
            depth -= 1
    return count


def _append_import_text_array_element(array_text: str, element_text: str) -> str:
    text = (array_text or '').strip()
    if text in ('', '()'):
        return f'({element_text})'
    if not (text.startswith('(') and text.endswith(')')):
        raise RuntimeError(f'Unexpected array text: {text}')
    return f'{text[:-1]},{element_text})'


def _make_bone_sampler_targets(
        components: list[unreal.SkeletalMeshComponent],
        bone_pattern: str) -> list[tuple[str, unreal.SkeletalMeshComponent, str]]:
    pattern = re.compile(bone_pattern)
    matches = []
    for component in components:
        for socket_name in component.get_all_socket_names():
            bone_name = str(socket_name)
            if pattern.search(bone_name):
                matches.append((component, bone_name))

    # ボーン名が複数コンポーネントで重複する場合だけコンポーネント名で修飾する
    name_counts: dict[str, int] = {}
    for _component, bone_name in matches:
        name_counts[bone_name] = name_counts.get(bone_name, 0) + 1
    component_counts: dict[tuple[str, str], int] = {}
    for component, bone_name in matches:
        identity = (str(component.get_name()), bone_name)
        component_counts[identity] = component_counts.get(identity, 0) + 1
    targets = []
    for component, bone_name in matches:
        if name_counts[bone_name] == 1:
            key = bone_name
        elif component_counts[(str(component.get_name()), bone_name)] == 1:
            key = f'{component.get_name()}.{bone_name}'
        else:
            key = f'{_actor_label(component.get_owner())}.{component.get_name()}.{bone_name}'
        targets.append((key, component, bone_name))
    return targets


def _make_bone_sample_accumulator() -> dict:
    return {
        'min': [0.0, 0.0, 0.0],
        'max': [0.0, 0.0, 0.0],
        'sum': [0.0, 0.0, 0.0],
        'count': 0,
        'prev': None,
        'step_square_sum': 0.0,
        'step_count': 0,
        'nan': False,
    }


def _accumulate_bone_sample(accumulator: dict, location: list[float]) -> None:
    if any(not math.isfinite(value) for value in location):
        accumulator['nan'] = True
        accumulator['prev'] = None
        return

    if accumulator['count'] == 0:
        accumulator['min'] = list(location)
        accumulator['max'] = list(location)
    else:
        accumulator['min'] = [min(a, b) for a, b in zip(accumulator['min'], location)]
        accumulator['max'] = [max(a, b) for a, b in zip(accumulator['max'], location)]
    accumulator['sum'] = [a + b for a, b in zip(accumulator['sum'], location)]
    accumulator['count'] += 1

    previous = accumulator['prev']
    if previous is not None:
        accumulator['step_square_sum'] += sum(
            (a - b) ** 2 for a, b in zip(location, previous))
        accumulator['step_count'] += 1
    accumulator['prev'] = list(location)


def _stop_bone_sampler_impl() -> None:
    state = _BONE_SAMPLER
    if state is None:
        return
    tick_handle = state.get('tick_handle')
    if tick_handle is not None:
        state['tick_handle'] = None
        unreal.unregister_slate_post_tick_callback(tick_handle)
    state['done'] = True


def _bone_sampler_tick(delta_seconds: float) -> None:
    state = _BONE_SAMPLER
    if state is None or state['done']:
        return

    try:
        for key, component, bone_name in state['targets']:
            transform = component.get_socket_transform(
                unreal.Name(bone_name),
                _BONE_SAMPLER_SPACES[state['space']],
            )
            location = transform.translation
            _accumulate_bone_sample(
                state['bones'][key],
                [location.x, location.y, location.z],
            )
        state['frames_collected'] += 1
    except Exception as error:
        # PIE 終了などでコンポーネントが失効した場合は採取を止めて理由を残す
        state['error'] = f'{type(error).__name__}: {error}'
        state['done'] = True

    if state['frames_collected'] >= state['frames']:
        state['done'] = True
    if state['done']:
        _stop_bone_sampler_impl()


def _runtime_enum(value):
    name = str(getattr(value, 'name', value)).split('.')[-1]
    # UE Python の列挙子名（UPPER_SNAKE）を C++ の列挙子名（PascalCase）へ戻す
    return ''.join(part.capitalize() for part in name.split('_'))


def _runtime_vector(value):
    return [float(value.x), float(value.y), float(value.z)]


def _runtime_info_to_dict(info, component_name):
    """Contain all reflected runtime struct reads in one place."""
    bones = [{
        'index': int(bone.index), 'bone_name': str(bone.bone_name),
        'dummy_type': _runtime_enum(bone.dummy_type),
        'parent_index': int(bone.parent_index),
        'location': _runtime_vector(bone.location),
        'pose_location': _runtime_vector(bone.pose_location),
        'radius': float(bone.radius),
        'length_rate': float(bone.length_rate_from_root),
        'skip_simulate': bool(getattr(bone, 'skip_simulate', False)),
    } for bone in info.bones]
    _assign_runtime_bone_keys(bones)
    limits = [{
        'limit_type': _runtime_enum(limit.limit_type),
        'source_type': _runtime_enum(limit.source_type),
        'source_array': str(limit.source_array_name),
        'source_index': int(limit.source_index),
        'driving_bone': str(limit.driving_bone),
        'enabled': bool(limit.enabled),
        'location': _runtime_vector(limit.location),
        'rotation': [float(limit.rotation.x), float(limit.rotation.y),
                     float(limit.rotation.z), float(limit.rotation.w)],
        'start': _runtime_vector(limit.start),
        'end': _runtime_vector(limit.end),
        'radius0': float(limit.radius0), 'radius1': float(limit.radius1),
        'extent': _runtime_vector(limit.extent),
        'plane_normal': _runtime_vector(limit.plane_normal),
        'inner_sphere': bool(limit.inner_sphere),
    } for limit in info.limits]
    by_index = {bone['index']: bone for bone in bones}
    constraints = []
    for item in info.constraints:
        first, second = int(item.bone_index1), int(item.bone_index2)
        constraints.append({
            'bone_index1': first, 'bone_index2': second,
            'bone_name1': str(item.bone_name1),
            'bone_name2': str(item.bone_name2),
            'bone_key1': by_index[first]['key'] if first in by_index else None,
            'bone_key2': by_index[second]['key'] if second in by_index else None,
            'source_type': _runtime_enum(item.source_type),
        })
    return {'component': str(component_name),
            'anim_instance_class': str(info.anim_instance_class_name),
            'node_index': int(info.node_index), 'root_bone': str(info.root_bone),
            'tag': str(info.tag.get_editor_property('tag_name')),
            'simulation_space': _runtime_enum(info.simulation_space),
            'evaluated': bool(info.evaluated),
            'non_uniform_scale': bool(info.non_uniform_scale),
            'bones': bones, 'limits': limits, 'constraints': constraints,
            'num_convex_limits': int(info.num_convex_limits),
            'convex_fallback_shape': _runtime_enum(info.convex_fallback_shape)}


def _assign_runtime_bone_keys(bones):
    """Name real bones and assign stable names to the three dummy kinds."""
    by_index = {bone['index']: bone for bone in bones}
    counters = defaultdict(int)
    for bone in bones:
        parent = by_index.get(bone['parent_index'])
        while parent is not None and parent['dummy_type'] != 'None':
            parent = by_index.get(parent['parent_index'])
        ancestor = parent['bone_name'] if parent else ''
        bone['real_ancestor'] = ancestor
        kind = bone['dummy_type']
        if kind == 'None':
            bone['key'] = bone['bone_name']
        elif kind == 'Tip':
            bone['key'] = f'{ancestor}#tip'
        elif kind == 'InterBone':
            counters[ancestor] += 1
            bone['key'] = f'{ancestor}#inter{counters[ancestor]}'
        else:
            bone['key'] = f"#bridge{bone['index']}"


def _runtime_sample_info(info, target, record=False):
    """Read fixed endpoints and limits, plus all eligible bones when recording."""
    raw_bones = info.bones
    topology = tuple((int(bone.index), _runtime_enum(bone.dummy_type),
                      int(bone.parent_index)) for bone in raw_bones)
    if topology != target['topology']:
        if target.get('record_started'):
            raise RuntimeError('Runtime bone topology changed during recording.')
        bones = [{'index': index, 'dummy_type': kind, 'parent_index': parent,
                  'bone_name': str(bone.bone_name)}
                 for bone, (index, kind, parent) in zip(raw_bones, topology)]
        _assign_runtime_bone_keys(bones)
        target['keys_by_index'] = {bone['index']: bone['key'] for bone in bones
                                   if bone['key'] in target['endpoint_keys']}
        if 'record_keys_by_index' in target:
            target['record_keys_by_index'] = {bone['index']: bone['key'] for bone in bones
                                              if bone['dummy_type'] in ('None', 'Tip')}
            converted = _runtime_info_to_dict(info, target['identity']['component'])
            target['record_header_node'].update(
                _record_node_header(converted, target['ring_root_bones']))
            target['real_bones'] = tuple(dict.fromkeys(
                bone['bone_name'] for bone in converted['bones']
                if bone['dummy_type'] == 'None'))
        target['topology'] = topology
    keys_by_index = target['keys_by_index']
    positions = {keys_by_index[index]: _runtime_vector(bone.location)
                 for bone, (index, _, _) in zip(raw_bones, topology)
                 if index in keys_by_index}
    limits = []
    for limit in info.limits:
        if not limit.enabled:
            continue
        kind = _runtime_enum(limit.limit_type)
        shape = {'limit_type': kind, 'source_type': _runtime_enum(limit.source_type),
                 'source_array': str(limit.source_array_name),
                 'source_index': int(limit.source_index),
                 'driving_bone': str(limit.driving_bone)}
        if kind == 'Spherical':
            shape.update(location=_runtime_vector(limit.location),
                         radius0=float(limit.radius0), inner_sphere=bool(limit.inner_sphere))
        elif kind in ('Capsule', 'TaperedCapsule'):
            shape.update(start=_runtime_vector(limit.start), end=_runtime_vector(limit.end),
                         radius0=float(limit.radius0))
            if kind == 'TaperedCapsule':
                shape['radius1'] = float(limit.radius1)
        elif kind == 'Box':
            rotation = limit.rotation
            shape.update(location=_runtime_vector(limit.location),
                         rotation=[float(rotation.x), float(rotation.y),
                                   float(rotation.z), float(rotation.w)],
                         extent=_runtime_vector(limit.extent))
        elif kind == 'Planar':
            shape.update(location=_runtime_vector(limit.location),
                         plane_normal=_runtime_vector(limit.plane_normal))
        if record and kind in ('Spherical', 'Capsule', 'TaperedCapsule'):
            shape['radius1'] = float(limit.radius1)
        limits.append(shape)
    if record:
        recorded = {target['record_keys_by_index'][index]:
                    _runtime_vector(bone.location) + _runtime_vector(bone.pose_location)
                    for bone, (index, _, _) in zip(raw_bones, topology)
                    if index in target['record_keys_by_index']}
        return positions, limits, recorded
    return positions, limits


def _runtime_sample_nodes(state, actor_state=None):
    actor_state = actor_state if actor_state is not None else state
    if _runtime_world_info(_resolve_world(state['prefer_pie'])) != state['world']:
        raise RuntimeError('Sampling world changed, possibly because PIE ended.')
    by_id = {}
    for component, component_name in actor_state['components']:
        count, infos, error = unreal.KawaiiPhysicsLibrary.get_runtime_node_infos_on_component(
            component, state['filter_tags'], False)
        if count < 0:
            raise RuntimeError(str(error))
        for info in infos:
            if state['root_bone'] and str(info.root_bone) != state['root_bone']:
                continue
            identity = (component_name, str(info.anim_instance_class_name),
                        int(info.node_index), str(info.root_bone),
                        str(info.tag.get_editor_property('tag_name')))
            target = actor_state['targets_by_id'].get(identity)
            if target is not None:
                by_id[identity] = (_runtime_sample_info(
                    info, target, bool(actor_state.get('record_path')) and
                    state['frames_seen'] >= state['warmup_frames'])
                                   if info.evaluated else None)
    if not by_id:
        raise RuntimeError('Actor, component or KawaiiPhysics node disappeared.')
    return by_id


def _runtime_node_id(node):
    return {key: node[key] for key in (
        'component', 'anim_instance_class', 'node_index', 'root_bone', 'tag')}


def _runtime_world_info(world):
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    return {'name': str(world.get_name()),
            'is_pie': bool(editor is not None and editor.get_game_world() == world)}


def _runtime_snapshot(actor_label, prefer_pie, filter_tag_names, root_bone):
    if not isinstance(actor_label, str) or not actor_label:
        raise ValueError('actor_label must be a nonempty string.')
    if not isinstance(root_bone, str):
        raise ValueError('root_bone must be a string.')
    filter_tag_names = [str(t) for t in (filter_tag_names or [])]
    world = _resolve_world(prefer_pie)
    components = _find_skeletal_mesh_components_by_label(actor_label, prefer_pie)
    result = {'status': 'not_found' if not components else 'no_nodes',
              'checked': 0, 'excluded': 0, 'reasons': [],
              'world': _runtime_world_info(world), 'components': []}
    if not components:
        result['reasons'].append('Actor or SkeletalMeshComponent was not found.')
        return result
    tags = _make_tag_container(filter_tag_names)
    nodes = []
    for component in components:
        count, infos, error = unreal.KawaiiPhysicsLibrary.get_runtime_node_infos_on_component(
            component, tags, False)
        if count < 0:
            raise RuntimeError(str(error))
        converted = [_runtime_info_to_dict(info, component.get_name()) for info in infos]
        if root_bone:
            converted = [node for node in converted if node['root_bone'] == root_bone]
        result['components'].append({'component': str(component.get_name()), 'nodes': converted})
        nodes.extend(converted)
    if nodes:
        result['status'] = 'ok' if any(n['evaluated'] for n in nodes) else 'not_evaluated'
        if result['status'] == 'not_evaluated':
            result['reasons'].append('Matching nodes have not been evaluated.')
    else:
        result['reasons'].append('No matching KawaiiPhysics nodes.')
    return result


def _runtime_nodes(snapshot):
    return [node for component in snapshot['components'] for node in component['nodes']]


def _compact_runtime_bone_summary(node):
    bone_counts = {kind: 0 for kind in ('None', 'Tip', 'InterBone', 'Bridge')}
    for bone in node['bones']:
        kind = bone['dummy_type']
        bone_counts[kind] = bone_counts.get(kind, 0) + 1
    limit_counts = {}
    for limit in node['limits']:
        kind = limit['limit_type']
        limit_counts[kind] = limit_counts.get(kind, 0) + 1
    return {
        'node': _runtime_node_id(node),
        'evaluated': node['evaluated'],
        'total_bones': len(node['bones']),
        'bones_by_dummy_type': bone_counts,
        'constraint_count': len(node['constraints']),
        'limit_count': len(node['limits']),
        'limits_by_type': limit_counts,
    }


def _runtime_limit_id(limit):
    return {key: limit[key] for key in (
        'limit_type', 'source_type', 'source_array', 'source_index', 'driving_bone')}


def _runtime_limit_reasons(nodes):
    reasons = []
    convex = [(n['num_convex_limits'], n['convex_fallback_shape']) for n in nodes
              if n['num_convex_limits']]
    if convex:
        reasons.append(f'Convex shapes are not checked: {convex}.')
    reasons.append('Ordinary World Collision sweeps are outside this check.')
    if any(n['non_uniform_scale'] for n in nodes):
        reasons.append('Radii and sizes are approximate under non-uniform scale.')
    return reasons


def _collision_checked_bones(node):
    bones = [bone for bone in node['bones'] if not bone.get('skip_simulate', False)]
    return bones, len(node['bones']) - len(bones)


def _penetration_columns(node, ring_root_bones):
    """Return eligible bones, vertical pairs, and ordered ring columns."""
    bones = {bone['index']: bone for bone in node['bones']}
    eligible = {i: b for i, b in bones.items() if b['dummy_type'] in ('None', 'Tip')}
    children = defaultdict(list)
    vertical_pairs = []
    for bone in eligible.values():
        parent = bones.get(bone['parent_index'])
        while parent is not None and parent['dummy_type'] == 'InterBone':
            parent = bones.get(parent['parent_index'])
        if parent is not None and parent['index'] in eligible:
            children[parent['key']].append(bone['key'])
            vertical_pairs.append((parent['key'], bone['key']))
    columns = []
    for root in ring_root_bones:
        if not any(b['key'] == root and b['dummy_type'] == 'None' for b in eligible.values()):
            raise ValueError(f'Ring root bone not found: {root}')
        column = [root]
        while len(children[column[-1]]) == 1:
            column.append(children[column[-1]][0])
        columns.append(column)
    return bones, eligible, vertical_pairs, columns


def _build_penetration_segments(node, ring_root_bones, min_depth, max_depth, closed):
    """Build fixed, named centerlines from one snapshot without Unreal objects."""
    _, eligible, vertical_pairs, columns = _penetration_columns(node, ring_root_bones)
    keyed = {bone['key']: bone for bone in eligible.values()}
    segments = {}
    def add(a, b, category):
        if a == b or (keyed[a].get('skip_simulate', False) and
                      keyed[b].get('skip_simulate', False)):
            return
        pair = tuple(sorted((a, b)))
        segments.setdefault(pair, {'bone1': a, 'bone2': b, 'category': category})
    if not ring_root_bones:
        for parent, child in vertical_pairs:
            add(parent, child, 'vertical')
    else:
        for column in columns:
            end = len(column) - 1 if max_depth == -1 else min(max_depth, len(column) - 1)
            for depth in range(min_depth, end):
                add(column[depth], column[depth + 1], 'vertical')
        neighbors = [(i, i + 1) for i in range(len(columns) - 1)]
        if closed and len(columns) >= 3:
            neighbors.append((len(columns) - 1, 0))
        deepest = max(map(len, columns)) - 1 if max_depth == -1 else max_depth
        for depth in range(min_depth, deepest + 1):
            for i, j in neighbors:
                if depth < len(columns[i]) and depth < len(columns[j]):
                    add(columns[i][depth], columns[j][depth], 'horizontal')
    for constraint in node['constraints']:
        a, b = constraint['bone_index1'], constraint['bone_index2']
        if a in eligible and b in eligible:
            add(eligible[a]['key'], eligible[b]['key'], 'other')
    return list(segments.values())


def _record_node_header(node, ring_root_bones):
    bones, eligible, _, columns = _penetration_columns(node, ring_root_bones)
    depths = {key: depth for column in columns for depth, key in enumerate(column)}
    listed = []
    for bone in eligible.values():
        parent = bones.get(bone['parent_index'])
        while parent is not None and parent['index'] not in eligible:
            parent = bones.get(parent['parent_index'])
        listed.append({'key': bone['key'], 'bone_name': bone['bone_name'],
                       'dummy_type': bone['dummy_type'],
                       'parent_key': parent['key'] if parent else None,
                       'depth': depths.get(bone['key'], -1),
                       'radius': round(bone['radius'], 4),
                       'skip_simulate': bone.get('skip_simulate', False)})
    constraints, seen = [], set()
    for item in node['constraints']:
        a, b = item['bone_index1'], item['bone_index2']
        if a in eligible and b in eligible:
            pair = (eligible[a]['key'], eligible[b]['key'])
            if tuple(sorted(pair)) not in seen:
                constraints.append(pair)
                seen.add(tuple(sorted(pair)))
    return {'node': _runtime_node_id(node), 'columns': columns,
            'bones': listed, 'constraints': constraints,
            'rest': {bone['key']: bone['pose_location'] for bone in eligible.values()}}


def _build_penetration_targets(nodes, ring_root_bones, min_depth, max_depth, closed):
    """Select only nodes with both centerlines and collision-used limits."""
    targets, checked, excluded, reasons = [], 0, 0, []
    for node in nodes:
        try:
            segments = _build_penetration_segments(
                node, ring_root_bones, min_depth, max_depth, closed)
        except ValueError as error:
            reasons.append(str(error))
            continue
        excluded += sum(not limit['enabled'] for limit in node['limits']) * len(segments)
        if not segments or not any(limit['enabled'] for limit in node['limits']):
            continue
        identity = _runtime_node_id(node)
        endpoint_order = tuple(dict.fromkeys(
            key for segment in segments for key in (segment['bone1'], segment['bone2'])))
        targets.append({'node_id': tuple(identity.values()), 'identity': identity,
                        'segments': segments,
                        'segment_specs': [(segment['bone1'], segment['bone2'],
                                           segment['category'],
                                           (segment['bone1'], segment['bone2']))
                                          for segment in segments],
                        'endpoint_order': endpoint_order,
                        'endpoint_keys': set(endpoint_order),
                        'topology': tuple((bone['index'], bone['dummy_type'],
                                           bone['parent_index']) for bone in node['bones']),
                        'keys_by_index': {bone['index']: bone['key'] for bone in node['bones']
                                          if bone['key'] in endpoint_order},
                        'previous': None, 'unchanged_frames': 0, 'maxima': {}})
        checked += len(segments)
    return targets, checked, excluded, reasons, ('running' if targets else 'nothing_checked')


def _new_penetration_stats():
    return {category: {'max': 0.0, 'positive_sum': 0.0, 'positive_count': 0,
                       'samples_over_threshold': 0, 'samples': 0,
                       'stale_or_unknown': 0, 'worst': None, 'events': []}
            for category in ('vertical', 'horizontal', 'other', 'all')}


def _accumulate_penetration_sample(stats, category, value, threshold, stale, detail):
    """Count one segment/frame maximum once in its class and in all."""
    for name in (category, 'all'):
        entry = stats[name]
        if stale:
            entry['stale_or_unknown'] += 1
            continue
        entry['samples'] += 1
        if value > 0:
            entry['positive_sum'] += value
            entry['positive_count'] += 1
            if detail is not None and name == 'all':
                entry['events'].append(detail if 'value' in detail else {**detail, 'value': value})
        if value > threshold:
            entry['samples_over_threshold'] += 1
        if entry['worst'] is None or value > entry['max']:
            entry['max'], entry['worst'] = value, detail


def _penetration_stats_result(stats):
    return {name: {key: value for key, value in entry.items()
                   if key not in ('positive_sum', 'positive_count', 'events')} |
            {'positive_count': entry['positive_count'],
             'mean_all': (entry['positive_sum'] / entry['samples']
                          if entry['samples'] else 0.0),
             'mean_positive': (entry['positive_sum'] / entry['positive_count']
                               if entry['positive_count'] else 0.0)}
            for name, entry in stats.items()}


def _accumulate_penetration_frame(target, positions, limits, stats, threshold, frame,
                                  game_time=None):
    """Measure one node's fixed centerlines using plain Python data."""
    signature = (tuple(tuple(positions[key]) for key in target['endpoint_order'])
                 if all(key in positions for key in target['endpoint_order']) else None)
    if signature is not None and signature == target['previous']:
        target['unchanged_frames'] = target.get('unchanged_frames', 0) + 1
    target['previous'] = signature
    if not positions:
        for _, _, category, _ in target['segment_specs']:
            _accumulate_penetration_sample(stats, category, 0.0, threshold, True, None)
        return False
    bounded = []
    for limit in limits:
        kind = limit['limit_type']
        if kind == 'Spherical' and not limit['inner_sphere']:
            center, radius = limit['location'], limit['radius0']
        elif kind in ('Capsule', 'TaperedCapsule'):
            start, end = limit['start'], limit['end']
            center = ((start[0] + end[0]) * 0.5,
                      (start[1] + end[1]) * 0.5,
                      (start[2] + end[2]) * 0.5)
            radius = (math.dist(start, end) * 0.5 +
                      max(limit['radius0'], limit.get('radius1', limit['radius0'])))
        elif kind == 'Box':
            center, radius = limit['location'], _geom_norm(limit['extent'])
        else:
            center, radius = None, 0.0
        bounded.append((limit, center, radius))

    any_valid = False
    for bone1, bone2, category, key in target['segment_specs']:
        a, b = positions.get(bone1), positions.get(bone2)
        if a is None or b is None:
            _accumulate_penetration_sample(stats, category, 0.0, threshold, True, None)
            continue
        any_valid = True
        mx, my, mz = ((a[0] + b[0]) * 0.5, (a[1] + b[1]) * 0.5,
                      (a[2] + b[2]) * 0.5)
        half_length = math.dist(a, b) * 0.5
        value, best_limit = 0.0, limits[0] if limits else None
        for limit, center, radius in bounded:
            if center is not None:
                reach = half_length + radius
                dx, dy, dz = mx - center[0], my - center[1], mz - center[2]
                if dx*dx + dy*dy + dz*dz > reach*reach:
                    continue
            depth = _geom_segment_penetration(limit, a, b)
            if depth > value:
                value, best_limit = depth, limit
        class_stats, all_stats = stats[category], stats['all']
        detail = None
        if value > 0 or class_stats['worst'] is None or all_stats['worst'] is None:
            detail = {'node': target['identity'], 'bone1': bone1, 'bone2': bone2,
                       'category': category,
                       'limit': _runtime_limit_id(best_limit) if best_limit else None,
                      'frame': frame, 'time': game_time, 'value': value}
        _accumulate_penetration_sample(stats, category, value, threshold, False, detail)
        if value > target['maxima'].get(key, 0.0):
            target['maxima'][key] = value
    return any_valid


def _find_sampler_engine():
    engine = unreal.find_object(None, '/Engine/Transient.UnrealEdEngine_0')
    return engine or unreal.find_object(None, '/Engine/Transient.GameEngine_0')


def _restore_sampler_fixed_frame_rate(state):
    """Restore saved engine settings once, including after partial setup."""
    saved = state.pop('saved_engine_frame_rate', None)
    engine = state.pop('frame_rate_engine', None)
    if saved is None or engine is None:
        return
    for name, value in (('bUseFixedFrameRate', saved[0]),
                        ('FixedFrameRate', saved[1])):
        try:
            engine.set_editor_property(name, value)
        except Exception as error:
            state.setdefault('notes', []).append(
                f'Could not restore engine {name}: {error}')


def _enable_sampler_fixed_frame_rate(state, rate, engine=None):
    """Save and set both engine properties; leave the rate unset on failure."""
    if rate <= 0:
        return
    try:
        engine = engine if engine is not None else _find_sampler_engine()
        if engine is None:
            state.setdefault('notes', []).append('Engine was not found; fixed frame rate was not changed.')
            return
        saved = (engine.get_editor_property('bUseFixedFrameRate'),
                 engine.get_editor_property('FixedFrameRate'))
    except Exception as error:
        state.setdefault('notes', []).append(
            f'Could not read engine fixed frame rate; it was not changed: {error}')
        return
    state['frame_rate_engine'] = engine
    state['saved_engine_frame_rate'] = saved
    try:
        engine.set_editor_property('bUseFixedFrameRate', True)
        engine.set_editor_property('FixedFrameRate', rate)
    except Exception as error:
        _restore_sampler_fixed_frame_rate(state)
        state.setdefault('notes', []).append(
            f'Could not set engine fixed frame rate: {error}')
        return
    state['fixed_frame_rate'] = rate
    state.setdefault('notes', []).append(
        'Game time advanced at a fixed rate during sampling so the sampling cost does not change the physics step.')


def _stop_collision_penetration_sampler_impl():
    state = _COLLISION_PENETRATION_SAMPLER
    if state is None:
        return
    if state.get('done'):
        return
    handle = state.get('tick_handle')
    state['tick_handle'] = None
    try:
        if handle is not None:
            unreal.unregister_slate_post_tick_callback(handle)
    except Exception as error:
        state['error'] = state.get('error') or str(error)
    finally:
        _restore_sampler_fixed_frame_rate(state)
        for actor_state in state.get('actor_states', {state.get('actor'): state}).values():
            if actor_state.get('record_path') and actor_state.get('record_header'):
                try:
                    with open(actor_state['record_path'], 'w', encoding='utf-8') as output:
                        output.write(json.dumps(actor_state['record_header'], ensure_ascii=False) + '\n')
                        for frame in actor_state['record_frames']:
                            output.write(json.dumps(frame, ensure_ascii=False) + '\n')
                except Exception as error:
                    actor_state.setdefault('notes', []).append(
                        f'Could not write recording: {error}')
        state['done'] = True


def _record_transform(component, name):
    transform = component.get_socket_transform(
        name, unreal.RelativeTransformSpace.RTS_COMPONENT)
    location, rotation = transform.translation, transform.rotation
    return [round(float(value), 4) for value in (
        location.x, location.y, location.z,
        rotation.x, rotation.y, rotation.z, rotation.w)]


def _record_extra_bone_names(value):
    """Copy and validate Unreal arrays or other bone-name iterables."""
    if isinstance(value, str):
        raise ValueError('record_extra_bones must contain nonempty bone names.')
    try:
        names = list(value)
    except TypeError as error:
        raise ValueError('record_extra_bones must contain nonempty bone names.') from error
    if any(not isinstance(name, str) or not name for name in names):
        raise ValueError('record_extra_bones must contain nonempty bone names.')
    return names


def _record_penetration_frame(state, by_id, actor_state=None):
    actor_state = actor_state if actor_state is not None else state
    nodes, limits = [], []
    for target in actor_state['targets']:
        sample = by_id.get(target['node_id'])
        if sample is None:
            nodes.append({'bones': None, 'final': {}})
            continue
        _, shapes, recorded = sample
        component = target['component']
        final = {name: _record_transform(component, name)
                 for name in target['real_bones']}
        nodes.append({'bones': {key: [round(float(x), 4) for x in values]
                                for key, values in recorded.items()},
                      'final': final})
        target['record_started'] = True
        for shape in shapes:
            if shape['limit_type'] not in ('Capsule', 'TaperedCapsule', 'Spherical'):
                continue
            item = {'node': target['identity'], 'type': shape['limit_type'],
                    'driving_bone': shape['driving_bone'],
                    'radius0': round(shape['radius0'], 4),
                    'radius1': round(shape.get('radius1', shape['radius0']), 4)}
            for field in ('start', 'end', 'location'):
                if field in shape:
                    item[field] = [round(x, 4) for x in shape[field]]
            limits.append(item)
    extra = {name: _record_transform(actor_state['extra_components'][name], name)
             for name in actor_state['record_extra_bones']}
    positions = {(index, key): value[:3]
                 for index, node in enumerate(nodes) if node['bones'] is not None
                 for key, value in node['bones'].items()}
    positions.update({('extra', key): value[:3] for key, value in extra.items()})
    previous = actor_state.get('record_positions')
    stale = bool(positions and previous is not None and positions == previous)
    actor_state['record_positions'] = positions
    actor_state['record_frames'].append({'type': 'frame', 'frame': state['frames_seen'],
                                   'time': state['frame_time'], 'stale': stale,
                                   'nodes': nodes, 'extra': extra, 'limits': limits})


def _sampler_world_time_seconds(prefer_pie: bool) -> float:
    return float(unreal.GameplayStatics.get_time_seconds(_resolve_world(prefer_pie)))


def _accumulate_penetration_actor_tick(state, actor_state, by_id, game_time):
    """Accumulate one actor using the shared frame number and game time."""
    any_valid, all_unchanged = False, True
    for target in actor_state['targets']:
        sample = by_id.get(target['node_id'])
        positions, limits = sample[:2] if sample is not None else ({}, [])
        previous_unchanged = target['unchanged_frames']
        valid = _accumulate_penetration_frame(
            target, positions, limits, actor_state['stats'], state['threshold'],
            state['frames_seen'], game_time)
        any_valid |= valid
        all_unchanged &= valid and target['unchanged_frames'] > previous_unchanged
    if any_valid:
        actor_state['frames_collected'] += 1
        if actor_state.get('record_path'):
            _record_penetration_frame(state, by_id, actor_state)
    if all_unchanged:
        actor_state['unchanged_frames'] += 1
    return any_valid


def _collision_penetration_sampler_tick(delta_seconds):
    state = _COLLISION_PENETRATION_SAMPLER
    if state is None or state['done']:
        return
    started = time.perf_counter()
    try:
        actor_states = state.get('actor_states', {state.get('actor'): state})
        primary = actor_states[state['actor']]
        by_id = _runtime_sample_nodes(state, primary)
        state['frames_seen'] += 1
        if state['frames_seen'] <= state['warmup_frames']:
            return
        game_time = _sampler_world_time_seconds(state['prefer_pie'])
        state['frame_time'] = game_time
        _accumulate_penetration_actor_tick(state, primary, by_id, game_time)
        for label, actor_state in actor_states.items():
            if label == state['actor'] or not actor_state.get('targets') or actor_state['error']:
                continue
            try:
                extra_samples = _runtime_sample_nodes(state, actor_state)
                _accumulate_penetration_actor_tick(state, actor_state, extra_samples, game_time)
            except Exception as error:
                actor_state['error'] = f'{type(error).__name__}: {error}'
        if primary['frames_collected'] >= state['frames']:
            _stop_collision_penetration_sampler_impl()
    except Exception as error:
        state['error'] = f'{type(error).__name__}: {error}'
        _stop_collision_penetration_sampler_impl()
    finally:
        elapsed_ms = (time.perf_counter() - started) * 1000.0
        state['cost_total_ms'] += elapsed_ms
        state['cost_max_ms'] = max(state['cost_max_ms'], elapsed_ms)
        state['cost_samples'] += 1


def _penetration_actor_result(state, actor_state):
    rows = []
    for target in actor_state['targets']:
        for segment in target['segments']:
            key = (segment['bone1'], segment['bone2'])
            rows.append({'node': target['identity'], **segment,
                         'max': target['maxima'].get(key, 0.0)})
    rows.sort(key=lambda row: row['max'], reverse=True)
    stats = _penetration_stats_result(actor_state['stats'])
    events = sorted(actor_state['stats']['all'].get('events', []),
                    key=lambda event: event['value'], reverse=True)
    top_events = []
    for event in events:
        if all(abs(event['frame'] - selected['frame']) >= 5
               for selected in top_events):
            top_events.append(event)
            if len(top_events) == 10:
                break
    status = ('error' if actor_state['error'] or state['error'] else
              'running' if not state['done'] else
              'stopped' if actor_state['frames_collected'] < state['frames'] else
              'penetration' if stats['all']['samples_over_threshold'] else
              'ok' if stats['all']['samples'] else 'nothing_checked')
    result = {'status': status, 'checked': stats['all']['samples'],
              'excluded': actor_state['excluded'], 'reasons': actor_state['reasons'],
              'world': actor_state['world'], 'done': state['done'],
              'frames_collected': actor_state['frames_collected'],
              'unchanged_frames': actor_state['unchanged_frames'],
              'frames_seen': state['frames_seen'], 'frames_requested': state['frames'],
              'threshold': state['threshold'], 'statistics': stats,
              'top_segments': rows[:10], 'top_events': top_events,
              'fixed_frame_rate': state['fixed_frame_rate'],
              'cost_ms': {'mean': (state['cost_total_ms'] / state['cost_samples']
                                   if state['cost_samples'] else 0.0),
                          'max': state['cost_max_ms']},
              'metric': 'At each sample, maximum depth (cm, component space) that an active limit enters each segment centerline. This is a bone-layout proxy, not cloth-mesh penetration.',
              'notes': [
                  'Sampling adds per-frame cost; compare runs under the same frame rate.',
                  'Many unchanged_frames may mean the mesh is off-screen or throttled and is not updating.',
                  'A physics-settings radius multiplier during sampling can change the comparison.',
                  'The 1.0 cm threshold is a guide, not a pass criterion.',
                  'For Off/On comparison, use the same motion, start phase, duration and warmup.',
                  *state['notes'],
                  *(actor_state.get('notes', []) if actor_state is not state else [])]}
    if actor_state['error'] or state['error']:
        result['error'] = actor_state['error'] or state['error']
    if actor_state.get('record_path'):
        result['record_path'] = actor_state['record_path']
        result['recorded_frames'] = len(actor_state['record_frames'])
    return result


def _penetration_sampler_result(state):
    if 'failed_start' in state:
        return state['failed_start']
    actor_states = state.get('actor_states')
    if not actor_states:
        return _penetration_actor_result(state, state)
    def entry(actor_state):
        if actor_state.get('targets'):
            return _penetration_actor_result(state, actor_state)
        result = dict(actor_state['start_result'])
        result.update(done=state.get('done', True), frames_collected=0,
                      unchanged_frames=0, frames_seen=state.get('frames_seen', 0),
                      frames_requested=state.get('frames'),
                      threshold=state.get('threshold'),
                      fixed_frame_rate=state.get('fixed_frame_rate'),
                      statistics=_penetration_stats_result(
                          actor_state.get('stats', _new_penetration_stats())),
                      top_segments=[], top_events=[],
                      cost_ms={'mean': (state['cost_total_ms'] / state['cost_samples']
                                        if state.get('cost_samples') else 0.0),
                               'max': state.get('cost_max_ms', 0.0)},
                      notes=[*state.get('notes', []), *actor_state.get('notes', [])])
        if actor_state.get('record_path'):
            result['record_path'] = actor_state['record_path']
            result['recorded_frames'] = len(actor_state['record_frames'])
        return result
    result = entry(actor_states[state['actor']])
    result['actors'] = {label: entry(actor_state)
                        for label, actor_state in actor_states.items()}
    return result


def _make_record_header(actor_label, world, warmup_frames, ring_root_bones,
                        record_extra_bones, closed):
    """Build a version-one motion recording header."""
    return {'type': 'header', 'version': 1, 'actor': actor_label, 'world': world,
            'fixed_frame_rate': None, 'warmup_frames': warmup_frames,
            'ring_root_bones': ring_root_bones, 'nodes': [],
            'extra_bones': record_extra_bones, 'closed': closed}


def _prepare_penetration_actor(actor_label, prefer_pie, filter_tag_names, root_bone,
                               ring_root_bones, min_depth, max_depth, closed,
                               record_path, record_extra_bones, warmup_frames):
    """Build independent targets, statistics, and recording data for one actor."""
    snapshot = _runtime_snapshot(actor_label, prefer_pie, filter_tag_names, root_bone)
    result = {key: snapshot[key] for key in ('status', 'checked', 'excluded', 'reasons', 'world')}
    result['fixed_frame_rate'] = None
    actor_state = {'actor': actor_label, 'world': result['world'], 'targets': [],
                   'stats': _new_penetration_stats(), 'frames_collected': 0,
                   'unchanged_frames': 0, 'checked': 0, 'excluded': result['excluded'],
                   'reasons': result['reasons'], 'error': '', 'notes': [],
                   'start_result': result}
    if record_path:
        actor_state.update(record_path=record_path, record_extra_bones=record_extra_bones,
                           record_header=_make_record_header(
                               actor_label, result['world'], warmup_frames,
                               ring_root_bones, record_extra_bones, closed),
                           record_frames=[], extra_components={})
        result['record_path'] = record_path
        result['recorded_frames'] = 0
    if snapshot['status'] in ('not_found', 'no_nodes', 'not_evaluated'):
        return actor_state
    nodes = [node for node in _runtime_nodes(snapshot) if node['evaluated']]
    targets, checked, excluded, reasons, start_status = _build_penetration_targets(
        nodes, ring_root_bones, min_depth, max_depth, closed)
    result['checked'] = checked
    result['excluded'] += excluded
    result['reasons'].extend(reasons)
    result['reasons'].extend(_runtime_limit_reasons(nodes))
    if result['excluded']:
        result['reasons'].append('Disabled limits were excluded.')
    actor_state.update(targets=targets, checked=checked, excluded=result['excluded'])
    if start_status == 'nothing_checked':
        result['status'] = start_status
        result['reasons'].append('No eligible centerlines and active limits were available.')
        return actor_state
    components = [(component, str(component.get_name())) for component in
                  _find_skeletal_mesh_components_by_label(actor_label, prefer_pie)]
    actor_state.update(components=components,
                       targets_by_id={target['node_id']: target for target in targets})
    components_by_name = {name: component for component, name in components}
    target_component_names = {target['identity']['component'] for target in targets}
    component_sockets = [(component, {str(socket) for socket in
                                      component.get_all_socket_names()})
                         for component, name in components
                         if name in target_component_names] if record_extra_bones else []
    extra_components = {}
    for name in record_extra_bones:
        component = next((component for component, sockets in component_sockets
                          if name in sockets), None)
        if component is None:
            raise ValueError(f'record_extra_bones bone not found: {name}')
        extra_components[name] = component
    if record_path:
        nodes_by_id = {tuple(_runtime_node_id(node).values()): node for node in nodes}
        for target in targets:
            node = nodes_by_id[target['node_id']]
            target['component'] = components_by_name[target['identity']['component']]
            target['real_bones'] = tuple(dict.fromkeys(
                bone['bone_name'] for bone in node['bones']
                if bone['dummy_type'] == 'None'))
            target['record_keys_by_index'] = {
                bone['index']: bone['key'] for bone in node['bones']
                if bone['dummy_type'] in ('None', 'Tip')}
        record_header = _make_record_header(
            actor_label, result['world'], warmup_frames, ring_root_bones,
            record_extra_bones, closed)
        record_header['nodes'] = [
            _record_node_header(nodes_by_id[target['node_id']], ring_root_bones)
            for target in targets]
        for target, header_node in zip(targets, record_header['nodes']):
            target['record_header_node'] = header_node
            target['ring_root_bones'] = ring_root_bones
        actor_state.update(record_path=record_path, record_extra_bones=record_extra_bones,
                           record_header=record_header, record_frames=[],
                           extra_components=extra_components)
    result['status'] = start_status
    return actor_state


def _extra_record_path(record_path, label, used_paths):
    stem, extension = os.path.splitext(record_path)
    safe_label = re.sub(r'[^A-Za-z0-9_-]+', '_', label).strip('_') or 'actor'
    path = f'{stem}__{safe_label}{extension}'
    suffix = 2
    while os.path.normcase(path) in used_paths:
        path = f'{stem}__{safe_label}__{suffix}{extension}'
        suffix += 1
    used_paths.add(os.path.normcase(path))
    return path


# UE 5.8 の MCP は省略可能なリスト引数（list[...] | None）に値を渡すと変換に失敗するため、リスト引数は省略不可の list にする（指定しないときは [] を渡す）
@unreal.uclass()
class KawaiiPhysicsToolset(unreal.ToolsetDefinition):
    """Sets up, applies presets to, and audits KawaiiPhysics AnimGraph nodes.

    Tools wrap KawaiiPhysics editor scripting APIs for automation agents.
    """

    @toolset_registry.tool_call
    @staticmethod
    def build_ring_bone_constraints(
            skeletal_mesh: unreal.SkeletalMesh,
            ring_root_bones: list[str],
            min_depth: int = 1,
            max_depth: int = -1,
            closed: bool = True,
            compliance_type: str | None = None,
            exclude_from_subdivision: bool = False) -> str:
        """Build neighboring skirt-column constraints from roots in ring order.

        Pass the returned pairs directly to set_bone_constraints_data_asset_pairs.
        Constraints preserve distance between columns; BoneConstraintSubdivisionCount
        adds collision dummies along constraint lines.
        """
        # MCP スキーマで任意引数にするため None を受け、未指定は従来の既定値として扱う
        compliance_type = compliance_type or ''
        _raise_for_invalid_object(skeletal_mesh, 'skeletal_mesh')
        if not ring_root_bones or any(not isinstance(b, str) or not b.strip()
                                      for b in ring_root_bones):
            raise ValueError('ring_root_bones must contain nonempty bone names.')
        if len(set(ring_root_bones)) != len(ring_root_bones):
            raise ValueError('ring_root_bones must be unique.')
        if min_depth < 0 or (max_depth != -1 and max_depth < min_depth):
            raise ValueError('Invalid depth range.')
        if compliance_type != '' and compliance_type not in _COMPLIANCE_TYPES:
            raise ValueError(f'Invalid compliance_type: {compliance_type}')
        modifier = _bone_children_from_mesh(skeletal_mesh)
        all_names = {str(name) for name in modifier.get_all_bone_names()}
        unknown = [name for name in ring_root_bones if name not in all_names]
        if unknown:
            raise ValueError(f'Bones not found in skeleton: {unknown}')
        children = _collect_children(modifier, ring_root_bones)
        return json.dumps(_build_ring_constraints(
            children, ring_root_bones, min_depth, max_depth, closed,
            compliance_type, exclude_from_subdivision))

    @toolset_registry.tool_call
    @staticmethod
    def set_bone_constraints_data_asset_pairs(
            data_asset: unreal.KawaiiPhysicsBoneConstraintsDataAsset,
            pairs_json: str,
            mode: str = 'replace',
            dry_run: bool = False) -> str:
        """Replace or append validated constraint pairs in a shared DataAsset.

        This tool does not clear all pairs; an empty request is rejected.
        """
        _raise_for_invalid_object(data_asset, 'data_asset')
        if mode not in ('replace', 'append'):
            raise ValueError('mode must be replace or append.')
        requested = _validate_constraint_pairs(pairs_json)
        if not requested:
            raise ValueError('pairs must contain at least one constraint; this tool does not clear the DataAsset.')
        previous = list(data_asset.get_editor_property('bone_constraints_data'))
        before = [_constraint_data_to_pair(item) for item in previous]
        previous_keys = {_constraint_pair_key(pair) for pair in before}
        if mode == 'replace':
            final = [_constraint_pair_to_data(pair) for pair in requested]
            added = [pair for pair in requested if _constraint_pair_key(pair) not in previous_keys]
            new_keys = {_constraint_pair_key(pair) for pair in requested}
            removed = [pair for pair in before if _constraint_pair_key(pair) not in new_keys]
            skipped = []
        else:
            added = [pair for pair in requested if _constraint_pair_key(pair) not in previous_keys]
            skipped = [pair for pair in requested if _constraint_pair_key(pair) in previous_keys]
            removed = []
            final = previous + [_constraint_pair_to_data(pair) for pair in added]
        package_name = _asset_package_path(data_asset.get_path_name())
        registry = unreal.AssetRegistryHelpers.get_asset_registry()
        referencers = [str(name) for name in (registry.get_referencers(
            unreal.Name(package_name), unreal.AssetRegistryDependencyOptions(
                include_hard_package_references=True,
                include_soft_package_references=True)) or [])]
        if not dry_run:
            with unreal.ScopedEditorTransaction('Set KawaiiPhysics bone constraint pairs'):
                data_asset.modify()
                data_asset.set_editor_property('bone_constraints_data', final)
        return json.dumps({
            'mode': mode, 'dry_run': dry_run, 'before_count': len(before),
            'after_count': len(final), 'added': added, 'removed': removed,
            'skipped_existing': skipped, 'referencers': referencers,
            'note': 'This is a shared asset and affects every referencing node. '
                    'Compile the AnimBlueprint and restart PIE to apply changes.'})

    @toolset_registry.tool_call
    @staticmethod
    def set_graph_node_radius_by_depth(
            handle: unreal.KawaiiPhysicsGraphNodeHandle,
            radius_by_depth: list[float],
            skeletal_mesh: unreal.SkeletalMesh,
            dry_run: bool = False) -> str:
        """Fit a linear radius curve to per-depth skirt-column radii using the required skeletal mesh.

        The prediction uses all mesh bones, so a LOD that removes bones can change the actual LengthRate; check describe_kawaii_physics_bones_on_actor length_rate.
        """
        _raise_for_invalid_handle(handle, 'handle')
        _raise_for_invalid_object(skeletal_mesh, 'skeletal_mesh')
        read = lambda name: KawaiiPhysicsToolset.get_graph_node_property(handle, name)
        curve_old = read('RadiusCurveData')
        external_curve = _import_field(curve_old, 'ExternalCurve')
        if external_curve and external_curve.strip().strip(chr(34)).strip(chr(39)).lower() != 'none':
            raise RuntimeError('RadiusCurveData has an ExternalCurve; internal keys have no effect.')
        settings_old = read('PhysicsSettings')
        root_names = _import_bone_names(read('RootBone'))
        if not root_names:
            raise ValueError('RootBone is not set.')
        base_excludes = set(_import_bone_names(read('ExcludeBones')))
        roots_and_excludes = [(root_names[0], base_excludes)]
        for item in _import_struct_items(read('AdditionalRootBones')):
            root_field = _import_field(item, 'RootBone')
            names = _import_bone_names(root_field)
            if not names:
                continue
            override = _import_field(item, 'bUseOverrideExcludeBones') == 'True'
            excludes = set(_import_bone_names(_import_field(item, 'OverrideExcludeBones'))) if override else base_excludes
            roots_and_excludes.append((names[0], excludes))
        dummy_length = float(read('DummyBoneLength'))
        modifier = _bone_children_from_mesh(skeletal_mesh)
        all_names = {str(name) for name in modifier.get_all_bone_names()}
        unknown = [root for root, _ in roots_and_excludes if root not in all_names]
        if unknown:
            raise ValueError(f'Root bones not found in skeleton: {unknown}')
        children = _collect_children(modifier, [root for root, _ in roots_and_excludes])
        local_lengths = {}
        for bone in children:
            transform = modifier.get_bone_transform(unreal.Name(bone), False)
            translation = transform.get_editor_property('translation')
            local_lengths[bone] = math.sqrt(sum(float(getattr(translation, axis))**2
                                                for axis in ('x', 'y', 'z')))
        chains = _radius_chain_rates(children, local_lengths, roots_and_excludes,
                                     dummy_length)
        radius, keys, bones, warnings = _radius_keys_and_bones(
            chains, radius_by_depth, dummy_length)
        for name in ('BoneSubdivisionCount', 'bBoneSubdivisionDensifyByRadius',
                     'BoneConstraintSubdivisionCount'):
            value = read(name)
            if value not in ('0', 'False', 'false'):
                warnings.append(f'{name} is enabled; dummy counts or radii may change after reinitialization.')
        key_text = ','.join('(InterpMode=RCIM_Linear,Time=%.9g,Value=%.9g)' %
                            (key['time'], key['value']) for key in keys)
        curve_new = ('(EditorCurveData=(Keys=(' + key_text + '),'
                     'DefaultValue=340282346638528859811704183484516925440.000000,'
                     'PreInfinityExtrap=RCCE_Constant,PostInfinityExtrap=RCCE_Constant),'
                     'ExternalCurve=None)')
        if re.search(r'(?<![A-Za-z])Radius\s*=\s*[^,)]+', settings_old):
            settings_new = re.sub(r'(?<![A-Za-z])Radius\s*=\s*[^,)]+',
                                  f'Radius={radius:.9g}', settings_old, count=1)
        else:
            separator = ',' if settings_old.strip() != '()' else ''
            settings_new = settings_old[:-1] + f'{separator}Radius={radius:.9g})'
        if not dry_run:
            with unreal.ScopedEditorTransaction('Set KawaiiPhysics radius by depth'):
                KawaiiPhysicsToolset.set_graph_node_property(handle, 'PhysicsSettings', settings_new)
                try:
                    KawaiiPhysicsToolset.set_graph_node_property(handle, 'RadiusCurveData', curve_new)
                except Exception as error:
                    KawaiiPhysicsToolset.set_graph_node_property(handle, 'PhysicsSettings', settings_old)
                    raise RuntimeError('Unable to set RadiusCurveData; PhysicsSettings restored.') from error
        chain_info = [{'root': chain['root'],
                       'bone_count': sum(r['bone'] is not None for r in chain['records']),
                       'length_rates': [r['length_rate'] for r in chain['records'] if r['bone'] is not None]}
                      for chain in chains]
        return json.dumps({'radius': radius, 'keys': keys, 'bones': bones,
                           'chains': chain_info, 'warnings': warnings,
                           'dry_run': dry_run,
                           'next_steps': 'Compile the AnimBlueprint and restart PIE.'})

    # ===== Editor: AnimBlueprint / preset / audit =====

    @toolset_registry.tool_call
    @staticmethod
    def create_anim_blueprint(
            folder_path: str,
            asset_name: str,
            skeleton: unreal.Skeleton,
            input_animation: unreal.AnimSequenceBase | None = None) -> unreal.AnimBlueprint:
        """Creates an AnimBlueprint with a target skeleton.

        A non-None input_animation is set as the AnimGraph input pose in the
        same way as set_anim_graph_input_animation.
        """
        _raise_for_invalid_object(skeleton, 'skeleton')

        factory = unreal.AnimBlueprintFactory()
        factory.set_editor_property('target_skeleton', skeleton)
        factory.set_editor_property('parent_class', unreal.AnimInstance.static_class())

        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            asset_name,
            folder_path,
            unreal.AnimBlueprint,
            factory,
        )
        if asset is None:
            raise RuntimeError(
                f'Unable to create AnimBlueprint {asset_name} at {folder_path}.')
        if not isinstance(asset, unreal.AnimBlueprint):
            raise RuntimeError(f'Created asset is not an AnimBlueprint: {asset}')
        if input_animation is not None:
            _set_anim_graph_input_animation_impl(asset, input_animation, '')
        return asset

    @toolset_registry.tool_call
    @staticmethod
    def create_post_process_anim_blueprint(
            skeleton: unreal.Skeleton,
            package_path: str,
            asset_name: str) -> unreal.AnimBlueprint:
        """Creates an AnimBlueprint whose AnimGraph starts with an Input Pose.

        Add KawaiiPhysics with add_kawaii_physics_node, call
        compile_anim_blueprint, then use set_post_process_anim_blueprint to
        assign it to a SkeletalMesh.
        """
        _raise_for_invalid_object(skeleton, 'skeleton')
        asset = KawaiiPhysicsToolset.create_anim_blueprint(
            package_path, asset_name, skeleton)
        count, error = _unpack_count_and_out(
            unreal.KawaiiPhysicsEditorLibrary.set_anim_graph_input_pose(asset))
        if count < 0:
            raise ValueError(f'Unable to set AnimGraph Input Pose: {error}')
        return asset

    @toolset_registry.tool_call
    @staticmethod
    def set_post_process_anim_blueprint(
            skeletal_mesh: unreal.SkeletalMesh,
            anim_blueprint: unreal.AnimBlueprint | None = None) -> bool:
        """Assigns the generated class to a SkeletalMesh post process slot and returns true.

        Pass None to clear the slot. The value is the AnimBlueprint generated
        class, not the AnimBlueprint asset. This does not save the mesh.
        Raises ValueError without changing the mesh unless the AnimBlueprint
        targets the mesh skeleton and its AnimGraph input is an Input Pose
        named InPose (as made by create_post_process_anim_blueprint).
        """
        _raise_for_invalid_object(skeletal_mesh, 'skeletal_mesh')
        # MCP スキーマで任意引数にするため None を受け、未指定は解除として扱う
        if anim_blueprint is not None:
            _raise_for_invalid_object(anim_blueprint, 'anim_blueprint')
            # 互換スケルトン判定は Python に公開されていないため、同一スケルトンだけを受け付ける
            mesh_skeleton = skeletal_mesh.get_editor_property('skeleton')
            target_skeleton = anim_blueprint.get_editor_property('target_skeleton')
            if mesh_skeleton is None or target_skeleton != mesh_skeleton:
                raise ValueError(
                    f'AnimBlueprint target skeleton {target_skeleton} does not match '
                    f'the SkeletalMesh skeleton {mesh_skeleton}.')
            # InPose が無いと Post Process インスタンスがメッシュのポーズを受け取れない
            if not unreal.KawaiiPhysicsEditorLibrary.is_anim_graph_input_pose_connected(
                    anim_blueprint):
                raise ValueError(
                    'AnimGraph input is not connected to an Input Pose named InPose. '
                    'Use create_post_process_anim_blueprint to make a post process AnimBlueprint.')
        skeletal_mesh.set_editor_property(
            'post_process_anim_blueprint',
            anim_blueprint.generated_class() if anim_blueprint else None)
        return True

    @toolset_registry.tool_call
    @staticmethod
    def set_anim_graph_input_animation(
            anim_blueprint: unreal.AnimBlueprint,
            animation: unreal.AnimSequenceBase,
            graph_name: str | None = None) -> None:
        """Sets the animation that feeds the AnimGraph pose chain; does not compile.

        Follows pose inputs upstream from Result (through KawaiiPhysics and
        space conversion nodes). An existing SequencePlayer gets the animation;
        otherwise a SequencePlayer is added at the first unlinked pose input.
        Works before or after adding KawaiiPhysics nodes. The animation skeleton
        must match the AnimBlueprint target skeleton.
        """
        # MCP スキーマで任意引数にするため None を受け、未指定は従来の既定値として扱う
        graph_name = graph_name or ''
        _set_anim_graph_input_animation_impl(anim_blueprint, animation, graph_name)

    @toolset_registry.tool_call
    @staticmethod
    def compile_anim_blueprint(
            anim_blueprint: unreal.AnimBlueprint) -> list[str]:
        """Compiles an AnimBlueprint and returns the compiler messages.

        Each message starts with "Error:", "Warning:" or "Note:"; compile errors
        are returned as messages instead of raising.
        """
        _raise_for_invalid_object(anim_blueprint, 'anim_blueprint')

        error_count, messages = _unpack_count_and_out(
            unreal.KawaiiPhysicsEditorLibrary.compile_anim_blueprint_with_messages(
                anim_blueprint))
        if error_count < 0:
            raise RuntimeError('Unable to compile AnimBlueprint.')
        return [str(message) for message in messages]

    @toolset_registry.tool_call
    @staticmethod
    def add_kawaii_physics_node(
            anim_blueprint: unreal.AnimBlueprint,
            root_bone: str,
            exclude_bones: list[str] = [],
            additional_root_bones: list[str] = [],
            tag: str | None = None,
            properties_json: str | None = None,
            graph_name: str | None = None,
            comment: str | None = None,
            prompt: str | None = None) -> unreal.KawaiiPhysicsGraphNodeHandle:
        """Adds one KawaiiPhysics node auto-positioned and auto-connected before Result.

        Always adds a new node; the pose chain upstream of Result is then laid
        out on one row as in layout_anim_graph. tag is a registered gameplay tag name.
        properties_json is applied like set_graph_node_properties; if a property
        fails the node stays in the graph. comment/prompt behave as in
        add_kawaii_physics_nodes.
        """
        # MCP スキーマで任意引数にするため None を受け、未指定は従来の既定値として扱う
        exclude_bones = exclude_bones or []
        additional_root_bones = additional_root_bones or []
        tag = tag or ''
        properties_json = properties_json or ''
        graph_name = graph_name or ''
        comment = comment or ''
        prompt = prompt or ''
        _raise_for_invalid_object(anim_blueprint, 'anim_blueprint')
        if not root_bone:
            raise ValueError('root_bone must not be empty.')
        # ノード追加前に JSON を検証し、書式エラーでノードだけ残らないようにする
        properties = (
            _parse_graph_node_properties(properties_json)
            if properties_json else []
        )

        request = unreal.KawaiiPhysicsNodePlacementRequest()
        request.set_editor_property('preset', None)
        request.set_editor_property('root_bone_name', unreal.Name(root_bone))
        request.set_editor_property(
            'exclude_bone_names',
            [unreal.Name(bone_name) for bone_name in exclude_bones],
        )
        request.set_editor_property(
            'additional_root_bones',
            [_make_root_bone_setting(bone_name) for bone_name in additional_root_bones],
        )
        if tag:
            request.set_editor_property('kawaii_physics_tag', _resolve_gameplay_tag(tag))
        # C++ プロパティ名 bAutoPosition / bAutoConnect は Python では auto_position / auto_connect に変換される。
        request.set_editor_property('auto_position', True)
        request.set_editor_property('auto_connect', True)

        _validate_requests_or_raise(anim_blueprint, [request])
        handles = unreal.KawaiiPhysicsEditorLibrary.add_kawaii_physics_nodes(
            anim_blueprint,
            [request],
            unreal.KawaiiPhysicsPlacementMatchKey.NONE,
            graph_name,
            comment,
            prompt,
        )
        if not handles:
            raise RuntimeError(
                f'Unable to add KawaiiPhysics node for RootBone: {root_bone}')
        handle = handles[0]

        if properties:
            _set_graph_node_properties_impl(handle, properties)
        return handle

    @toolset_registry.tool_call
    @staticmethod
    def add_kawaii_physics_nodes(
            anim_blueprint: unreal.AnimBlueprint,
            requests: list[unreal.KawaiiPhysicsNodePlacementRequest],
            match_key: unreal.KawaiiPhysicsPlacementMatchKey,
            graph_name: str,
            comment: str | None = None,
            prompt: str | None = None) -> list[unreal.KawaiiPhysicsGraphNodeHandle]:
        """Adds or updates KawaiiPhysics nodes; auto_connect wires before Result.

        A non-empty comment creates an MCP comment frame with the configured
        prefix. The prompt is stored in that frame's Details. Requests may set
        placement_direction; project settings also control node direction/wrap/spacing.
        When a request sets both auto_position and auto_connect, the nodes are
        finally laid out with the upstream pose chain on Result's row, as in
        layout_anim_graph (placement_direction then applies only to unconnected nodes).
        """
        # MCP スキーマで任意引数にするため None を受け、未指定は従来の既定値として扱う
        comment = comment or ''
        prompt = prompt or ''
        _raise_for_invalid_object(anim_blueprint, 'anim_blueprint')
        _validate_requests_or_raise(anim_blueprint, requests)

        return unreal.KawaiiPhysicsEditorLibrary.add_kawaii_physics_nodes(
            anim_blueprint,
            requests,
            match_key,
            graph_name,
            comment,
            prompt,
        )

    @toolset_registry.tool_call
    @staticmethod
    def layout_anim_graph(
            anim_blueprint: unreal.AnimBlueprint,
            graph_name: str | None = None) -> None:
        """Lays out the AnimGraph pose chain on one row; does not compile.

        Follows the first linked pose input of each node upstream from Result
        and places that chain left to right on Result's row (Result stays put,
        spacing follows node widths). Nodes off the chain are not moved. MCP
        comment frames are refit around their KawaiiPhysics nodes. graph_name
        defaults to the main AnimGraph.
        """
        _raise_for_invalid_object(anim_blueprint, 'anim_blueprint')
        # str | None は ToolsetRegistry のスキーマで任意引数になる（str = '' は既定値が伝わらず必須扱い）
        ok = unreal.KawaiiPhysicsEditorLibrary.layout_kawaii_physics_anim_graph(
            anim_blueprint,
            graph_name or '',
        )
        if not ok:
            raise RuntimeError(
                'Unable to lay out the AnimGraph. Check that the graph exists and '
                'has a Result node; see the output log for details.')

    @toolset_registry.tool_call
    @staticmethod
    def get_anim_graph_comments(
            anim_blueprint: unreal.AnimBlueprint,
            graph_name: str | None = None) -> list[unreal.KawaiiPhysicsAnimGraphCommentInfo]:
        """Returns comment nodes in the AnimGraph with node, position and size."""
        # MCP スキーマで任意引数にするため None を受け、未指定は従来の既定値として扱う
        graph_name = graph_name or ''
        _raise_for_invalid_object(anim_blueprint, 'anim_blueprint')
        return unreal.KawaiiPhysicsEditorLibrary.get_anim_graph_comments(
            anim_blueprint,
            graph_name,
        )

    @toolset_registry.tool_call
    @staticmethod
    def collect_kawaii_physics_graph_nodes(
            anim_blueprint: unreal.AnimBlueprint,
            filter_tag_names: list[str],
            filter_exact_match: bool) -> list[unreal.KawaiiPhysicsGraphNodeHandle]:
        """Collects KawaiiPhysics graph nodes; empty tag names collect all nodes."""
        return _collect_graph_nodes_impl(
            anim_blueprint,
            filter_tag_names,
            filter_exact_match,
        )

    @toolset_registry.tool_call
    @staticmethod
    def is_graph_node_handle_valid(
            handle: unreal.KawaiiPhysicsGraphNodeHandle) -> bool:
        """Returns whether a KawaiiPhysics graph node handle is valid."""
        return (
            handle is not None and
            unreal.KawaiiPhysicsEditorLibrary.is_graph_node_handle_valid(handle)
        )

    @toolset_registry.tool_call
    @staticmethod
    def set_graph_node_property(
            handle: unreal.KawaiiPhysicsGraphNodeHandle,
            property_name: str,
            value: str) -> None:
        """Sets a graph node property from a string value.

        ExternalForces is not accessible here; use set_graph_node_external_forces
        for ExternalForces and add_collision_limit to place a collision limit
        in component space.
        """
        _raise_for_invalid_handle(handle, 'handle')
        if not property_name:
            raise ValueError('property_name must not be empty.')
        if value is None:
            raise ValueError('value must not be None.')

        ok = unreal.KawaiiPhysicsEditorLibrary.set_graph_node_property_from_string(
            handle,
            unreal.Name(property_name),
            value,
        )
        if not ok:
            raise RuntimeError(
                f'Unable to set KawaiiPhysics graph node property: {property_name}')

    @toolset_registry.tool_call
    @staticmethod
    def set_graph_node_properties(
            handle: unreal.KawaiiPhysicsGraphNodeHandle,
            properties_json: str) -> list[str]:
        """Sets several FAnimNode_KawaiiPhysics properties in order and returns the names set.

        properties_json is a JSON object {"<property name>": value}; a value is
        an ImportText string such as "(Damping=0.3,Stiffness=0.1)", a number or
        a bool. Each write is read back; an unchanged value after a different
        request raises with the property name. Earlier writes stay set on error.
        """
        _raise_for_invalid_handle(handle, 'handle')
        properties = _parse_graph_node_properties(properties_json)
        return _set_graph_node_properties_impl(handle, properties)

    @toolset_registry.tool_call
    @staticmethod
    def get_graph_node_property(
            handle: unreal.KawaiiPhysicsGraphNodeHandle,
            property_name: str) -> str:
        """Gets a graph node property as a string value.

        Use get_graph_node_external_forces for ExternalForces.
        """
        _raise_for_invalid_handle(handle, 'handle')
        if not property_name:
            raise ValueError('property_name must not be empty.')

        value = unreal.KawaiiPhysicsEditorLibrary.get_graph_node_property_as_string(
            handle,
            unreal.Name(property_name),
        )
        if value is None:
            raise RuntimeError(
                f'Unable to get KawaiiPhysics graph node property: {property_name}')
        return str(value)

    @toolset_registry.tool_call
    @staticmethod
    def add_shared_publisher_node(
            anim_blueprint: unreal.AnimBlueprint,
            shared_group_tag: str = 'KawaiiPhysics.Shared.Default',
            reuse_existing: bool = True,
            properties_json: str | None = None) -> unreal.KawaiiPhysicsSharedPublisherGraphNodeHandle:
        """Adds a KawaiiPhysics Shared Publisher node auto-connected before Result.

        The pass-through node publishes shared ProceduralWind parameters, phase
        and gusts plus Simple World Collision gather results to KawaiiPhysics
        nodes in the same actor family whose WindSource /
        SimpleWorldCollisionSource is Shared or Auto with the same tag. Place it
        where the AnimGraph always updates (e.g. just before Output Pose).
        With reuse_existing, an existing node with the same tag is returned
        (properties_json is still applied to it). properties_json is applied
        like set_graph_node_properties; SharedWind is an ImportText string such
        as "(ParameterMode=Advanced,ConstantForce=8.0,SwayForce=12.0)". If a
        property fails the node stays in the graph.
        """
        # MCP スキーマで任意引数にするため None を受け、未指定は従来の既定値として扱う
        properties_json = properties_json or ''
        _raise_for_invalid_object(anim_blueprint, 'anim_blueprint')
        if not shared_group_tag:
            raise ValueError('shared_group_tag must not be empty.')
        tag = _resolve_gameplay_tag(shared_group_tag)
        # ノード追加前に JSON を検証し、書式エラーでノードだけ残らないようにする
        properties = (
            _parse_graph_node_properties(properties_json)
            if properties_json else []
        )

        handle = unreal.KawaiiPhysicsEditorLibrary.add_kawaii_physics_shared_publisher_node(
            anim_blueprint,
            tag,
            reuse_existing,
            True,
        )
        if (handle is None or
                not unreal.KawaiiPhysicsEditorLibrary.is_shared_publisher_graph_node_handle_valid(
                    handle)):
            raise RuntimeError(
                f'Unable to add KawaiiPhysics Shared Publisher node: {shared_group_tag}')

        for name, text in properties:
            _set_shared_publisher_node_property_impl(handle, name, text)
        return handle

    @toolset_registry.tool_call
    @staticmethod
    def collect_shared_publisher_nodes(
            anim_blueprint: unreal.AnimBlueprint
    ) -> list[unreal.KawaiiPhysicsSharedPublisherGraphNodeHandle]:
        """Collects KawaiiPhysics Shared Publisher graph nodes in an AnimBlueprint."""
        _raise_for_invalid_object(anim_blueprint, 'anim_blueprint')
        return list(
            unreal.KawaiiPhysicsEditorLibrary.collect_kawaii_physics_shared_publisher_graph_nodes(
                anim_blueprint))

    @toolset_registry.tool_call
    @staticmethod
    def set_shared_publisher_node_property(
            handle: unreal.KawaiiPhysicsSharedPublisherGraphNodeHandle,
            property_name: str,
            value: str) -> None:
        """Sets a Shared Publisher node property from a string value.

        value is in ImportText format, e.g. SharedWind
        "(ParameterMode=Advanced,ConstantForce=8.0)" or bEnabled "False".
        """
        _set_shared_publisher_node_property_impl(handle, property_name, value)

    @toolset_registry.tool_call
    @staticmethod
    def get_shared_publisher_node_property(
            handle: unreal.KawaiiPhysicsSharedPublisherGraphNodeHandle,
            property_name: str) -> str:
        """Gets a Shared Publisher node property as a string value."""
        _raise_for_invalid_shared_publisher_handle(handle, 'handle')
        if not property_name:
            raise ValueError('property_name must not be empty.')

        value = unreal.KawaiiPhysicsEditorLibrary.get_shared_publisher_node_property_as_string(
            handle,
            unreal.Name(property_name),
        )
        # C++ 側は失敗時に空文字列を返す（有効なプロパティの書き出しは空にならない）
        if not value:
            raise RuntimeError(
                f'Unable to get KawaiiPhysics Shared Publisher node property: {property_name}')
        return str(value)

    @toolset_registry.tool_call
    @staticmethod
    def get_graph_node_external_forces(
            handle: unreal.KawaiiPhysicsGraphNodeHandle) -> str:
        """Returns the node's ExternalForces as a JSON array.

        Each element is {"_structType": "/Script/KawaiiPhysics.<struct>", ...}
        with the editable fields of that force (an empty slot is null); the
        same format is accepted by set_graph_node_external_forces.
        """
        _raise_for_invalid_handle(handle, 'handle')

        text = _unpack_bool_out(
            unreal.KawaiiPhysicsEditorLibrary.get_graph_node_external_forces_as_json(handle),
            None,
        )
        if text is None:
            raise RuntimeError('Unable to get KawaiiPhysics graph node ExternalForces.')
        return str(text)

    @toolset_registry.tool_call
    @staticmethod
    def set_graph_node_external_forces(
            handle: unreal.KawaiiPhysicsGraphNodeHandle,
            forces_json: str) -> int:
        """Replaces the node's ExternalForces from a JSON array and returns the element count.

        Uses the get_graph_node_external_forces format, e.g. [{"_structType":
        "/Script/KawaiiPhysics.KawaiiPhysics_ExternalForce_Basic", "ForceDir":
        {"X": 0, "Y": 1, "Z": 0}}]. _structType must be a struct derived from
        FKawaiiPhysics_ExternalForce (path or struct name); omitted fields keep
        their defaults and "[]" clears the array. Nothing changes on error.
        """
        _raise_for_invalid_handle(handle, 'handle')
        if forces_json is None or not forces_json.strip():
            raise ValueError('forces_json must not be empty.')

        count, error = _unpack_count_and_out(
            unreal.KawaiiPhysicsEditorLibrary.set_graph_node_external_forces_from_json(
                handle,
                forces_json,
            ))
        if count < 0:
            raise ValueError(f'Unable to set ExternalForces: {error}')
        return count

    @toolset_registry.tool_call
    @staticmethod
    def add_collision_limit(
            handle: unreal.KawaiiPhysicsGraphNodeHandle,
            shape: str,
            driving_bone: str,
            location: list[float],
            rotation: list[float] = [],
            radius: float = 5.0,
            radius1: float = 5.0,
            length: float = 10.0,
            extent: list[float] = [],
            limit_type: str = 'Outer') -> int:
        """Appends one collision limit placed in component space and returns the new size of that shape's array.

        shape is Sphere, Capsule, TaperedCapsule, Box or Planar. location and
        rotation ([pitch, yaw, roll], empty = identity) are component-space
        values in the reference pose; they are converted to the DrivingBone
        local OffsetLocation/OffsetRotation. Capsules extend along the limit's
        local Z axis and a Planar normal is its local Z (up) axis. An empty
        driving_bone is allowed only for Planar (stays in component space).
        radius is the Sphere/Capsule radius and the TaperedCapsule +Z end
        radius, radius1 the TaperedCapsule -Z end radius, extent the Box half
        extents [x, y, z] (empty = default), limit_type (Outer/Inner) is for Sphere.
        """
        # Python から None で呼ばれても動くよう空リストに揃える
        rotation = rotation or []
        extent = extent or []
        _raise_for_invalid_handle(handle, 'handle')
        shape_entry = _COLLISION_LIMIT_SHAPES.get(str(shape or '').lower())
        if shape_entry is None:
            raise ValueError(
                f'Unknown shape: {shape}. Valid shapes: Sphere, Capsule, '
                'TaperedCapsule, Box, Planar.')
        shape_name, property_name = shape_entry
        canonical_limit_type = _SPHERICAL_LIMIT_TYPES.get(str(limit_type or '').lower())
        if canonical_limit_type is None:
            raise ValueError(f'Unknown limit_type: {limit_type}. Valid values: Outer, Inner.')
        box_extent = _make_float3(extent, 'extent') if extent else []

        bone_transform = None
        if driving_bone:
            bone_transform = _unpack_bool_out(
                unreal.KawaiiPhysicsEditorLibrary.get_graph_node_reference_bone_transform(
                    handle,
                    unreal.Name(driving_bone),
                ),
                None,
            )
            if bone_transform is None:
                raise ValueError(
                    f"driving_bone '{driving_bone}' does not exist in the target "
                    "skeleton of the node's AnimBlueprint.")
        elif shape_name != 'Planar':
            raise ValueError(
                'driving_bone must not be empty unless shape is Planar.')

        offset_location, offset_rotation = _compute_collision_limit_offset(
            bone_transform,
            location,
            rotation,
        )
        element_text = _make_collision_limit_text(
            shape_name,
            driving_bone,
            offset_location,
            offset_rotation,
            radius,
            radius1,
            length,
            box_extent,
            canonical_limit_type,
        )

        current_text = _graph_node_property_or_none(handle, property_name)
        if current_text is None:
            raise RuntimeError(
                f'Unable to get KawaiiPhysics graph node property: {property_name}')
        _set_graph_node_properties_impl(
            handle,
            [(property_name, _append_import_text_array_element(current_text, element_text))],
        )

        updated_text = _graph_node_property_or_none(handle, property_name)
        if updated_text is None:
            raise RuntimeError(
                f'Unable to get KawaiiPhysics graph node property: {property_name}')
        return _count_import_text_array_elements(updated_text)

    @toolset_registry.tool_call
    @staticmethod
    def set_graph_nodes_property(
            anim_blueprint: unreal.AnimBlueprint,
            property_name: str,
            value: str,
            filter_tag_names: list[str] = [],
            filter_exact_match: bool = False) -> int:
        """Sets a FAnimNode_KawaiiPhysics property from a string on every KawaiiPhysics node in the AnimBlueprint (optionally filtered by tags).

        Returns the number of nodes updated. Raises with the property and node
        number when a requested change is absent on read-back.
        """
        # Python から None で呼ばれても動くよう空リストに揃える
        filter_tag_names = filter_tag_names or []
        _raise_for_invalid_object(anim_blueprint, 'anim_blueprint')
        if not property_name:
            raise ValueError('property_name must not be empty.')
        if value is None:
            raise ValueError('value must not be None.')

        handles = _collect_graph_nodes_impl(
            anim_blueprint,
            filter_tag_names,
            filter_exact_match,
        )
        for index, handle in enumerate(handles):
            try:
                _set_graph_node_property_verified(handle, property_name, value)
            except RuntimeError as error:
                raise RuntimeError(
                    f'Unable to set {property_name} on node '
                    f'{index + 1}/{len(handles)}: {error}') from error
        return len(handles)

    @toolset_registry.tool_call
    @staticmethod
    def get_graph_nodes_property(
            anim_blueprint: unreal.AnimBlueprint,
            property_name: str,
            filter_tag_names: list[str] = [],
            filter_exact_match: bool = False) -> dict[str, str]:
        """Gets a FAnimNode_KawaiiPhysics property as a string from every KawaiiPhysics node in the AnimBlueprint (optionally filtered by tags).

        Keys are the node GUID when it can be read, otherwise a stable
        node name or "node<index>" fallback.
        """
        # Python から None で呼ばれても動くよう空リストに揃える
        filter_tag_names = filter_tag_names or []
        _raise_for_invalid_object(anim_blueprint, 'anim_blueprint')
        if not property_name:
            raise ValueError('property_name must not be empty.')

        handles = _collect_graph_nodes_impl(
            anim_blueprint,
            filter_tag_names,
            filter_exact_match,
        )
        values = {}
        for index, handle in enumerate(handles):
            value = _graph_node_property_or_none(handle, property_name)
            if value is None:
                raise RuntimeError(
                    f'Unable to get {property_name} on node '
                    f'{index + 1}/{len(handles)}')
            values[_graph_node_key(handle, index)] = value
        return values

    @toolset_registry.tool_call
    @staticmethod
    def describe_graph_nodes(anim_blueprint: unreal.AnimBlueprint) -> str:
        """Returns a JSON array describing every KawaiiPhysics node in the AnimBlueprint.

        Each element holds the node index/key, RootBone, tag and the world
        collision switches; a property that cannot be read is null.
        """
        _raise_for_invalid_object(anim_blueprint, 'anim_blueprint')

        handles = _collect_graph_nodes_impl(anim_blueprint, [], False)
        descriptions = []
        for index, handle in enumerate(handles):
            root_bone = unreal.KawaiiPhysicsEditorLibrary.get_graph_node_root_bone_name(
                handle)
            tag = unreal.KawaiiPhysicsEditorLibrary.get_graph_node_tag(handle)
            description = {
                'index': index,
                'key': _graph_node_key(handle, index),
                'root_bone': None if root_bone is None else str(root_bone),
                'tag': None if tag is None else str(
                    tag.get_editor_property('tag_name')),
            }
            for name in (
                    'bAllowWorldCollision',
                    'bUseSimpleWorldCollision',
                    'SimpleWorldCollisionSkeletalMeshCollision',
                    'bUseSharedCollision',
            ):
                description[name] = _graph_node_property_or_none(handle, name)
            descriptions.append(description)
        return json.dumps(descriptions)

    @toolset_registry.tool_call
    @staticmethod
    def describe_graph_node_settings(
            anim_blueprint: unreal.AnimBlueprint,
            filter_tag_names: list[str] = [],
            filter_exact_match: bool = False) -> str:
        """Returns a JSON array of KawaiiPhysics nodes with their FAnimNode_KawaiiPhysics settings and external forces.

        settings holds ImportText values of the properties specific to the
        KawaiiPhysics node (FAnimNode_KawaiiPhysics); base class properties
        such as Alpha, AlphaInputType and LODThreshold are not included. Use
        describe_kawaii_physics_runtime_on_actor for the runtime Alpha.
        Unreadable settings are null; external_forces is a JSON array.
        """
        # Python から None で呼ばれても動くよう空リストに揃える
        filter_tag_names = filter_tag_names or []
        handles = _collect_graph_nodes_impl(
            anim_blueprint, filter_tag_names, filter_exact_match)
        descriptions = []
        for index, handle in enumerate(handles):
            root_bone = unreal.KawaiiPhysicsEditorLibrary.get_graph_node_root_bone_name(
                handle)
            tag = unreal.KawaiiPhysicsEditorLibrary.get_graph_node_tag(handle)
            settings = {}
            for name in _GRAPH_NODE_SETTINGS_PROPERTIES:
                try:
                    settings[name] = _graph_node_property_or_none(handle, name)
                except Exception:
                    settings[name] = None
            forces_text = _unpack_bool_out(
                unreal.KawaiiPhysicsEditorLibrary.get_graph_node_external_forces_as_json(
                    handle), None)
            if forces_text is None:
                raise RuntimeError('Unable to get KawaiiPhysics graph node ExternalForces.')
            descriptions.append({
                'index': index,
                'key': _graph_node_key(handle, index),
                'root_bone': None if root_bone is None else str(root_bone),
                'tag': None if tag is None else str(
                    tag.get_editor_property('tag_name')),
                'settings': settings,
                'external_forces': json.loads(str(forces_text)),
            })
        return json.dumps(descriptions)

    @toolset_registry.tool_call
    @staticmethod
    def diff_graph_node_settings(
            anim_blueprint: unreal.AnimBlueprint,
            reference_json: str,
            node_index: int = 0) -> str:
        """Compare a node with a describe output, one node, or a settings object.

        ImportText comparison ignores whitespace outside quotes and numeric
        spelling differences. SyncBones ignores runtime and preview fields;
        quoted text and all other tokens stay exact.
        """
        handles = _collect_graph_nodes_impl(anim_blueprint, [], False)
        if not isinstance(node_index, int) or node_index < 0 or node_index >= len(handles):
            raise ValueError(f'node_index {node_index} is out of range.')
        settings, forces, has_forces, key = _parse_graph_node_settings_snapshot(
            reference_json, node_index)
        handle = handles[node_index]
        if key is not None and key != _graph_node_key(handle, node_index):
            raise ValueError(f'Settings snapshot key does not match node_index {node_index}.')
        changed = []
        missing = []
        for name in _GRAPH_NODE_SETTINGS_PROPERTIES:
            if name not in settings:
                missing.append(name)
                continue
            try:
                current = _graph_node_property_or_none(handle, name)
            except Exception:
                current = None
            reference = settings[name]
            if _normalize_import_text(current, name) != _normalize_import_text(reference, name):
                changed.append({'name': name, 'current': current, 'reference': reference})
        if has_forces:
            current_forces = json.loads(
                KawaiiPhysicsToolset.get_graph_node_external_forces(handle))
            if current_forces != forces:
                changed.append({
                    'name': 'external_forces', 'current': current_forces,
                    'reference': forces})
        return json.dumps({
            'status': 'differs' if changed or missing else 'ok',
            'node_index': node_index,
            'changed': changed,
            'missing_in_reference': missing,
        })

    @toolset_registry.tool_call
    @staticmethod
    def apply_graph_node_settings(
            anim_blueprint: unreal.AnimBlueprint,
            settings_json: str,
            node_index: int = 0,
            dry_run: bool = False) -> str:
        """Restore a settings snapshot in one Undo transaction; does not compile or save.

        Accepts a describe output, one node object, or a settings object.
        The caller compiles with compile_anim_blueprint after applying changes.
        """
        handles = _collect_graph_nodes_impl(anim_blueprint, [], False)
        if not isinstance(node_index, int) or node_index < 0 or node_index >= len(handles):
            raise ValueError(f'node_index {node_index} is out of range.')
        settings, forces, has_forces, key = _parse_graph_node_settings_snapshot(
            settings_json, node_index)
        handle = handles[node_index]
        if key is not None and key != _graph_node_key(handle, node_index):
            raise ValueError(f'Settings snapshot key does not match node_index {node_index}.')

        pending = []
        failed = []
        unchanged_names = set()
        for name in _GRAPH_NODE_SETTINGS_PROPERTIES:
            if name not in settings:
                continue
            reference = settings[name]
            try:
                current = _graph_node_property_or_none(handle, name)
                if current is None and reference is None:
                    unchanged_names.add(name)
                    continue
                if reference is None:
                    failed.append({'name': name, 'error': 'Snapshot value is null.'})
                    continue
                if current is None:
                    raise RuntimeError('Current value is unreadable.')
                if _normalize_import_text(current, name) == _normalize_import_text(reference, name):
                    unchanged_names.add(name)
                else:
                    pending.append((name, reference))
            except Exception as error:
                if reference is None:
                    unchanged_names.add(name)
                else:
                    failed.append({'name': name, 'error': str(error)})
        if has_forces:
            try:
                current_forces = json.loads(
                    KawaiiPhysicsToolset.get_graph_node_external_forces(handle))
                if current_forces == forces:
                    unchanged_names.add('external_forces')
                else:
                    pending.append(('external_forces', forces))
            except Exception as error:
                failed.append({'name': 'external_forces', 'error': str(error)})
        if dry_run:
            return json.dumps({
                'status': 'dry_run', 'written': [], 'would_write': [name for name, _ in pending],
                'failed': failed, 'unchanged_count': len(unchanged_names)})

        written = []
        if pending:
            with unreal.ScopedEditorTransaction('Apply KawaiiPhysics graph node settings'):
                for name, reference in pending:
                    try:
                        if name == 'external_forces':
                            KawaiiPhysicsToolset.set_graph_node_external_forces(
                                handle, json.dumps(reference))
                            read_back = json.loads(
                                KawaiiPhysicsToolset.get_graph_node_external_forces(handle))
                            if read_back != reference:
                                raise RuntimeError(f'Read-back value differs: {read_back}')
                        else:
                            _set_graph_node_property_verified(handle, name, reference)
                            read_back = _graph_node_property_or_none(handle, name)
                            if (_normalize_import_text(read_back, name) !=
                                    _normalize_import_text(reference, name)):
                                raise RuntimeError(f'Read-back value differs: {read_back}')
                        written.append(name)
                    except Exception as error:
                        failed.append({'name': name, 'error': str(error)})
                failed_names = {item['name'] for item in failed}
                for name in _GRAPH_NODE_SETTINGS_PROPERTIES:
                    if name not in settings or settings[name] is None or name in failed_names:
                        continue
                    try:
                        read_back = _graph_node_property_or_none(handle, name)
                        if (_normalize_import_text(read_back, name) !=
                                _normalize_import_text(settings[name], name)):
                            raise RuntimeError(f'Final read-back value differs: {read_back}')
                    except Exception as error:
                        failed.append({'name': name, 'error': str(error)})
                        failed_names.add(name)
                        unchanged_names.discard(name)
                        if name in written:
                            written.remove(name)
                if has_forces and 'external_forces' not in failed_names:
                    try:
                        read_back = json.loads(
                            KawaiiPhysicsToolset.get_graph_node_external_forces(handle))
                        if read_back != forces:
                            raise RuntimeError(f'Final read-back value differs: {read_back}')
                    except Exception as error:
                        failed.append({'name': 'external_forces', 'error': str(error)})
                        unchanged_names.discard('external_forces')
                        if 'external_forces' in written:
                            written.remove('external_forces')
        return json.dumps({
            'status': 'partial' if failed else 'ok',
            'written': written, 'failed': failed,
            'unchanged_count': len(unchanged_names)})

    @toolset_registry.tool_call
    @staticmethod
    def bind_graph_node_function(
            handle: unreal.KawaiiPhysicsGraphNodeHandle,
            event: str,
            function_name: str) -> str:
        """Binds an AnimNode event and returns JSON with function_name and created.

        A new function graph is thread safe, has const reference (Context, Node)
        arguments, and starts with an empty body. In its body, convert the Node
        pin with ConvertToKawaiiPhysics and call KawaiiPhysicsLibrary Set*
        functions such as SetPhysicsSettings. Call compile_anim_blueprint after
        binding. Existing function graphs are never deleted or replaced;
        an empty function_name only clears the binding.
        """
        _raise_for_invalid_handle(handle, 'handle')
        event_value = _ANIM_NODE_FUNCTION_EVENTS.get(str(event or '').lower())
        if event_value is None:
            raise ValueError(f'Unknown AnimNode function event: {event}')
        if function_name is None:
            raise ValueError('function_name must not be None.')
        count, error = _unpack_count_and_out(
            unreal.KawaiiPhysicsEditorLibrary.bind_graph_node_anim_node_function(
                handle, event_value, unreal.Name(function_name)))
        if count < 0:
            raise ValueError(f'Unable to bind AnimNode function: {error}')
        return json.dumps({'function_name': function_name, 'created': count == 1})

    @toolset_registry.tool_call
    @staticmethod
    def set_preset_node_property(
            preset: unreal.KawaiiPhysicsPresetDataAsset,
            property_name: str,
            value: str) -> None:
        """Sets a preset node property from a string value."""
        _raise_for_invalid_object(preset, 'preset')
        if not property_name:
            raise ValueError('property_name must not be empty.')
        if value is None:
            raise ValueError('value must not be None.')

        ok = unreal.KawaiiPhysicsEditorLibrary.set_preset_node_property_from_string(
            preset,
            unreal.Name(property_name),
            value,
        )
        if not ok:
            raise RuntimeError(
                f'Unable to set KawaiiPhysics preset node property: {property_name}')

    @toolset_registry.tool_call
    @staticmethod
    def get_preset_node_property(
            preset: unreal.KawaiiPhysicsPresetDataAsset,
            property_name: str) -> str:
        """Gets a preset node property as a string value."""
        _raise_for_invalid_object(preset, 'preset')
        if not property_name:
            raise ValueError('property_name must not be empty.')

        value = unreal.KawaiiPhysicsEditorLibrary.get_preset_node_property_as_string(
            preset,
            unreal.Name(property_name),
        )
        if value is None:
            raise RuntimeError(
                f'Unable to get KawaiiPhysics preset node property: {property_name}')
        return str(value)

    @toolset_registry.tool_call
    @staticmethod
    def set_preset_target_tags(
            preset: unreal.KawaiiPhysicsPresetDataAsset,
            tag_names: list[str],
            exact_match: bool) -> None:
        """Sets preset target tags from gameplay tag names."""
        _raise_for_invalid_object(preset, 'preset')
        if tag_names is None:
            raise ValueError('tag_names must not be None.')

        ok = unreal.KawaiiPhysicsEditorLibrary.set_preset_target_tags(
            preset,
            [unreal.Name(tag_name) for tag_name in tag_names],
            exact_match,
        )
        if not ok:
            raise RuntimeError('Unable to set KawaiiPhysics preset target tags.')

    @toolset_registry.tool_call
    @staticmethod
    def set_preset_description(
            preset: unreal.KawaiiPhysicsPresetDataAsset,
            description: str) -> None:
        """Sets a preset description from a string."""
        _raise_for_invalid_object(preset, 'preset')
        if description is None:
            raise ValueError('description must not be None.')

        ok = unreal.KawaiiPhysicsEditorLibrary.set_preset_description(
            preset,
            description,
        )
        if not ok:
            raise RuntimeError('Unable to set KawaiiPhysics preset description.')

    @toolset_registry.tool_call
    @staticmethod
    def get_preset_description(
            preset: unreal.KawaiiPhysicsPresetDataAsset) -> str:
        """Gets a preset description as a string."""
        _raise_for_invalid_object(preset, 'preset')

        description = unreal.KawaiiPhysicsEditorLibrary.get_preset_description(preset)
        return str(description)

    @toolset_registry.tool_call
    @staticmethod
    def set_graph_node_tag(
            handle: unreal.KawaiiPhysicsGraphNodeHandle,
            tag_name: str) -> None:
        """Sets a graph node KawaiiPhysicsTag from a gameplay tag name."""
        _raise_for_invalid_handle(handle, 'handle')
        if not tag_name:
            raise ValueError('tag_name must not be empty.')

        ok = unreal.KawaiiPhysicsEditorLibrary.set_graph_node_tag(
            handle,
            _resolve_gameplay_tag(tag_name),
        )
        if not ok:
            raise RuntimeError(
                f'Unable to set KawaiiPhysics graph node tag: {tag_name}')

    @toolset_registry.tool_call
    @staticmethod
    def get_graph_node_tag(
            handle: unreal.KawaiiPhysicsGraphNodeHandle) -> str:
        """Gets a graph node KawaiiPhysicsTag as a string."""
        _raise_for_invalid_handle(handle, 'handle')

        tag = unreal.KawaiiPhysicsEditorLibrary.get_graph_node_tag(handle)
        if tag is None:
            raise RuntimeError('Unable to get KawaiiPhysics graph node tag.')
        # GameplayTag は属性アクセス不可のため get_editor_property 経由で読む
        return str(tag.get_editor_property('tag_name'))

    @toolset_registry.tool_call
    @staticmethod
    def set_graph_node_root_bone(
            handle: unreal.KawaiiPhysicsGraphNodeHandle,
            bone_name: str) -> None:
        """Sets a graph node RootBone from a bone name."""
        _raise_for_invalid_handle(handle, 'handle')
        if not bone_name:
            raise ValueError('bone_name must not be empty.')

        ok = unreal.KawaiiPhysicsEditorLibrary.set_graph_node_root_bone_name(
            handle,
            unreal.Name(bone_name),
        )
        if not ok:
            raise RuntimeError(
                f'Unable to set KawaiiPhysics graph node RootBone: {bone_name}')

    @toolset_registry.tool_call
    @staticmethod
    def get_graph_node_root_bone(
            handle: unreal.KawaiiPhysicsGraphNodeHandle) -> str:
        """Gets a graph node RootBone as a string."""
        _raise_for_invalid_handle(handle, 'handle')

        bone_name = unreal.KawaiiPhysicsEditorLibrary.get_graph_node_root_bone_name(
            handle)
        if bone_name is None:
            raise RuntimeError('Unable to get KawaiiPhysics graph node RootBone.')
        return str(bone_name)

    @toolset_registry.tool_call
    @staticmethod
    def find_all_preset_assets() -> list[unreal.KawaiiPhysicsPresetDataAsset]:
        """Finds all KawaiiPhysics preset assets in the project."""
        presets = unreal.KawaiiPhysicsEditorLibrary.find_all_preset_assets()
        if presets is None:
            raise RuntimeError('Unable to find KawaiiPhysics preset assets.')
        return presets

    @toolset_registry.tool_call
    @staticmethod
    def find_anim_blueprint_assets(content_paths: list[str]) -> list[str]:
        """Finds AnimBlueprint asset paths under content paths."""
        if content_paths is None:
            raise ValueError('content_paths must not be None.')

        asset_paths = unreal.KawaiiPhysicsEditorLibrary.find_anim_blueprint_assets(
            content_paths)
        if asset_paths is None:
            raise RuntimeError('Unable to find AnimBlueprint assets.')
        # str(SoftObjectPath) は構造体表記になるため export_text() でパス文字列化する
        return [asset_path.export_text() for asset_path in asset_paths]

    @toolset_registry.tool_call
    @staticmethod
    def apply_preset_to_graph_node(
            handle: unreal.KawaiiPhysicsGraphNodeHandle,
            preset: unreal.KawaiiPhysicsPresetDataAsset,
            apply_bone_assignment: bool,
            apply_tag: bool) -> None:
        """Applies a KawaiiPhysics preset to a graph node."""
        _raise_for_invalid_handle(handle, 'handle')
        _raise_for_invalid_object(preset, 'preset')
        options = _make_preset_apply_options(apply_bone_assignment, apply_tag)
        ok = unreal.KawaiiPhysicsEditorLibrary.apply_preset_to_graph_node(
            handle,
            preset,
            options,
        )
        if not ok:
            raise RuntimeError('Unable to apply preset to KawaiiPhysics graph node.')

    @toolset_registry.tool_call
    @staticmethod
    def does_graph_node_match_preset(
            handle: unreal.KawaiiPhysicsGraphNodeHandle,
            preset: unreal.KawaiiPhysicsPresetDataAsset,
            apply_bone_assignment: bool,
            apply_tag: bool) -> list[str]:
        """Returns differing preset property names; an empty list means a match."""
        _raise_for_invalid_handle(handle, 'handle')
        _raise_for_invalid_object(preset, 'preset')
        options = _make_preset_apply_options(apply_bone_assignment, apply_tag)

        diff_properties = (
            unreal.KawaiiPhysicsEditorLibrary
            .get_graph_node_preset_diff_properties(
                handle,
                preset,
                options,
            )
        )
        if diff_properties is None:
            raise RuntimeError('Unable to diff KawaiiPhysics graph node preset.')
        return [str(property_name) for property_name in diff_properties]

    @toolset_registry.tool_call
    @staticmethod
    def get_preset_diff(
            handle: unreal.KawaiiPhysicsGraphNodeHandle,
            preset: unreal.KawaiiPhysicsPresetDataAsset,
            apply_bone_assignment: bool,
            apply_tag: bool) -> list[unreal.KawaiiPhysicsPresetDiffValue]:
        """Returns differing preset properties with node/preset values; an empty list means a match."""
        _raise_for_invalid_handle(handle, 'handle')
        _raise_for_invalid_object(preset, 'preset')
        options = _make_preset_apply_options(apply_bone_assignment, apply_tag)

        diff_values = (
            unreal.KawaiiPhysicsEditorLibrary
            .get_graph_node_preset_diff_values(
                handle,
                preset,
                options,
            )
        )
        if diff_values is None:
            raise RuntimeError('Unable to diff KawaiiPhysics graph node preset values.')
        return list(diff_values)

    @toolset_registry.tool_call
    @staticmethod
    def export_graph_node_to_preset(
            handle: unreal.KawaiiPhysicsGraphNodeHandle,
            preset_path: str) -> unreal.KawaiiPhysicsPresetDataAsset:
        """Exports a graph node to an existing or newly created preset asset."""
        _raise_for_invalid_handle(handle, 'handle')
        package_path = _asset_package_path(preset_path)
        preset = unreal.EditorAssetLibrary.load_asset(package_path)
        if preset is None:
            folder_path, asset_name = _split_asset_path(package_path)
            if not unreal.EditorAssetLibrary.does_directory_exist(folder_path):
                unreal.EditorAssetLibrary.make_directory(folder_path)
            factory = unreal.DataAssetFactory()
            factory.set_editor_property(
                'data_asset_class',
                unreal.KawaiiPhysicsPresetDataAsset.static_class(),
            )
            preset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
                asset_name,
                folder_path,
                unreal.KawaiiPhysicsPresetDataAsset,
                factory,
            )
        if preset is None:
            raise RuntimeError(f'Unable to create preset asset: {package_path}')
        if not isinstance(preset, unreal.KawaiiPhysicsPresetDataAsset):
            raise RuntimeError(f'Preset asset has an unexpected type: {package_path}')

        ok = unreal.KawaiiPhysicsEditorLibrary.export_graph_node_to_preset(
            handle,
            preset,
        )
        if not ok:
            raise RuntimeError(
                f'Unable to export KawaiiPhysics graph node to {package_path}.')
        return preset

    @toolset_registry.tool_call
    @staticmethod
    def apply_preset_to_project(
            preset: unreal.KawaiiPhysicsPresetDataAsset,
            dry_run: bool,
            check_out_files: bool) -> list[unreal.KawaiiPhysicsNodeAuditEntry]:
        """Applies a preset and returns the audit report, not the update count."""
        _raise_for_invalid_object(preset, 'preset')

        result = unreal.KawaiiPhysicsEditorLibrary.apply_preset_to_project(
            preset,
            dry_run,
            check_out_files,
        )
        if not isinstance(result, tuple) or len(result) != 2:
            raise RuntimeError(
                'Unexpected return value from apply_preset_to_project.')
        _updated_count, report = result
        return report

    @toolset_registry.tool_call
    @staticmethod
    def audit_kawaii_physics_nodes(
            content_paths: list[str],
            filter_tag_names: list[str],
            filter_exact_match: bool) -> list[unreal.KawaiiPhysicsNodeAuditEntry]:
        """Audits KawaiiPhysics nodes under content paths; empty paths use /Game."""
        filter_tags = _make_tag_container(filter_tag_names)

        result = unreal.KawaiiPhysicsEditorLibrary.audit_kawaii_physics_nodes(
            content_paths,
            filter_tags,
            filter_exact_match,
        )
        if result is None:
            raise RuntimeError('KawaiiPhysics node audit failed.')
        return result

    @toolset_registry.tool_call
    @staticmethod
    def find_bones_by_pattern(
            skeleton: unreal.Skeleton,
            pattern: str) -> list[str]:
        """Finds reference bone names by regex; empty input returns no names."""
        _raise_for_invalid_object(skeleton, 'skeleton')
        names = unreal.KawaiiPhysicsEditorLibrary.find_bones_by_pattern(
            skeleton,
            pattern,
        )
        return [str(name) for name in names]

    @toolset_registry.tool_call
    @staticmethod
    def validate_placement_requests(
            anim_blueprint: unreal.AnimBlueprint,
            requests: list[unreal.KawaiiPhysicsNodePlacementRequest]) -> list[str]:
        """Returns placement errors and warnings; an empty list means no issues."""
        return _validate_placement_requests_impl(anim_blueprint, requests)

    # ===== Runtime (PIE): actors / console / multiplier / gust / alpha / sampler =====

    @toolset_registry.tool_call
    @staticmethod
    def execute_console_command(
            command: str,
            prefer_pie: bool = True) -> bool:
        """Executes a console command in the PIE world when prefer_pie and PIE runs, otherwise the editor world.

        prefer_pie=False targets the editor world even while PIE runs. Also
        usable for CVars, stat commands and `py "<file>"`. Raises when no world
        is available; True means the command was dispatched (the console
        reports command errors only through the log).
        """
        if not command:
            raise ValueError('command must not be empty.')

        world = _resolve_world(prefer_pie)
        unreal.SystemLibrary.execute_console_command(world, command)
        return True

    @toolset_registry.tool_call
    @staticmethod
    def find_kawaii_physics_actors(
            actor_label: str | None = None,
            prefer_pie: bool = True) -> list[str]:
        """Lists SkeletalMeshComponents that run an AnimBlueprint as "<actor_label>|<component_name>|<anim_class_name>".

        An empty actor_label scans every actor; components without an
        AnimClass are skipped.
        """
        # MCP スキーマで任意引数にするため None を受け、未指定は従来の既定値として扱う
        actor_label = actor_label or ''
        world = _resolve_world(prefer_pie)
        entries = []
        for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor):
            if actor_label and not _does_actor_match_label(actor, actor_label):
                continue
            label = _actor_label(actor)
            for component in _skeletal_mesh_components(actor):
                anim_class_name = _anim_class_name(component)
                if not anim_class_name:
                    continue
                entries.append(
                    f'{label}|{component.get_name()}|{anim_class_name}')
        return entries

    @toolset_registry.tool_call
    @staticmethod
    def describe_kawaii_physics_runtime_on_actor(
            actor_label: str,
            prefer_pie: bool = True) -> str:
        """Returns runtime anim_instance_class and configured anim_class, node counts and alpha per component.

        alpha is the current Alpha of the first KawaiiPhysics node found in the
        component (null when none is found), not a per-node or combined value.
        """
        components = _find_skeletal_mesh_components_by_label(actor_label, prefer_pie)
        filter_tags = unreal.GameplayTagContainer()
        descriptions = []
        for component in components:
            anim_instance = component.get_anim_instance()
            post_instance = component.get_post_process_instance()
            main_nodes = (_unpack_bool_out(
                unreal.KawaiiPhysicsLibrary.collect_kawaii_physics_nodes_from_anim_instance(
                    anim_instance, filter_tags, False), [])
                if anim_instance is not None else [])
            post_nodes = (_unpack_bool_out(
                unreal.KawaiiPhysicsLibrary.collect_kawaii_physics_nodes_from_anim_instance(
                    post_instance, filter_tags, False), [])
                if post_instance is not None else [])
            all_nodes = _unpack_bool_out(
                unreal.KawaiiPhysicsLibrary.collect_kawaii_physics_nodes_from_component(
                    component, filter_tags, False), [])
            main_count = len(main_nodes)
            post_count = len(post_nodes)
            total_count = len(all_nodes)
            mode = component.get_animation_mode()
            descriptions.append({
                'actor': _actor_label(component.get_owner()),
                'component': str(component.get_name()),
                'animation_mode': str(mode.name) if hasattr(mode, 'name') else str(mode),
                'anim_instance_class': (
                    str(anim_instance.get_class().get_name())
                    if anim_instance is not None else None),
                'anim_class': _anim_class_name(component) or None,
                'is_sequencer_instance': isinstance(
                    anim_instance, unreal.AnimSequencerInstance),
                'post_process_instance_class': (
                    str(post_instance.get_class().get_name())
                    if post_instance is not None else None),
                'node_count': {
                    'main': main_count,
                    'linked': max(0, total_count - main_count - post_count),
                    'post_process': post_count,
                    'total': total_count,
                },
                'alpha': _get_alpha_on_component(component, filter_tags, False),
            })
        return json.dumps(descriptions)

    @toolset_registry.tool_call
    @staticmethod
    def describe_kawaii_physics_bones_on_actor(
            actor_label: str, prefer_pie: bool = True,
            filter_tag_names: list[str] = [], root_bone: str | None = None,
            include_dummy: bool = False, summary_only: bool = False) -> str:
        """Describe every runtime bone, limit and constraint in each matching node.

        Unlike describe_kawaii_physics_runtime_on_actor, this returns the
        per-node geometry instead of only component and node counts.
        Hiding dummies affects display only and preserves original indices.
        non_uniform_scale reports simulation-to-component scale, so scaling an
        actor non-uniformly leaves it false for ComponentSpace nodes; radii are
        in component-space centimeters. summary_only returns node identities
        and counts only; include_dummy does not affect those total counts.
        """
        filter_tag_names = list(filter_tag_names or [])
        root_bone = root_bone or ''
        result = _runtime_snapshot(actor_label, prefer_pie, filter_tag_names, root_bone)
        nodes = _runtime_nodes(result)
        result['checked'] = sum(len(node['bones']) for node in nodes if node['evaluated'])
        if result['status'] == 'ok' and result['checked'] == 0:
            result['status'] = 'nothing_checked'
            result['reasons'].append('Evaluated nodes have no runtime bones.')
        if summary_only:
            for component in result['components']:
                component['nodes'] = [
                    _compact_runtime_bone_summary(node) for node in component['nodes']]
            return json.dumps(result)
        if not include_dummy:
            for node in nodes:
                hidden = sum(b['dummy_type'] != 'None' for b in node['bones'])
                result['excluded'] += hidden
                node['bones'] = [b for b in node['bones'] if b['dummy_type'] == 'None']
            if result['excluded']:
                result['reasons'].append('Dummy bones are hidden from display only.')
        result['reasons'].extend(_runtime_limit_reasons(nodes))
        return json.dumps(result)

    @toolset_registry.tool_call
    @staticmethod
    def check_collision_clearance_on_actor(
            actor_label: str, prefer_pie: bool = True,
            filter_tag_names: list[str] = [], root_bone: str | None = None,
            position_source: str = 'both', contact_threshold: float = 0.0,
            max_results: int = 20) -> str:
        """Check current component-space bone clearances against active limits."""
        # MCP スキーマで任意引数にするため None を受け、未指定は従来の既定値として扱う
        filter_tag_names = list(filter_tag_names or [])
        root_bone = root_bone or ''
        if position_source not in ('pose', 'simulated', 'both'):
            raise ValueError('position_source must be pose, simulated or both.')
        if not isinstance(contact_threshold, (int, float)) or not math.isfinite(contact_threshold):
            raise ValueError('contact_threshold must be finite.')
        if not isinstance(max_results, int) or max_results < 0:
            raise ValueError('max_results must be nonnegative.')
        snapshot = _runtime_snapshot(actor_label, prefer_pie, filter_tag_names, root_bone)
        result = {key: snapshot[key] for key in
                  ('status', 'checked', 'excluded', 'reasons', 'world')}
        result['contacts'] = []
        result['by_source_type'] = {}
        result['notes'] = [
            'Displacement between pose and simulated positions alone does not prove collision caused it.',
            'Plane clearance is geometric and differs from the solver previous-frame crossing test.',
            'Values are centimeters in component space.']
        if result['status'] in ('not_found', 'no_nodes', 'not_evaluated'):
            return json.dumps(result)
        nodes = [n for n in _runtime_nodes(snapshot) if n['evaluated']]
        result['reasons'].extend(_runtime_limit_reasons(nodes))
        contacts = []
        excluded_disabled = 0
        excluded_pinned = 0
        for node in nodes:
            identity = _runtime_node_id(node)
            checked_bones, pinned_count = _collision_checked_bones(node)
            for limit in node['limits']:
                if not limit['enabled']:
                    excluded_disabled += len(node['bones'])
                    continue
                excluded_pinned += pinned_count
                source = limit['source_type']
                group = result['by_source_type'].setdefault(source, {
                    'checked': 0, 'minimum_clearance': None})
                for bone in checked_bones:
                    pose = _geom_point_clearance(limit, bone['pose_location'], bone['radius'])
                    simulated = _geom_point_clearance(limit, bone['location'], bone['radius'])
                    selected = (pose if position_source == 'pose' else simulated
                                if position_source == 'simulated' else min(pose, simulated))
                    result['checked'] += 1
                    group['checked'] += 1
                    group['minimum_clearance'] = (selected if group['minimum_clearance'] is None
                        else min(group['minimum_clearance'], selected))
                    if selected < contact_threshold:
                        contacts.append((selected, {
                            'node': identity, 'bone': bone['key'],
                            'limit': _runtime_limit_id(limit),
                            'pose_clearance': pose, 'simulated_clearance': simulated,
                            'displacement': _geom_norm(_geom_sub(
                                bone['location'], bone['pose_location']))}))
        result['excluded'] += excluded_disabled + excluded_pinned
        if excluded_disabled:
            result['reasons'].append('Disabled limits were excluded.')
        if any(bone.get('skip_simulate', False) for node in nodes for bone in node['bones']):
            result['reasons'].append('Bones pinned to the input pose (skip_simulate) are not checked because the collision step skips them.')
        result['contacts'] = [entry for _, entry in sorted(contacts, key=lambda pair: pair[0])[:max_results]]
        result['status'] = ('nothing_checked' if not result['checked'] else
                            'contact' if contacts else 'ok')
        if result['status'] == 'nothing_checked':
            result['reasons'].append('No bone and active limit pairs were available.')
        return json.dumps(result)

    @toolset_registry.tool_call
    @staticmethod
    def start_collision_penetration_sampler(
            actor_label: str, ring_root_bones: list[str], frames: int = 300,
            prefer_pie: bool = True,
            filter_tag_names: list[str] = [], root_bone: str | None = None,
            min_depth: int = 0, max_depth: int = -1, closed: bool = True, threshold: float = 1.0,
            warmup_frames: int = 0, fixed_frame_rate: float = 30.0,
            record_path: str | None = None,
            record_extra_bones: list[str] = [],
            extra_actor_labels: list[str] = []) -> str:
        """Sample fixed skirt centerlines after Slate ticks in component space.

        Pass [] for ring_root_bones to use the default segment set.
        fixed_frame_rate fixes game-time steps using the engine's fixed frame rate only
        during sampling; values <= 0 disable it, and positive values below 15 are invalid.
        The engine settings are restored on completion, stop, or error; real-time
        FPS may fall, making the scene appear slower.
        record_path writes a JSONL rest header and frames with game time and stale
        markers. record_extra_bones adds final component-space leg or body transforms.
        extra_actor_labels samples other actors in the same ticks and records each separately.
        """
        global _COLLISION_PENETRATION_SAMPLER
        # MCP スキーマで任意引数にするため None を受け、未指定は従来の既定値として扱う
        filter_tag_names = list(filter_tag_names or [])
        root_bone = root_bone or ''
        ring_root_bones = list(ring_root_bones)
        if not isinstance(frames, int) or frames < 1:
            raise ValueError('frames must be at least 1.')
        if not isinstance(warmup_frames, int) or warmup_frames < 0:
            raise ValueError('warmup_frames must be nonnegative.')
        if not isinstance(threshold, (int, float)) or not math.isfinite(threshold) or threshold < 0:
            raise ValueError('threshold must be finite and nonnegative.')
        if (not isinstance(fixed_frame_rate, (int, float)) or
                not math.isfinite(fixed_frame_rate) or
                0 < fixed_frame_rate < 15):
            raise ValueError('fixed_frame_rate must be finite and at least 15 when positive.')
        if (not isinstance(min_depth, int) or min_depth < 0 or
                not isinstance(max_depth, int) or
                (max_depth != -1 and max_depth < min_depth)):
            raise ValueError('Invalid depth range.')
        if (any(not isinstance(root, str) or not root for root in ring_root_bones) or
                len(set(ring_root_bones)) != len(ring_root_bones)):
            raise ValueError('ring_root_bones must contain unique nonempty names.')
        if record_path is not None:
            if (not isinstance(record_path, str) or not os.path.isabs(record_path) or
                    os.path.splitext(record_path)[1].lower() != '.jsonl' or
                    not os.path.isdir(os.path.dirname(record_path))):
                raise ValueError('record_path must be an absolute .jsonl path in an existing directory.')
        record_extra_bones = _record_extra_bone_names(record_extra_bones)
        # ufunction 経由では unreal.Array で届くため、型ではなく中身で検証する
        try:
            extra_actor_labels = list(extra_actor_labels or [])
        except TypeError as error:
            raise ValueError('extra_actor_labels must be a list of actor labels.') from error
        if (any(not isinstance(label, str) or not label for label in extra_actor_labels) or
                len(set(extra_actor_labels)) != len(extra_actor_labels) or
                actor_label in extra_actor_labels):
            raise ValueError('extra_actor_labels must contain unique nonempty labels other than actor_label.')
        _stop_collision_penetration_sampler_impl()
        used_paths = {os.path.normcase(record_path)} if record_path else set()
        actor_states = {}
        for label in [actor_label, *extra_actor_labels]:
            path = (record_path if label == actor_label else
                    _extra_record_path(record_path, label, used_paths) if record_path else None)
            try:
                actor_states[label] = _prepare_penetration_actor(
                    label, prefer_pie, filter_tag_names, root_bone, ring_root_bones,
                    min_depth, max_depth, closed, path, record_extra_bones, warmup_frames)
            except Exception as error:
                if label == actor_label:
                    raise
                actor_states[label] = {'targets': [], 'error': str(error),
                                       'start_result': {'status': 'error', 'error': str(error),
                                                        'checked': 0, 'excluded': 0,
                                                        'reasons': [], 'fixed_frame_rate': None}}
                if path:
                    actor_states[label].update(
                        record_path=path, record_frames=[],
                        record_header=_make_record_header(
                            label, actor_states[actor_label]['world'], warmup_frames,
                            ring_root_bones, record_extra_bones, closed))
        primary = actor_states[actor_label]
        result = primary['start_result']
        if not primary['targets']:
            result['actors'] = {label: dict(actor['start_result'])
                                for label, actor in actor_states.items()}
            _COLLISION_PENETRATION_SAMPLER = {
                'done': True, 'failed_start': result,
                'tick_handle': None, 'fixed_frame_rate': None}
            return json.dumps(result)
        _COLLISION_PENETRATION_SAMPLER = {
            'actor': actor_label, 'prefer_pie': prefer_pie,
            'root_bone': root_bone, 'world': primary['world'],
            'filter_tags': _make_tag_container(filter_tag_names),
            'actor_states': actor_states, 'frames': frames, 'frames_seen': 0,
            'warmup_frames': warmup_frames, 'threshold': threshold,
            'done': False, 'error': '', 'tick_handle': None,
            'fixed_frame_rate': None, 'notes': [],
            'cost_total_ms': 0.0, 'cost_max_ms': 0.0, 'cost_samples': 0}
        state = _COLLISION_PENETRATION_SAMPLER
        _enable_sampler_fixed_frame_rate(state, fixed_frame_rate)
        for actor in actor_states.values():
            if actor.get('record_header'):
                actor['record_header']['fixed_frame_rate'] = state['fixed_frame_rate']
        try:
            state['tick_handle'] = unreal.register_slate_post_tick_callback(
                _collision_penetration_sampler_tick)
        except Exception as error:
            state['error'] = str(error)
            _stop_collision_penetration_sampler_impl()
            return json.dumps(_penetration_sampler_result(state))
        result['fixed_frame_rate'] = state['fixed_frame_rate']
        result['notes'] = state['notes']
        if record_path:
            result['record_path'] = record_path
            result['recorded_frames'] = 0
        result['actors'] = {label: ({**actor['start_result'],
                                    'fixed_frame_rate': state['fixed_frame_rate'],
                                    'notes': state['notes'],
                                    **({'record_path': actor['record_path'], 'recorded_frames': 0}
                                       if actor.get('record_path') else {})}
                            if actor.get('targets') else actor['start_result'])
                            for label, actor in actor_states.items()}
        return json.dumps(result)

    @toolset_registry.tool_call
    @staticmethod
    def get_collision_penetration_sampler_result() -> str:
        """Return centerline penetration statistics with status running, ok,
        penetration, nothing_checked, stopped, error, or not_started.
        Statistics include positive_count and mean_all; top_events gives the ten
        largest samples at least five frames apart, with game time in seconds.
        cost_ms is mean/max sampling cost per tick, unchanged_frames counts frames
        with unchanged target positions, and fixed_frame_rate is the applied game-time rate.
        actors maps each label to its own statistics, events, status, and recording path.
        """
        state = _COLLISION_PENETRATION_SAMPLER
        if state is None:
            return json.dumps({'status': 'not_started'})
        return json.dumps(_penetration_sampler_result(state))

    @toolset_registry.tool_call
    @staticmethod
    def stop_collision_penetration_sampler() -> bool:
        """Stop sampling while retaining the result; idle stops are harmless."""
        _stop_collision_penetration_sampler_impl()
        return True

    @toolset_registry.tool_call
    @staticmethod
    def analyze_motion_recording(record_path: str, baseline_path: str | None = None,
                                 spike_degrees: float = 25.0) -> str:
        """Analyze skirt motion and optionally compare a tuning run with a baseline.

        Deviation measures swing from pose; lift includes per-frame tip maxima.
        The lifts flag uses lift.p50 only; lift.p90 and lift.max remain in the output.
        In spins, higher flare.p90 means the hem opens; higher rise.p50 with low
        flare means the tips ride up together. These values do not set flags.
        Stretch uses start rest spacing, collapse counts short vertical segments,
        and flips count rotations above 90 degrees. Stale frames are excluded;
        status is stale when they exceed 10 percent. Comparison flags are hints.
        """
        from . import motion_metrics

        try:
            recording = motion_metrics.load_recording(record_path)
            metrics = motion_metrics.compute_motion_metrics(recording, spike_degrees)
            result = {'status': 'ok', 'metrics': metrics, 'notes': []}
            stale = metrics['overall']['stale_frames']
            total = metrics['overall']['frame_count']
            if total and stale / total > 0.1:
                result['status'] = 'stale'
                result['notes'].append(
                    f'{stale}/{total} recorded frames repeated all bone positions; '
                    'motion scores exclude these frames and adjacent differences.')
            if baseline_path is not None:
                baseline = motion_metrics.compute_motion_metrics(
                    motion_metrics.load_recording(baseline_path), spike_degrees)
                result['comparison'] = motion_metrics.compare_motion_metrics(
                    metrics, baseline)
        except FileNotFoundError as error:
            result = {'status': 'not_found', 'notes': [str(error)]}
        except Exception as error:
            result = {'status': 'invalid', 'notes': [str(error)]}
        return json.dumps(result)

    @toolset_registry.tool_call
    @staticmethod
    def reset_dynamics_on_actor(actor_label: str, prefer_pie: bool = True) -> int:
        """Resets dynamics on matching components and returns their count.

        Use this to check bNeedWarmUp and bUseWarmUpWhenResetDynamics.
        """
        components = _find_skeletal_mesh_components_by_label(actor_label, prefer_pie)
        for component in components:
            component.reset_anim_instance_dynamics(unreal.TeleportType.RESET_PHYSICS)
        return len(components)

    @toolset_registry.tool_call
    @staticmethod
    def set_background_cpu_throttle(enabled: bool) -> bool:
        """Sets editor background CPU throttling and returns its previous value.

        SaveConfig is not called, so the ini is not changed; saving Editor
        Preferences makes it persistent. Set False before measuring PIE without
        focus, then restore the previous value afterward.
        """
        return unreal.KawaiiPhysicsEditorLibrary.set_background_cpu_throttle_enabled(enabled)

    @toolset_registry.tool_call
    @staticmethod
    def start_bone_sampler(
            actor_label: str,
            bone_pattern: str,
            frames: int = 120,
            prefer_pie: bool = True,
            space: str = 'world') -> str:
        """Records positions of matching bones for the next frames via a Slate post-tick callback.

        bone_pattern is a regular expression matched against socket and bone
        names; a running sampler is stopped first. space is "world" or
        "component" (relative to the SkeletalMeshComponent, so component
        movement does not show up).
        When the editor is not focused, call set_background_cpu_throttle(False) first.
        """
        global _BONE_SAMPLER

        if not bone_pattern:
            raise ValueError('bone_pattern must not be empty.')
        if frames < 1:
            raise ValueError('frames must be 1 or greater.')
        sampler_space = str(space or '').lower()
        if sampler_space not in _BONE_SAMPLER_SPACES:
            raise ValueError(f'Unknown space: {space}. Valid values: world, component.')

        components = _find_skeletal_mesh_components_by_label(actor_label, prefer_pie)
        targets = _make_bone_sampler_targets(components, bone_pattern)
        if not targets:
            raise ValueError(
                f'No bone matches pattern "{bone_pattern}" on actor: {actor_label}')

        _stop_bone_sampler_impl()
        _BONE_SAMPLER = {
            'actor': actor_label,
            'targets': targets,
            'bones': {key: _make_bone_sample_accumulator() for key, _, _ in targets},
            'frames': frames,
            'space': sampler_space,
            'frames_collected': 0,
            'done': False,
            'error': '',
            'tick_handle': None,
        }
        _BONE_SAMPLER['tick_handle'] = unreal.register_slate_post_tick_callback(
            _bone_sampler_tick)
        return json.dumps({
            'started': True,
            'actor': actor_label,
            'bones': [key for key, _, _ in targets],
            'frames': frames,
            'space': sampler_space,
        })

    @toolset_registry.tool_call
    @staticmethod
    def get_bone_sampler_result() -> str:
        """Returns the bone sampler result as JSON with per-bone min/max/mean, frame-to-frame RMS displacement and a NaN flag."""
        state = _BONE_SAMPLER
        if state is None:
            return json.dumps({'done': False, 'error': 'not started'})

        bones = {}
        for key, accumulator in state['bones'].items():
            step_count = max(accumulator['step_count'], 1)
            bones[key] = {
                'min': accumulator['min'] if accumulator['count'] else None,
                'max': accumulator['max'] if accumulator['count'] else None,
                'mean': ([value / accumulator['count'] for value in accumulator['sum']]
                         if accumulator['count'] else None),
                'rms_step': math.sqrt(accumulator['step_square_sum'] / step_count),
                'nan': accumulator['nan'],
            }

        result = {
            'done': state['done'],
            'frames_collected': state['frames_collected'],
            'space': state['space'],
            'bones': bones,
        }
        if state['error']:
            result['error'] = state['error']
        return json.dumps(result)

    @toolset_registry.tool_call
    @staticmethod
    def stop_bone_sampler() -> bool:
        """Stops the bone sampler; the collected result stays readable and stopping an idle sampler is not an error."""
        _stop_bone_sampler_impl()
        return True

    @toolset_registry.tool_call
    @staticmethod
    def start_physics_settings_multiplier_on_actor(
            actor_label: str,
            settings_json: str,
            duration: float,
            blend_in_time: float = 0.2,
            blend_out_time: float = 0.5,
            filter_tag_names: list[str] = [],
            filter_exact_match: bool = False,
            prefer_pie: bool = True) -> str:
        """Starts a temporary physics settings multiplier on every SkeletalMeshComponent of the matching actors.

        settings_json holds FKawaiiPhysicsSettingsMultiplier fields (Damping,
        Stiffness, WorldDampingLocation, WorldDampingRotation, Radius,
        LimitAngle); the JSON array result carries the stop handles.
        """
        # Python から None で呼ばれても動くよう空リストに揃える
        filter_tag_names = filter_tag_names or []
        settings_scale = _make_settings_multiplier(settings_json)
        components = _find_skeletal_mesh_components_by_label(actor_label, prefer_pie)
        filter_tags = _make_tag_container(filter_tag_names)

        results = []
        for component in components:
            count, handle = _unpack_count_and_out(
                unreal.KawaiiPhysicsLibrary.start_physics_settings_multiplier_on_component(
                    component,
                    settings_scale,
                    duration,
                    blend_in_time,
                    blend_out_time,
                    filter_tags,
                    filter_exact_match,
                )
            )
            results.append({
                'component': component.get_name(),
                'nodes': count,
                'handle': _handle_to_dict(handle),
            })
        return json.dumps(results)

    @toolset_registry.tool_call
    @staticmethod
    def stop_physics_settings_multiplier_on_actor(
            actor_label: str,
            handle_json: str,
            blend_out_time: float = 0.5,
            filter_tag_names: list[str] = [],
            filter_exact_match: bool = False,
            prefer_pie: bool = True) -> int:
        """Stops the physics settings multiplier matching the handle and returns the total number of stopped nodes.

        handle_json is the handle from start_physics_settings_multiplier_on_actor
        ({"id": <int>} or a bare integer).
        """
        # Python から None で呼ばれても動くよう空リストに揃える
        filter_tag_names = filter_tag_names or []
        handle = _handle_from_json(handle_json)
        components = _find_skeletal_mesh_components_by_label(actor_label, prefer_pie)
        filter_tags = _make_tag_container(filter_tag_names)

        stopped_count = 0
        for component in components:
            stopped_count += int(
                unreal.KawaiiPhysicsLibrary.stop_physics_settings_multiplier_on_component(
                    component,
                    handle,
                    filter_tags,
                    filter_exact_match,
                    blend_out_time,
                )
            )
        return stopped_count

    @toolset_registry.tool_call
    @staticmethod
    def start_procedural_wind_gust_on_actor(
            actor_label: str,
            strength: float,
            duration: float,
            rise_time: float = 0.1,
            decay_time: float = 0.3,
            direction: list[float] = [],
            filter_tag_names: list[str] = [],
            filter_exact_match: bool = False,
            prefer_pie: bool = True) -> str:
        """Starts a runtime ProceduralWind gust on every SkeletalMeshComponent of the matching actors.

        Omitting direction (empty list) inherits the authored ProceduralWind
        direction; pass a world space vector such as [1, 0, 0] for an explicit
        direction. The JSON array result carries the stop handles.
        """
        # Python から None で呼ばれても動くよう空リストに揃える
        direction = direction or []
        filter_tag_names = filter_tag_names or []
        gust_direction = _make_gust_direction(direction)
        components = _find_skeletal_mesh_components_by_label(actor_label, prefer_pie)
        filter_tags = _make_tag_container(filter_tag_names)

        results = []
        for component in components:
            count, handle = _unpack_count_and_out(
                unreal.KawaiiPhysicsLibrary.start_procedural_wind_gust_on_component(
                    component,
                    strength,
                    duration,
                    rise_time,
                    decay_time,
                    filter_tags,
                    filter_exact_match,
                    gust_direction,
                )
            )
            results.append({
                'component': component.get_name(),
                'nodes': count,
                'handle': _handle_to_dict(handle),
            })
        return json.dumps(results)

    @toolset_registry.tool_call
    @staticmethod
    def stop_transient_external_force_on_actor(
            actor_label: str,
            handle_json: str,
            blend_out_time: float = 0.5,
            filter_tag_names: list[str] = [],
            filter_exact_match: bool = False,
            prefer_pie: bool = True) -> int:
        """Stops the transient external force matching the handle and returns the total number of stopped nodes.

        handle_json is the handle from start_procedural_wind_gust_on_actor
        ({"id": <int>} or a bare integer).
        """
        # Python から None で呼ばれても動くよう空リストに揃える
        filter_tag_names = filter_tag_names or []
        handle = _handle_from_json(handle_json)
        components = _find_skeletal_mesh_components_by_label(actor_label, prefer_pie)
        filter_tags = _make_tag_container(filter_tag_names)

        stopped_count = 0
        for component in components:
            stopped_count += int(
                unreal.KawaiiPhysicsLibrary.stop_transient_external_force_on_component(
                    component,
                    handle,
                    filter_tags,
                    filter_exact_match,
                    blend_out_time,
                )
            )
        return stopped_count

    @toolset_registry.tool_call
    @staticmethod
    def set_alpha_on_actor(
            actor_label: str,
            alpha: float,
            filter_tag_names: list[str] = [],
            filter_exact_match: bool = False,
            prefer_pie: bool = True) -> int:
        """Sets the KawaiiPhysics alpha on every SkeletalMeshComponent of the matching actors and returns the number of updated components."""
        # Python から None で呼ばれても動くよう空リストに揃える
        filter_tag_names = filter_tag_names or []
        components = _find_skeletal_mesh_components_by_label(actor_label, prefer_pie)
        filter_tags = _make_tag_container(filter_tag_names)

        updated_count = 0
        for component in components:
            if _set_alpha_on_component(component, alpha, filter_tags, filter_exact_match):
                updated_count += 1
        return updated_count

    @toolset_registry.tool_call
    @staticmethod
    def get_alpha_on_actor(
            actor_label: str,
            filter_tag_names: list[str] = [],
            filter_exact_match: bool = False,
            prefer_pie: bool = True) -> float:
        """Gets the KawaiiPhysics alpha from the first matching SkeletalMeshComponent; returns -1.0 when no value is available."""
        # Python から None で呼ばれても動くよう空リストに揃える
        filter_tag_names = filter_tag_names or []
        components = _find_skeletal_mesh_components_by_label(actor_label, prefer_pie)
        filter_tags = _make_tag_container(filter_tag_names)

        for component in components:
            alpha = _get_alpha_on_component(component, filter_tags, filter_exact_match)
            if alpha is not None:
                return float(alpha)
        return -1.0

    # ===== Diagnostics: Simple World Collision =====

    @toolset_registry.tool_call
    @staticmethod
    def get_simple_world_collision_debug_info(
            skeletal_mesh_component: unreal.SkeletalMeshComponent
    ) -> unreal.KawaiiPhysicsSimpleWorldCollisionDebugInfo:
        """Returns Simple World Collision diagnostics for a SkeletalMeshComponent.

        Thin wrapper over a GameThread-only diagnostics API. While PIE/SIE is
        running, pass a component that belongs to the PIE world. An unregistered
        component is not an error and returns has_entry == False.
        """
        _raise_for_invalid_object(
            skeletal_mesh_component,
            'skeletal_mesh_component',
        )

        result = unreal.KawaiiPhysicsLibrary.get_simple_world_collision_debug_info(
            skeletal_mesh_component)
        # Entry が無いだけならエラーにせず has_entry == False の既定値を返す
        return _unpack_bool_out(
            result,
            unreal.KawaiiPhysicsSimpleWorldCollisionDebugInfo(),
        )

    @toolset_registry.tool_call
    @staticmethod
    def find_simple_world_collision_debug_info(
            actor_label: str,
            prefer_pie: bool = True
    ) -> list[unreal.KawaiiPhysicsSimpleWorldCollisionDebugInfo]:
        """Finds actors by label (PIE world first when prefer_pie) and returns Simple World Collision diagnostics for each SkeletalMeshComponent.

        Lets MCP clients read gather state without injecting Python.
        """
        components = _find_skeletal_mesh_components_by_label(actor_label, prefer_pie)
        debug_infos = []
        for component in components:
            result = (
                unreal.KawaiiPhysicsLibrary
                .get_simple_world_collision_debug_info(component)
            )
            debug_infos.append(_unpack_bool_out(
                result,
                unreal.KawaiiPhysicsSimpleWorldCollisionDebugInfo(),
            ))
        return debug_infos

    @toolset_registry.tool_call
    @staticmethod
    def get_simple_world_collider_count_on_actor(
            actor_label: str,
            filter_tag_names: list[str] = [],
            filter_exact_match: bool = False,
            prefer_pie: bool = True) -> int:
        """Returns the total number of Simple World Collision colliders across every SkeletalMeshComponent of the matching actors."""
        # Python から None で呼ばれても動くよう空リストに揃える
        filter_tag_names = filter_tag_names or []
        components = _find_skeletal_mesh_components_by_label(actor_label, prefer_pie)
        filter_tags = _make_tag_container(filter_tag_names)

        collider_count = 0
        for component in components:
            collider_count += int(unreal.KawaiiPhysicsLibrary.get_simple_world_collider_count_on_component(
                component,
                filter_tags,
                filter_exact_match,
            ))
        return collider_count
