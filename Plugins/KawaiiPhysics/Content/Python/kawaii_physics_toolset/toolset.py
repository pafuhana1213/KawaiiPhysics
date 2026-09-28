from __future__ import annotations

import json
import math
import os
import re

import unreal

import toolset_registry


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


def _set_graph_node_properties_impl(
        handle: unreal.KawaiiPhysicsGraphNodeHandle,
        properties: list[tuple[str, str]]) -> list[str]:
    # tool_call ラッパは例外を握るため、add_kawaii_physics_node からもこの素の関数を使う
    _raise_for_invalid_handle(handle, 'handle')
    set_names = []
    for name, text in properties:
        ok = unreal.KawaiiPhysicsEditorLibrary.set_graph_node_property_from_string(
            handle,
            unreal.Name(name),
            text,
        )
        if not ok:
            raise RuntimeError(
                f'Unable to set KawaiiPhysics graph node property: {name}')
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
    return {'id': int(handle.get_editor_property('id'))}


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
    handle.set_editor_property('id', value)
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
    targets = []
    for component, bone_name in matches:
        key = bone_name if name_counts[bone_name] == 1 else f'{component.get_name()}.{bone_name}'
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
        state['error'] = str(error)
        state['done'] = True

    if state['frames_collected'] >= state['frames']:
        state['done'] = True
    if state['done']:
        _stop_bone_sampler_impl()


@unreal.uclass()
class KawaiiPhysicsToolset(unreal.ToolsetDefinition):
    """Sets up, applies presets to, and audits KawaiiPhysics AnimGraph nodes.

    Tools wrap KawaiiPhysics editor scripting APIs for automation agents.
    """

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
            exclude_bones: list[str] | None = None,
            additional_root_bones: list[str] | None = None,
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
        a bool. Stops at the first property that fails; earlier ones stay set.
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
            rotation: list[float] | None = None,
            radius: float = 5.0,
            radius1: float = 5.0,
            length: float = 10.0,
            extent: list[float] | None = None,
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
        # MCP スキーマで任意引数にするため None を受け、未指定は従来の既定値として扱う
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
            filter_tag_names: list[str] | None = None,
            filter_exact_match: bool = False) -> int:
        """Sets a FAnimNode_KawaiiPhysics property from a string on every KawaiiPhysics node in the AnimBlueprint (optionally filtered by tags).

        Returns the number of nodes updated.
        """
        # MCP スキーマで任意引数にするため None を受け、未指定は従来の既定値として扱う
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
            ok = unreal.KawaiiPhysicsEditorLibrary.set_graph_node_property_from_string(
                handle,
                unreal.Name(property_name),
                value,
            )
            if not ok:
                raise RuntimeError(
                    f'Unable to set {property_name} on node '
                    f'{index + 1}/{len(handles)}')
        return len(handles)

    @toolset_registry.tool_call
    @staticmethod
    def get_graph_nodes_property(
            anim_blueprint: unreal.AnimBlueprint,
            property_name: str,
            filter_tag_names: list[str] | None = None,
            filter_exact_match: bool = False) -> dict[str, str]:
        """Gets a FAnimNode_KawaiiPhysics property as a string from every KawaiiPhysics node in the AnimBlueprint (optionally filtered by tags).

        Keys are the node GUID when it can be read, otherwise a stable
        node name or "node<index>" fallback.
        """
        # MCP スキーマで任意引数にするため None を受け、未指定は従来の既定値として扱う
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
            filter_tag_names: list[str] | None = None,
            filter_exact_match: bool = False) -> str:
        """Returns a JSON array of KawaiiPhysics nodes with their FAnimNode_KawaiiPhysics settings and external forces.

        settings holds ImportText values of the properties specific to the
        KawaiiPhysics node (FAnimNode_KawaiiPhysics); base class properties
        such as Alpha, AlphaInputType and LODThreshold are not included. Use
        describe_kawaii_physics_runtime_on_actor for the runtime Alpha.
        Unreadable settings are null; external_forces is a JSON array.
        """
        # MCP スキーマで任意引数にするため None を受け、未指定は空フィルタとして扱う
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
            sample_count = max(accumulator['count'], 1)
            step_count = max(accumulator['step_count'], 1)
            bones[key] = {
                'min': accumulator['min'],
                'max': accumulator['max'],
                'mean': [value / sample_count for value in accumulator['sum']],
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
            filter_tag_names: list[str] | None = None,
            filter_exact_match: bool = False,
            prefer_pie: bool = True) -> str:
        """Starts a temporary physics settings multiplier on every SkeletalMeshComponent of the matching actors.

        settings_json holds FKawaiiPhysicsSettingsMultiplier fields (Damping,
        Stiffness, WorldDampingLocation, WorldDampingRotation, Radius,
        LimitAngle); the JSON array result carries the stop handles.
        """
        # MCP スキーマで任意引数にするため None を受け、未指定は従来の既定値として扱う
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
            filter_tag_names: list[str] | None = None,
            filter_exact_match: bool = False,
            prefer_pie: bool = True) -> int:
        """Stops the physics settings multiplier matching the handle and returns the total number of stopped nodes.

        handle_json is the handle from start_physics_settings_multiplier_on_actor
        ({"id": <int>} or a bare integer).
        """
        # MCP スキーマで任意引数にするため None を受け、未指定は従来の既定値として扱う
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
            direction: list[float] | None = None,
            filter_tag_names: list[str] | None = None,
            filter_exact_match: bool = False,
            prefer_pie: bool = True) -> str:
        """Starts a runtime ProceduralWind gust on every SkeletalMeshComponent of the matching actors.

        Omitting direction (empty list) inherits the authored ProceduralWind
        direction; pass a world space vector such as [1, 0, 0] for an explicit
        direction. The JSON array result carries the stop handles.
        """
        # MCP スキーマで任意引数にするため None を受け、未指定は従来の既定値として扱う
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
            filter_tag_names: list[str] | None = None,
            filter_exact_match: bool = False,
            prefer_pie: bool = True) -> int:
        """Stops the transient external force matching the handle and returns the total number of stopped nodes.

        handle_json is the handle from start_procedural_wind_gust_on_actor
        ({"id": <int>} or a bare integer).
        """
        # MCP スキーマで任意引数にするため None を受け、未指定は従来の既定値として扱う
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
            filter_tag_names: list[str] | None = None,
            filter_exact_match: bool = False,
            prefer_pie: bool = True) -> int:
        """Sets the KawaiiPhysics alpha on every SkeletalMeshComponent of the matching actors and returns the number of updated components."""
        # MCP スキーマで任意引数にするため None を受け、未指定は従来の既定値として扱う
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
            filter_tag_names: list[str] | None = None,
            filter_exact_match: bool = False,
            prefer_pie: bool = True) -> float:
        """Gets the KawaiiPhysics alpha from the first matching SkeletalMeshComponent; returns -1.0 when no value is available."""
        # MCP スキーマで任意引数にするため None を受け、未指定は従来の既定値として扱う
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
            filter_tag_names: list[str] | None = None,
            filter_exact_match: bool = False,
            prefer_pie: bool = True) -> int:
        """Returns the total number of Simple World Collision colliders across every SkeletalMeshComponent of the matching actors."""
        # MCP スキーマで任意引数にするため None を受け、未指定は従来の既定値として扱う
        filter_tag_names = filter_tag_names or []
        components = _find_skeletal_mesh_components_by_label(actor_label, prefer_pie)
        filter_tags = _make_tag_container(filter_tag_names)

        collider_count = 0
        for component in components:
            get_collider_count = getattr(
                unreal.KawaiiPhysicsLibrary,
                'get_simple_world_collider_count_on_component',
                None,
            )
            if get_collider_count is None:
                raise RuntimeError(
                    'GetSimpleWorldColliderCountOnComponent is not available '
                    'in this build.')
            collider_count += int(get_collider_count(
                component,
                filter_tags,
                filter_exact_match,
            ))
        return collider_count
