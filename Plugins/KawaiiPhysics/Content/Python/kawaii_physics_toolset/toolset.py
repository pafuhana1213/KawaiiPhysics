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
        return result[1]
    if result is None:
        return default
    return result


def _unpack_count_and_out(result: object) -> tuple[int, object]:
    # int32 戻り値 + out 引数は (count, out) のタプルで返る。
    if not isinstance(result, tuple) or len(result) != 2:
        raise RuntimeError(f'Unexpected return value: {result}')
    return int(result[0]), result[1]


def _resolve_world(prefer_pie: bool) -> unreal.World:
    editor_subsystem = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    if editor_subsystem is None:
        raise RuntimeError('No world available.')

    world = editor_subsystem.get_game_world() if prefer_pie else None
    if world is None:
        # get_editor_world() は PIE 中に None を返してログを出すため、PIE ワールドが取れたときは呼ばない
        world = editor_subsystem.get_editor_world()
    if world is None:
        raise RuntimeError('No world available.')
    return world


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
                unreal.RelativeTransformSpace.RTS_WORLD,
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
            skeleton: unreal.Skeleton) -> unreal.AnimBlueprint:
        """Creates an AnimBlueprint with a target skeleton."""
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
        return asset

    @toolset_registry.tool_call
    @staticmethod
    def add_kawaii_physics_nodes(
            anim_blueprint: unreal.AnimBlueprint,
            requests: list[unreal.KawaiiPhysicsNodePlacementRequest],
            match_key: unreal.KawaiiPhysicsPlacementMatchKey,
            graph_name: str,
            comment: str = '',
            prompt: str = '') -> list[unreal.KawaiiPhysicsGraphNodeHandle]:
        """Adds or updates KawaiiPhysics nodes; auto_connect wires before Result.

        A non-empty comment creates an MCP comment frame with the configured
        prefix. The prompt is stored in that frame's Details. Requests may set
        placement_direction; project settings also control node direction/wrap/spacing.
        """
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
    def get_anim_graph_comments(
            anim_blueprint: unreal.AnimBlueprint,
            graph_name: str = '') -> list[unreal.KawaiiPhysicsAnimGraphCommentInfo]:
        """Returns comment nodes in the AnimGraph."""
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
        """Sets a graph node property from a string value."""
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
    def get_graph_node_property(
            handle: unreal.KawaiiPhysicsGraphNodeHandle,
            property_name: str) -> str:
        """Gets a graph node property as a string value."""
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
    def set_graph_nodes_property(
            anim_blueprint: unreal.AnimBlueprint,
            property_name: str,
            value: str,
            filter_tag_names: list[str] = [],
            filter_exact_match: bool = False) -> int:
        """Sets a FAnimNode_KawaiiPhysics property from a string on every KawaiiPhysics node in the AnimBlueprint (optionally filtered by tags).

        Returns the number of nodes updated.
        """
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
            filter_tag_names: list[str] = [],
            filter_exact_match: bool = False) -> dict[str, str]:
        """Gets a FAnimNode_KawaiiPhysics property as a string from every KawaiiPhysics node in the AnimBlueprint (optionally filtered by tags).

        Keys are the node GUID when it can be read, otherwise a stable
        node name or "node<index>" fallback.
        """
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

        container = unreal.KawaiiPhysicsEditorLibrary.make_gameplay_tag_container_from_names(
            [unreal.Name(tag_name)])
        if container is None:
            raise ValueError(f'No valid gameplay tags were resolved from: {tag_name}')
        tags = container.get_editor_property('gameplay_tags')
        if len(tags) == 0:
            raise ValueError(f'No valid gameplay tags were resolved from: {tag_name}')

        ok = unreal.KawaiiPhysicsEditorLibrary.set_graph_node_tag(
            handle,
            tags[0],
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
        """Executes a console command in the PIE world when available, otherwise the editor world.

        Also usable for CVars, stat commands and `py "<file>"`; returns True
        because the console reports failures only through the log.
        """
        if not command:
            raise ValueError('command must not be empty.')

        world = _resolve_world(prefer_pie)
        unreal.SystemLibrary.execute_console_command(world, command)
        return True

    @toolset_registry.tool_call
    @staticmethod
    def find_kawaii_physics_actors(
            actor_label: str = '',
            prefer_pie: bool = True) -> list[str]:
        """Lists SkeletalMeshComponents that run an AnimBlueprint as "<actor_label>|<component_name>|<anim_class_name>".

        An empty actor_label scans every actor; components without an
        AnimClass are skipped.
        """
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
    def start_bone_sampler(
            actor_label: str,
            bone_pattern: str,
            frames: int = 120,
            prefer_pie: bool = True) -> str:
        """Records world positions of matching bones for the next frames via a Slate post-tick callback.

        bone_pattern is a regular expression matched against socket and bone
        names; a running sampler is stopped first.
        """
        global _BONE_SAMPLER

        if not bone_pattern:
            raise ValueError('bone_pattern must not be empty.')
        if frames < 1:
            raise ValueError('frames must be 1 or greater.')

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
            direction: list[float] = [1.0, 0.0, 0.0],
            filter_tag_names: list[str] = [],
            filter_exact_match: bool = False,
            prefer_pie: bool = True) -> str:
        """Starts a runtime ProceduralWind gust on every SkeletalMeshComponent of the matching actors.

        direction is a world space vector; an empty list inherits the authored
        ProceduralWind direction. The JSON array result carries the stop handles.
        """
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
        components = _find_skeletal_mesh_components_by_label(actor_label, prefer_pie)
        filter_tags = _make_tag_container(filter_tag_names)

        updated_count = 0
        for component in components:
            if unreal.KawaiiPhysicsLibrary.set_alpha_on_component(
                    component,
                    alpha,
                    filter_tags,
                    filter_exact_match,
            ):
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
        components = _find_skeletal_mesh_components_by_label(actor_label, prefer_pie)
        filter_tags = _make_tag_container(filter_tag_names)

        for component in components:
            alpha = _unpack_bool_out(
                unreal.KawaiiPhysicsLibrary.get_alpha_on_component(
                    component,
                    filter_tags,
                    filter_exact_match,
                ),
                None,
            )
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
