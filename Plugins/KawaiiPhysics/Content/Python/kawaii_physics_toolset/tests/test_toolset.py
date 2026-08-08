from __future__ import annotations

import unittest

import unreal

from kawaii_physics_toolset.toolset import KawaiiPhysicsToolset
from toolset_registry.tests.toolset_testcase import ToolCallTestCase


TEST_FOLDER = '/Game/Test/PyToolset/'
GRAYCHAN_SKELETON_PATH = '/Game/KawaiiPhysicsSample/GrayChan/Mesh/GrayChan_Skeleton'
HAIR_PRESET_PATH = '/Game/KawaiiPhysicsSample/Presets/KPP_Hair_Soft'
CHAIN_SKELETON_PATH = '/Game/KawaiiPhysicsSample/Chain/S_Chain_Skeleton'
REAPPLY_TAG = 'KawaiiPhysics.Test.Reapply'
HAIR_TAG = 'KawaiiPhysics.Hair.Soft'


def _load_asset_checked(asset_path: str) -> unreal.Object:
    asset = unreal.EditorAssetLibrary.load_asset(asset_path)
    if asset is None:
        raise RuntimeError(f'Unable to load test asset: {asset_path}')
    return asset


def _delete_test_folder() -> None:
    eas = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    assert eas
    if eas.does_directory_exist(TEST_FOLDER):
        eas.delete_directory(TEST_FOLDER)


def _make_gameplay_tag(tag_name: str) -> unreal.GameplayTag:
    container = unreal.KawaiiPhysicsEditorLibrary.make_gameplay_tag_container_from_names([unreal.Name(tag_name)])
    # UE Pythonのbool+out剥がしにより成功時はGameplayTagContainerが直接返る（失敗時None）
    if container is None:
        raise RuntimeError(f'failed to resolve gameplay tag: {tag_name}')
    tags = container.get_editor_property('gameplay_tags')
    if len(tags) == 0:
        raise RuntimeError(f'no gameplay tags resolved from: {tag_name}')
    return tags[0]


def _tag_to_string(tag: unreal.GameplayTag) -> str:
    try:
        return str(tag.get_editor_property('tag_name'))
    except Exception:
        return str(tag)


def _target_tags_contain(target_tags: unreal.GameplayTagContainer, tag_name: str) -> bool:
    if hasattr(target_tags, 'has_tag_exact'):
        try:
            if target_tags.has_tag_exact(_make_gameplay_tag(tag_name)):
                return True
        except Exception:
            pass
    return tag_name in str(target_tags)


def _make_request(
        root_bone_pattern: str = 'Twintail[A-E]_L',
        kawaii_physics_tag: str = '',
        preset: unreal.KawaiiPhysicsPresetDataAsset | None = None) -> unreal.KawaiiPhysicsNodePlacementRequest:
    request = unreal.KawaiiPhysicsNodePlacementRequest()
    request.set_editor_property('root_bone_pattern', root_bone_pattern)
    if kawaii_physics_tag:
        request.set_editor_property(
            'kawaii_physics_tag',
            _make_gameplay_tag(kawaii_physics_tag),
        )
    if preset is not None:
        request.set_editor_property('preset', preset)
    return request


class KawaiiPhysicsToolsetTestCase(ToolCallTestCase):
    """Test KawaiiPhysicsToolset tool calls."""

    def setUp(self):
        super().setUp()
        _delete_test_folder()
        eas = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
        assert eas
        eas.make_directory(TEST_FOLDER)
        self.skeleton = _load_asset_checked(GRAYCHAN_SKELETON_PATH)
        self.assertIsInstance(self.skeleton, unreal.Skeleton)

    def tearDown(self):
        _delete_test_folder()
        super().tearDown()

    def _create_anim_blueprint(self, asset_name: str = 'ABP_KawaiiPhysicsToolsetTest') -> unreal.AnimBlueprint:
        anim_blueprint = KawaiiPhysicsToolset.create_anim_blueprint(
            TEST_FOLDER,
            asset_name,
            self.skeleton,
        )
        self.assertIsInstance(anim_blueprint, unreal.AnimBlueprint)
        return anim_blueprint

    def _place_test_node(
            self,
            anim_blueprint: unreal.AnimBlueprint | None = None,
            request: unreal.KawaiiPhysicsNodePlacementRequest | None = None,
            upsert_key: unreal.KawaiiPhysicsPlacementUpsertKey = unreal.KawaiiPhysicsPlacementUpsertKey.NONE
    ) -> unreal.KawaiiPhysicsGraphNodeHandle:
        if anim_blueprint is None:
            anim_blueprint = self._create_anim_blueprint()
        if request is None:
            request = _make_request()
        handles = KawaiiPhysicsToolset.add_kawaii_physics_nodes(
            anim_blueprint,
            [request],
            upsert_key,
            '',
        )
        self.assertEqual(len(handles), 1)
        return handles[0]

    def _export_test_preset(
            self,
            handle: unreal.KawaiiPhysicsGraphNodeHandle,
            asset_name: str) -> unreal.KawaiiPhysicsPresetDataAsset:
        preset_path = TEST_FOLDER + asset_name
        preset = KawaiiPhysicsToolset.export_graph_node_to_preset(handle, preset_path)
        self.assertIsInstance(preset, unreal.KawaiiPhysicsPresetDataAsset)
        self.assertTrue(unreal.EditorAssetLibrary.does_asset_exist(preset_path))
        return preset

    def _set_graph_node_property(
            self,
            handle: unreal.KawaiiPhysicsGraphNodeHandle,
            property_name: str,
            value: str) -> None:
        self.assertTrue(unreal.KawaiiPhysicsEditorLibrary.set_graph_node_property_by_string(
            handle,
            unreal.Name(property_name),
            value,
        ))

    def _save_asset(self, asset: unreal.Object) -> None:
        self.assertTrue(unreal.EditorAssetLibrary.save_loaded_asset(asset, False))

    def _audit_entries(
            self,
            filter_tag_names: list[str] | None = None,
            filter_exact_match: bool = False) -> list[unreal.KawaiiPhysicsNodeAuditEntry]:
        return KawaiiPhysicsToolset.audit_kawaii_physics_nodes(
            [TEST_FOLDER],
            filter_tag_names or [],
            filter_exact_match,
        )

    def _entry_root_bone_name(self, entry: unreal.KawaiiPhysicsNodeAuditEntry) -> str:
        return str(entry.get_editor_property('root_bone_name'))

    def _entry_tag_name(self, entry: unreal.KawaiiPhysicsNodeAuditEntry) -> str:
        return _tag_to_string(entry.get_editor_property('kawaii_physics_tag'))

    def _entry_diff_names(self, entry: unreal.KawaiiPhysicsNodeAuditEntry) -> list[str]:
        return [str(name) for name in entry.get_editor_property('diff_properties')]

    def test_create_anim_blueprint(self):
        anim_blueprint = self._create_anim_blueprint()
        self.assertEqual(anim_blueprint.get_editor_property('target_skeleton'), self.skeleton)

    def test_create_anim_blueprint_skeleton_none_raises(self):
        with self.assertToolRaisesRuntimeError():
            KawaiiPhysicsToolset.create_anim_blueprint(TEST_FOLDER, 'ABP_Invalid', None)

    def test_validate_add_collect_success(self):
        anim_blueprint = self._create_anim_blueprint()
        request = _make_request()

        messages = list(KawaiiPhysicsToolset.validate_placement_requests(anim_blueprint, [request]))
        # Twintail[A-E]_L はチェーン全体にマッチするため入れ子ルート警告(非ブロッキング)が返る
        blocking_errors = [m for m in messages if not str(m).startswith('Warning:')]
        self.assertEqual(blocking_errors, [])
        self.assertTrue(any('descendant' in str(m) for m in messages))

        handles = KawaiiPhysicsToolset.add_kawaii_physics_nodes(
            anim_blueprint,
            [request],
            unreal.KawaiiPhysicsPlacementUpsertKey.NONE,
            '',
        )
        self.assertEqual(len(handles), 1)

        collected = KawaiiPhysicsToolset.collect_kawaii_physics_graph_nodes(
            anim_blueprint,
            [],
            False,
        )
        self.assertEqual(len(collected), 1)

    def test_collect_with_valid_filter_tags_succeeds(self):
        anim_blueprint = self._create_anim_blueprint()

        collected = KawaiiPhysicsToolset.collect_kawaii_physics_graph_nodes(
            anim_blueprint,
            ['KawaiiPhysics.Hair', 'KawaiiPhysics.Hair.Soft'],
            False,
        )

        self.assertEqual(len(collected), 0)

    def test_collect_with_invalid_filter_tags_raises(self):
        anim_blueprint = self._create_anim_blueprint()

        with self.assertToolRaisesRuntimeError() as cm:
            KawaiiPhysicsToolset.collect_kawaii_physics_graph_nodes(
                anim_blueprint,
                ['KawaiiPhysics.NotRegistered', 'KawaiiPhysics.AlsoNotRegistered'],
                False,
            )
        self.assertIn('No valid gameplay tags were resolved', str(cm.exception))

    def test_validate_missing_root_bone_reports_error(self):
        anim_blueprint = self._create_anim_blueprint()
        request = unreal.KawaiiPhysicsNodePlacementRequest()
        request.set_editor_property(
            'root_bone_name',
            unreal.Name('TotallyBogusBone_XYZ'),
        )

        errors = KawaiiPhysicsToolset.validate_placement_requests(anim_blueprint, [request])

        self.assertNotEqual(errors, [])
        self.assertIn('TotallyBogusBone_XYZ', '; '.join(errors))

    def test_set_get_graph_node_property_round_trip(self):
        handle = self._place_test_node()

        self.assertTrue(KawaiiPhysicsToolset.is_graph_node_handle_valid(handle))
        self.assertFalse(KawaiiPhysicsToolset.is_graph_node_handle_valid(
            unreal.KawaiiPhysicsGraphNodeHandle(),
        ))
        KawaiiPhysicsToolset.set_graph_node_property(handle, 'WindScale', '3.25')
        value = KawaiiPhysicsToolset.get_graph_node_property(handle, 'WindScale')

        self.assertAlmostEqual(float(value), 3.25)

    def test_set_graph_node_property_denied_raises(self):
        handle = self._place_test_node()

        with self.assertToolRaisesRuntimeError() as cm:
            KawaiiPhysicsToolset.set_graph_node_property(
                handle,
                'ExternalForces',
                '()',
            )
        self.assertIn('ExternalForces', str(cm.exception))

    def test_set_get_preset_node_property_round_trip(self):
        handle = self._place_test_node()
        preset = self._export_test_preset(handle, 'KPP_PresetPropertyRoundTrip')
        physics_settings_value = (
            '(Damping=0.33,Stiffness=0.44,WorldDampingLocation=0.55,'
            'WorldDampingRotation=0.66,Radius=7.0,LimitAngle=45.0)'
        )

        KawaiiPhysicsToolset.set_preset_node_property(
            preset,
            'PhysicsSettings',
            physics_settings_value,
        )
        value = KawaiiPhysicsToolset.get_preset_node_property(
            preset,
            'PhysicsSettings',
        )
        round_trip_preset = self._export_test_preset(
            handle,
            'KPP_PresetPropertyRoundTripCopy',
        )
        KawaiiPhysicsToolset.set_preset_node_property(
            round_trip_preset,
            'PhysicsSettings',
            value,
        )

        self.assertEqual(
            KawaiiPhysicsToolset.get_preset_node_property(
                round_trip_preset,
                'PhysicsSettings',
            ),
            value,
        )

    def test_set_preset_target_tags_and_description(self):
        handle = self._place_test_node()
        preset = self._export_test_preset(handle, 'KPP_TargetTagsAndDescription')
        description = 'KawaiiPhysics Python toolset description round trip'

        KawaiiPhysicsToolset.set_preset_target_tags(preset, [REAPPLY_TAG], True)
        target_tags = preset.get_editor_property('target_tags')
        exact_match = preset.get_editor_property('target_tags_exact_match')
        KawaiiPhysicsToolset.set_preset_description(preset, description)

        self.assertTrue(exact_match)
        self.assertTrue(_target_tags_contain(target_tags, REAPPLY_TAG))
        self.assertEqual(
            KawaiiPhysicsToolset.get_preset_description(preset),
            description,
        )

    def test_set_get_graph_node_tag_round_trip(self):
        handle = self._place_test_node()

        KawaiiPhysicsToolset.set_graph_node_tag(handle, REAPPLY_TAG)
        tag_name = KawaiiPhysicsToolset.get_graph_node_tag(handle)

        self.assertEqual(tag_name, REAPPLY_TAG)

    def test_set_graph_node_tag_unregistered_raises(self):
        handle = self._place_test_node()

        with self.assertToolRaisesRuntimeError() as cm:
            KawaiiPhysicsToolset.set_graph_node_tag(
                handle,
                'KawaiiPhysics.Test.NotRegisteredForPythonToolset',
            )
        self.assertIn('No valid gameplay tags were resolved', str(cm.exception))

    def test_set_get_graph_node_root_bone_round_trip(self):
        handle = self._place_test_node(request=_make_request('TwintailA_L'))

        KawaiiPhysicsToolset.set_graph_node_root_bone(handle, 'TwintailB_L')
        bone_name = KawaiiPhysicsToolset.get_graph_node_root_bone(handle)

        self.assertEqual(bone_name, 'TwintailB_L')

    def test_find_all_preset_assets_contains_sample_presets(self):
        presets = KawaiiPhysicsToolset.find_all_preset_assets()

        self.assertTrue(any(
            preset.get_name() == 'KPP_Hair_Soft'
            for preset in presets
        ))

    def test_find_anim_blueprint_assets_under_folder(self):
        asset_name = 'ABP_FindAnimBlueprintAssets'
        anim_blueprint = self._create_anim_blueprint(asset_name)
        self._save_asset(anim_blueprint)
        unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(
            [TEST_FOLDER.rstrip('/')],
            True,
        )
        expected_package_path = TEST_FOLDER + asset_name

        asset_paths = KawaiiPhysicsToolset.find_anim_blueprint_assets([TEST_FOLDER])

        self.assertTrue(any(
            path == expected_package_path or
            path.startswith(expected_package_path + '.')
            for path in asset_paths
        ))

    def test_add_nodes_with_auto_connect_compiles(self):
        anim_blueprint = self._create_anim_blueprint()
        request = _make_request('TwintailA_L')
        request.set_editor_property('auto_connect', True)

        handles = KawaiiPhysicsToolset.add_kawaii_physics_nodes(
            anim_blueprint,
            [request],
            unreal.KawaiiPhysicsPlacementUpsertKey.NONE,
            '',
        )
        unreal.BlueprintEditorLibrary.compile_blueprint(anim_blueprint)
        collected = KawaiiPhysicsToolset.collect_kawaii_physics_graph_nodes(
            anim_blueprint,
            [],
            False,
        )

        self.assertEqual(len(handles), 1)
        self.assertEqual(len(collected), 1)

    def test_add_nodes_with_comment(self):
        anim_blueprint = self._create_anim_blueprint('ABP_AddNodesWithComment')
        request = _make_request('TwintailA_L')
        comment = 'Twintail left physics'
        prompt = 'Add KawaiiPhysics to the left twintail chain.'
        comment_prefix = unreal.get_default_object(
            unreal.KawaiiPhysicsDeveloperSettings,
        ).get_editor_property('mcp_comment_prefix')

        handles = KawaiiPhysicsToolset.add_kawaii_physics_nodes(
            anim_blueprint,
            [request],
            unreal.KawaiiPhysicsPlacementUpsertKey.NONE,
            '',
            comment,
            prompt,
        )
        unreal.BlueprintEditorLibrary.compile_blueprint(anim_blueprint)
        comments = KawaiiPhysicsToolset.get_anim_graph_comments(anim_blueprint, '')
        default_graph_comments = KawaiiPhysicsToolset.get_anim_graph_comments(anim_blueprint)

        self.assertEqual(len(handles), 1)
        self.assertEqual(len(comments), 1)
        self.assertEqual(len(default_graph_comments), 1)
        self.assertEqual(comments[0].get_editor_property('title'), comment_prefix + comment)
        self.assertEqual(comments[0].get_editor_property('prompt'), prompt)
        self.assertTrue(comments[0].get_editor_property('mcp_comment'))

        empty_comment_anim_blueprint = self._create_anim_blueprint(
            'ABP_AddNodesWithEmptyComment',
        )
        empty_comment_handles = KawaiiPhysicsToolset.add_kawaii_physics_nodes(
            empty_comment_anim_blueprint,
            [_make_request('TwintailB_L')],
            unreal.KawaiiPhysicsPlacementUpsertKey.NONE,
            '',
            '',
            'This prompt should not create a comment frame.',
        )
        empty_comments = KawaiiPhysicsToolset.get_anim_graph_comments(
            empty_comment_anim_blueprint,
            '',
        )

        self.assertEqual(len(empty_comment_handles), 1)
        self.assertEqual(len(empty_comments), 0)

    def test_apply_preset_success(self):
        handle = self._place_test_node()
        preset = _load_asset_checked(HAIR_PRESET_PATH)
        self.assertIsInstance(preset, unreal.KawaiiPhysicsPresetDataAsset)

        KawaiiPhysicsToolset.apply_preset_to_graph_node(handle, preset, True, True)
        diff_properties = KawaiiPhysicsToolset.does_graph_node_match_preset(
            handle,
            preset,
            True,
            True,
        )
        self.assertEqual(diff_properties, [])

    def test_apply_preset_none_raises(self):
        handle = self._place_test_node()
        with self.assertToolRaisesRuntimeError():
            KawaiiPhysicsToolset.apply_preset_to_graph_node(handle, None, True, True)

    def test_get_preset_diff_reports_values_after_change(self):
        handle = self._place_test_node()
        preset = self._export_test_preset(handle, 'KPP_GetPresetDiff')

        matches = KawaiiPhysicsToolset.get_preset_diff(handle, preset, True, True)
        self.assertEqual(matches, [])

        changed_physics_settings = (
            '(Damping=0.91,Stiffness=0.44,WorldDampingLocation=0.55,'
            'WorldDampingRotation=0.66,Radius=7.0,LimitAngle=45.0)'
        )
        KawaiiPhysicsToolset.set_graph_node_property(handle, 'PhysicsSettings', changed_physics_settings)

        diffs = KawaiiPhysicsToolset.get_preset_diff(handle, preset, True, True)

        physics_settings_diffs = [
            entry for entry in diffs
            if str(entry.get_editor_property('property_name')) == 'PhysicsSettings'
        ]
        self.assertEqual(len(physics_settings_diffs), 1)
        node_value = str(physics_settings_diffs[0].get_editor_property('node_value'))
        preset_value = str(physics_settings_diffs[0].get_editor_property('preset_value'))
        self.assertTrue(node_value)
        self.assertTrue(preset_value)
        self.assertNotEqual(node_value, preset_value)

    def test_get_preset_diff_invalid_handle_raises(self):
        invalid_handle = unreal.KawaiiPhysicsGraphNodeHandle()
        preset = _load_asset_checked(HAIR_PRESET_PATH)

        with self.assertToolRaisesRuntimeError():
            KawaiiPhysicsToolset.get_preset_diff(invalid_handle, preset, True, True)

    def test_export_graph_node_to_preset_creates_new_asset(self):
        handle = self._place_test_node()
        preset = self._export_test_preset(handle, 'KPP_ExportedNew')

        diff_properties = KawaiiPhysicsToolset.does_graph_node_match_preset(
            handle,
            preset,
            True,
            True,
        )
        self.assertEqual(diff_properties, [])

    def test_export_graph_node_to_preset_overwrites_existing(self):
        anim_blueprint = self._create_anim_blueprint()
        first_handle = self._place_test_node(
            anim_blueprint,
            _make_request('TwintailA_L'),
        )
        preset = self._export_test_preset(first_handle, 'KPP_Overwrite')

        second_handle = self._place_test_node(
            anim_blueprint,
            _make_request('TwintailB_L'),
        )
        KawaiiPhysicsToolset.export_graph_node_to_preset(
            second_handle,
            TEST_FOLDER + 'KPP_Overwrite',
        )

        self.assertTrue(unreal.EditorAssetLibrary.does_asset_exist(TEST_FOLDER + 'KPP_Overwrite'))
        self.assertEqual(
            KawaiiPhysicsToolset.does_graph_node_match_preset(second_handle, preset, True, True),
            [],
        )
        self.assertIn(
            'RootBone',
            KawaiiPhysicsToolset.does_graph_node_match_preset(first_handle, preset, True, True),
        )

    def test_export_graph_node_invalid_handle_raises(self):
        invalid_handle = unreal.KawaiiPhysicsGraphNodeHandle()

        with self.assertToolRaisesRuntimeError() as cm:
            KawaiiPhysicsToolset.export_graph_node_to_preset(
                invalid_handle,
                TEST_FOLDER + 'KPP_InvalidHandle',
            )
        self.assertIn('handle is not a valid KawaiiPhysics graph node handle', str(cm.exception))

    def test_export_preset_takes_node_tag_into_target_tags(self):
        handle = self._place_test_node(
            request=_make_request('TwintailA_L', REAPPLY_TAG),
        )
        preset = self._export_test_preset(handle, 'KPP_TaggedExport')

        target_tags = preset.get_editor_property('target_tags')
        self.assertTrue(_target_tags_contain(target_tags, REAPPLY_TAG))

    def test_reapply_preset_dry_run_reports_without_applying(self):
        anim_blueprint = self._create_anim_blueprint()
        handle = self._place_test_node(
            anim_blueprint,
            _make_request('TwintailA_L', REAPPLY_TAG),
        )
        preset = self._export_test_preset(handle, 'KPP_ReapplyDryRun')
        self._set_graph_node_property(handle, 'WindScale', '3.25')
        self._save_asset(anim_blueprint)

        diff_before = KawaiiPhysicsToolset.does_graph_node_match_preset(
            handle,
            preset,
            False,
            False,
        )
        report = KawaiiPhysicsToolset.reapply_preset_to_project(preset, True, False)
        diff_after = KawaiiPhysicsToolset.does_graph_node_match_preset(
            handle,
            preset,
            False,
            False,
        )

        self.assertEqual(len(report), 1)
        self.assertIn('WindScale', self._entry_diff_names(report[0]))
        self.assertNotEqual(diff_before, [])
        self.assertEqual(diff_after, diff_before)

    def test_reapply_preset_applies_to_matching_nodes(self):
        anim_blueprint = self._create_anim_blueprint()
        handle = self._place_test_node(
            anim_blueprint,
            _make_request('TwintailA_L', REAPPLY_TAG),
        )
        preset = self._export_test_preset(handle, 'KPP_ReapplyApply')
        self._set_graph_node_property(handle, 'WindScale', '3.25')
        self._save_asset(anim_blueprint)

        report = KawaiiPhysicsToolset.reapply_preset_to_project(preset, False, False)
        diff_after = KawaiiPhysicsToolset.does_graph_node_match_preset(
            handle,
            preset,
            False,
            False,
        )

        self.assertEqual(len(report), 1)
        self.assertIn('WindScale', self._entry_diff_names(report[0]))
        self.assertEqual(diff_after, [])

    def test_reapply_preset_empty_target_tags_returns_empty_report(self):
        handle = self._place_test_node()
        preset = self._export_test_preset(handle, 'KPP_EmptyTargetTags')

        report = KawaiiPhysicsToolset.reapply_preset_to_project(preset, True, False)

        self.assertEqual(report, [])

    def test_audit_nodes_reports_placed_node(self):
        anim_blueprint = self._create_anim_blueprint()
        self._place_test_node(
            anim_blueprint,
            _make_request('TwintailA_L', REAPPLY_TAG),
        )
        self._save_asset(anim_blueprint)

        entries = self._audit_entries()

        self.assertTrue(any(
            self._entry_root_bone_name(entry) == 'TwintailA_L' and
            self._entry_tag_name(entry) == REAPPLY_TAG
            for entry in entries
        ))

    def test_resolve_bones_by_pattern_returns_matches(self):
        bone_names = KawaiiPhysicsToolset.resolve_bones_by_pattern(
            self.skeleton,
            'TwintailA_L',
        )

        self.assertEqual(bone_names, ['TwintailA_L'])

    def test_resolve_bones_by_pattern_empty_pattern_returns_empty(self):
        bone_names = KawaiiPhysicsToolset.resolve_bones_by_pattern(
            self.skeleton,
            '',
        )

        self.assertEqual(bone_names, [])

    def test_resolve_bones_greedy_pattern_returns_safely(self):
        all_bone_names = set(KawaiiPhysicsToolset.resolve_bones_by_pattern(
            self.skeleton,
            '[^, ]+',
        ))

        bone_names = KawaiiPhysicsToolset.resolve_bones_by_pattern(
            self.skeleton,
            '.*',
        )

        bone_names = list(bone_names)
        self.assertTrue(set(bone_names).issubset(all_bone_names))

    def test_upsert_by_tag_updates_existing_node(self):
        anim_blueprint = self._create_anim_blueprint()
        first_request = _make_request('TwintailA_L', REAPPLY_TAG)
        second_request = _make_request('TwintailB_L', REAPPLY_TAG)

        self._place_test_node(
            anim_blueprint,
            first_request,
            unreal.KawaiiPhysicsPlacementUpsertKey.TAG,
        )
        second_handle = self._place_test_node(
            anim_blueprint,
            second_request,
            unreal.KawaiiPhysicsPlacementUpsertKey.TAG,
        )
        collected = KawaiiPhysicsToolset.collect_kawaii_physics_graph_nodes(
            anim_blueprint,
            [],
            False,
        )
        root_bone_name = unreal.KawaiiPhysicsEditorLibrary.get_graph_node_root_bone_name(
            second_handle,
        )

        self.assertEqual(len(collected), 1)
        self.assertIsNotNone(root_bone_name)
        self.assertEqual(str(root_bone_name), 'TwintailB_L')

    def test_upsert_by_root_bone_updates_existing_node(self):
        anim_blueprint = self._create_anim_blueprint()
        first_request = _make_request('TwintailA_L', HAIR_TAG)
        second_request = _make_request('TwintailA_L', REAPPLY_TAG)

        self._place_test_node(
            anim_blueprint,
            first_request,
            unreal.KawaiiPhysicsPlacementUpsertKey.ROOT_BONE,
        )
        second_handle = self._place_test_node(
            anim_blueprint,
            second_request,
            unreal.KawaiiPhysicsPlacementUpsertKey.ROOT_BONE,
        )
        collected = KawaiiPhysicsToolset.collect_kawaii_physics_graph_nodes(
            anim_blueprint,
            [],
            False,
        )
        node_tag = unreal.KawaiiPhysicsEditorLibrary.get_graph_node_tag(second_handle)

        self.assertEqual(len(collected), 1)
        self.assertIsNotNone(node_tag)
        self.assertEqual(_tag_to_string(node_tag), REAPPLY_TAG)

    def test_apply_preset_keeps_bone_assignment_and_tag_when_disabled(self):
        anim_blueprint = self._create_anim_blueprint()
        source_handle = self._place_test_node(
            anim_blueprint,
            _make_request('TwintailA_L', HAIR_TAG),
        )
        target_handle = self._place_test_node(
            anim_blueprint,
            _make_request('TwintailB_L', REAPPLY_TAG),
        )
        preset = self._export_test_preset(source_handle, 'KPP_ApplyKeepsBoneAndTag')

        KawaiiPhysicsToolset.apply_preset_to_graph_node(target_handle, preset, False, False)
        self._save_asset(anim_blueprint)
        entries = self._audit_entries([REAPPLY_TAG], True)

        self.assertEqual(len(entries), 1)
        self.assertEqual(self._entry_root_bone_name(entries[0]), 'TwintailB_L')
        self.assertEqual(self._entry_tag_name(entries[0]), REAPPLY_TAG)

    def test_validate_skeleton_mismatch_warning_is_nonblocking(self):
        mismatch_skeleton = unreal.EditorAssetLibrary.load_asset(CHAIN_SKELETON_PATH)
        if mismatch_skeleton is None:
            self.skipTest(f'Mismatch skeleton fixture is unavailable: {CHAIN_SKELETON_PATH}')
        self.assertIsInstance(mismatch_skeleton, unreal.Skeleton)

        anim_blueprint = self._create_anim_blueprint()
        source_handle = self._place_test_node(
            anim_blueprint,
            _make_request('TwintailA_L'),
        )
        preset = self._export_test_preset(source_handle, 'KPP_SkeletonMismatch')
        preset.set_editor_property('skeleton', mismatch_skeleton)
        request = _make_request('TwintailB_L', preset=preset)

        errors = KawaiiPhysicsToolset.validate_placement_requests(anim_blueprint, [request])
        handles = KawaiiPhysicsToolset.add_kawaii_physics_nodes(
            anim_blueprint,
            [request],
            unreal.KawaiiPhysicsPlacementUpsertKey.NONE,
            '',
        )

        self.assertTrue(any(error.startswith('Warning:') for error in errors))
        self.assertTrue(any('Skeleton does not match' in error for error in errors))
        self.assertEqual(len(handles), 1)


if __name__ == '__main__':
    unittest.main()
