from __future__ import annotations

import os

import unreal

import toolset_registry


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


def _validate_requests_or_raise(
        anim_blueprint: unreal.AnimBlueprint,
        requests: list[unreal.KawaiiPhysicsNodePlacementRequest]) -> list[str]:
    errors = KawaiiPhysicsToolset.validate_placement_requests(anim_blueprint, requests)
    blocking_errors = [
        error for error in errors
        if not str(error).startswith('Warning:')
    ]
    if blocking_errors:
        raise ValueError(
            'Invalid KawaiiPhysics placement requests: ' +
            '; '.join(blocking_errors))
    return errors


@unreal.uclass()
class KawaiiPhysicsToolset(unreal.ToolsetDefinition):
    """Sets up, applies presets to, and audits KawaiiPhysics AnimGraph nodes.

    Tools wrap KawaiiPhysics editor scripting APIs for automation agents.
    """

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
            upsert_key: unreal.KawaiiPhysicsPlacementUpsertKey,
            graph_name: str,
            comment: str = '',
            prompt: str = '') -> list[unreal.KawaiiPhysicsGraphNodeHandle]:
        """Adds or upserts KawaiiPhysics nodes; auto_connect wires before Result.

        A non-empty comment creates an MCP comment frame with the configured
        prefix. The prompt is stored in that frame's Details. Requests may set
        placement_direction; project settings also control node direction/wrap/spacing.
        """
        _raise_for_invalid_object(anim_blueprint, 'anim_blueprint')
        _validate_requests_or_raise(anim_blueprint, requests)

        return unreal.KawaiiPhysicsEditorLibrary.add_kawaii_physics_nodes(
            anim_blueprint,
            requests,
            upsert_key,
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
        _raise_for_invalid_object(anim_blueprint, 'anim_blueprint')
        filter_tags = _make_tag_container(filter_tag_names)
        return unreal.KawaiiPhysicsEditorLibrary.collect_kawaii_physics_graph_nodes(
            anim_blueprint,
            filter_tags,
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

        ok = unreal.KawaiiPhysicsEditorLibrary.set_graph_node_property_by_string(
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

        ok = unreal.KawaiiPhysicsEditorLibrary.set_preset_node_property_by_string(
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
    def reapply_preset_to_project(
            preset: unreal.KawaiiPhysicsPresetDataAsset,
            dry_run: bool,
            check_out_files: bool) -> list[unreal.KawaiiPhysicsNodeAuditEntry]:
        """Reapplies a preset and returns the audit report, not the update count."""
        _raise_for_invalid_object(preset, 'preset')

        result = unreal.KawaiiPhysicsEditorLibrary.reapply_preset_to_project(
            preset,
            dry_run,
            check_out_files,
        )
        if not isinstance(result, tuple) or len(result) != 2:
            raise RuntimeError(
                'Unexpected return value from reapply_preset_to_project.')
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
    def resolve_bones_by_pattern(
            skeleton: unreal.Skeleton,
            pattern: str) -> list[str]:
        """Resolves reference bone names by regex; empty input returns no names."""
        _raise_for_invalid_object(skeleton, 'skeleton')
        names = unreal.KawaiiPhysicsEditorLibrary.resolve_bones_by_pattern(
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
        _raise_for_invalid_object(anim_blueprint, 'anim_blueprint')

        result = unreal.KawaiiPhysicsEditorLibrary.validate_placement_requests(
            anim_blueprint,
            requests,
        )
        if result is None:
            raise RuntimeError(
                'Unexpected return value from validate_placement_requests.')
        return [str(error) for error in result]
