from __future__ import annotations

import ast
import inspect
import json
import unittest

import unreal

from kawaii_physics_toolset import toolset as toolset_module
from kawaii_physics_toolset.toolset import KawaiiPhysicsToolset
from toolset_registry.tests.toolset_testcase import ToolCallTestCase


UNKNOWN_ACTOR_LABEL = '__KP_NO_SUCH_ACTOR__'
TEST_FOLDER = '/Game/Test/PyToolset/'
GRAYCHAN_SKELETON_PATH = '/Game/KawaiiPhysicsSample/Model/GrayChan/Mesh/GrayChan_Skeleton'
HAIR_PRESET_PATH = '/Game/KawaiiPhysicsSample/Presets/KPP_Hair_Soft'
CHAIN_SKELETON_PATH = '/Game/KawaiiPhysicsSample/Model/Chain/S_Chain_Skeleton'
GRAYCHAN_RUN_ANIMATION_PATH = '/Game/KawaiiPhysicsSample/Model/GrayChan/Animations/ThirdPersonRun'
GRAYCHAN_IDLE_ANIMATION_PATH = '/Game/KawaiiPhysicsSample/Model/GrayChan/Animations/ThirdPersonIdle'
SKIRT_ANIMATION_PATH = '/Game/KawaiiPhysicsSample/Model/Skirt/Animations/A_CheckSkirt'
GRAYCHAN_MESH_PATH = '/Game/KawaiiPhysicsSample/Model/GrayChan/Mesh/GrayChan'
BONE_SAMPLER_ACTOR_LABEL = 'KP_PyToolsetBoneSamplerSpaceTest'
BASIC_EXTERNAL_FORCE_STRUCT = '/Script/KawaiiPhysics.KawaiiPhysics_ExternalForce_Basic'
PHYSICS_SETTINGS_VALUE = (
    '(Damping=0.3,Stiffness=0.1,WorldDampingLocation=0.55,'
    'WorldDampingRotation=0.66,Radius=7.0,LimitAngle=45.0)'
)
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
            match_key: unreal.KawaiiPhysicsPlacementMatchKey = unreal.KawaiiPhysicsPlacementMatchKey.NONE
    ) -> unreal.KawaiiPhysicsGraphNodeHandle:
        if anim_blueprint is None:
            anim_blueprint = self._create_anim_blueprint()
        if request is None:
            request = _make_request()
        handles = KawaiiPhysicsToolset.add_kawaii_physics_nodes(
            anim_blueprint,
            [request],
            match_key,
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
        self.assertTrue(unreal.KawaiiPhysicsEditorLibrary.set_graph_node_property_from_string(
            handle,
            unreal.Name(property_name),
            value,
        ))

    def _require_world(self) -> None:
        # ヘッドレス実行ではワールドを取得できないことがあり、その場合ランタイム系ツールは検証できない
        editor_subsystem = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
        world = editor_subsystem.get_game_world() if editor_subsystem else None
        if world is None and editor_subsystem is not None:
            world = editor_subsystem.get_editor_world()
        if world is None:
            self.skipTest('No world is available in this environment.')

    def _load_animation(self, asset_path: str = GRAYCHAN_RUN_ANIMATION_PATH) -> unreal.AnimSequenceBase:
        animation = _load_asset_checked(asset_path)
        self.assertIsInstance(animation, unreal.AnimSequenceBase)
        return animation

    def _assert_compiles_without_errors(self, anim_blueprint: unreal.AnimBlueprint) -> list[str]:
        messages = list(KawaiiPhysicsToolset.compile_anim_blueprint(anim_blueprint))
        errors = [message for message in messages if message.startswith('Error:')]
        self.assertEqual(errors, [])
        return messages

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

    def test_argument_guards_raise(self):
        anim_blueprint = self._create_anim_blueprint()
        handle = self._place_test_node(anim_blueprint)
        invalid_handle = unreal.KawaiiPhysicsGraphNodeHandle()
        invalid_publisher = unreal.KawaiiPhysicsSharedPublisherGraphNodeHandle()
        preset = _load_asset_checked(HAIR_PRESET_PATH)
        cases = [
            ('create skeleton', lambda: KawaiiPhysicsToolset.create_anim_blueprint(
                TEST_FOLDER, 'ABP_Invalid', None, None), None),
            ('set nodes property name', lambda: KawaiiPhysicsToolset.set_graph_nodes_property(
                anim_blueprint, '', 'true', [], False), None),
            ('get nodes property name', lambda: KawaiiPhysicsToolset.get_graph_nodes_property(
                anim_blueprint, '', [], False), None),
            ('apply preset', lambda: KawaiiPhysicsToolset.apply_preset_to_graph_node(
                handle, None, True, True), None),
            ('preset diff handle', lambda: KawaiiPhysicsToolset.get_preset_diff(
                invalid_handle, preset, True, True), None),
            ('export handle', lambda: KawaiiPhysicsToolset.export_graph_node_to_preset(
                invalid_handle, TEST_FOLDER + 'KPP_InvalidHandle'), None),
            ('set properties handle', lambda: KawaiiPhysicsToolset.set_graph_node_properties(
                invalid_handle, '{"WindScale": 1.5}'), None),
            ('set publisher handle', lambda: KawaiiPhysicsToolset.set_shared_publisher_node_property(
                invalid_publisher, 'bEnabled', 'False'), None),
            ('get publisher handle', lambda: KawaiiPhysicsToolset.get_shared_publisher_node_property(
                invalid_publisher, 'bEnabled'), None),
            ('input animation', lambda: KawaiiPhysicsToolset.set_anim_graph_input_animation(
                anim_blueprint, None, None), None),
            ('layout null', lambda: KawaiiPhysicsToolset.layout_anim_graph(None, ''), None),
            ('layout missing graph', lambda: KawaiiPhysicsToolset.layout_anim_graph(
                anim_blueprint, 'NoSuchGraph'), 'Unable to lay out the AnimGraph'),
            ('compile null', lambda: KawaiiPhysicsToolset.compile_anim_blueprint(None), None),
            ('collision debug null', lambda: KawaiiPhysicsToolset.get_simple_world_collision_debug_info(
                None), None),
            ('collision debug label', lambda: KawaiiPhysicsToolset.find_simple_world_collision_debug_info(
                '', True), None),
            ('console command', lambda: KawaiiPhysicsToolset.execute_console_command(
                '', True), None),
            ('sampler unknown actor', lambda: KawaiiPhysicsToolset.start_bone_sampler(
                UNKNOWN_ACTOR_LABEL, '.*', 120, True, 'world'), None),
            ('sampler empty pattern', lambda: KawaiiPhysicsToolset.start_bone_sampler(
                UNKNOWN_ACTOR_LABEL, '', 120, True, 'world'), None),
            ('multiplier invalid json', lambda: KawaiiPhysicsToolset.start_physics_settings_multiplier_on_actor(
                UNKNOWN_ACTOR_LABEL, 'not json', 1.0, 0.2, 0.5, [], False, True),
                'settings_json is not valid JSON'),
            ('multiplier unknown field', lambda: KawaiiPhysicsToolset.start_physics_settings_multiplier_on_actor(
                UNKNOWN_ACTOR_LABEL, '{"NotAField": 2.0}', 1.0, 0.2, 0.5, [], False, True),
                'Unknown FKawaiiPhysicsSettingsMultiplier field'),
            ('multiplier invalid handle', lambda: KawaiiPhysicsToolset.stop_physics_settings_multiplier_on_actor(
                UNKNOWN_ACTOR_LABEL, '', 0.5, [], False, True), None),
            ('wind direction length', lambda: KawaiiPhysicsToolset.start_procedural_wind_gust_on_actor(
                UNKNOWN_ACTOR_LABEL, 1.0, 1.0, 0.1, 0.3, [1.0, 0.0], [], False, True),
                'direction must have 3 elements'),
            ('alpha actor label', lambda: KawaiiPhysicsToolset.set_alpha_on_actor(
                '', 1.0, [], False, True), None),
            ('external forces json', lambda: KawaiiPhysicsToolset.set_graph_node_external_forces(
                handle, ''), None),
            ('sampler space', lambda: KawaiiPhysicsToolset.start_bone_sampler(
                UNKNOWN_ACTOR_LABEL, '.*', 1, True, 'local'), 'Unknown space'),
        ]
        for name, call, message in cases:
            with self.subTest(case=name):
                if name == 'sampler unknown actor':
                    self._require_world()
                with self.assertToolRaisesRuntimeError() as cm:
                    call()
                if message is not None:
                    self.assertIn(message, str(cm.exception))

    def test_collect_with_invalid_filter_tags_raises(self):
        anim_blueprint = self._create_anim_blueprint()

        with self.assertToolRaisesRuntimeError() as cm:
            KawaiiPhysicsToolset.collect_kawaii_physics_graph_nodes(
                anim_blueprint,
                ['KawaiiPhysics.NotRegistered', 'KawaiiPhysics.AlsoNotRegistered'],
                False,
            )
        self.assertIn('No valid gameplay tags were resolved', str(cm.exception))

    def test_set_graph_nodes_property_updates_all_nodes(self):
        anim_blueprint = self._create_anim_blueprint()
        first_handle = self._place_test_node(
            anim_blueprint,
            _make_request('TwintailA_L'),
        )
        second_handle = self._place_test_node(
            anim_blueprint,
            _make_request('TwintailB_L'),
        )

        updated_count = KawaiiPhysicsToolset.set_graph_nodes_property(
            anim_blueprint,
            'bUseSimpleWorldCollision',
            'true',
            [],
            False,
        )

        self.assertEqual(updated_count, 2)
        self.assertEqual(
            KawaiiPhysicsToolset.get_graph_node_property(
                first_handle,
                'bUseSimpleWorldCollision',
            ),
            'True',
        )
        self.assertEqual(
            KawaiiPhysicsToolset.get_graph_node_property(
                second_handle,
                'bUseSimpleWorldCollision',
            ),
            'True',
        )

        values = KawaiiPhysicsToolset.get_graph_nodes_property(
            anim_blueprint, 'bUseSimpleWorldCollision', [], False)
        self.assertEqual(len(values), 2)
        for value in values.values():
            self.assertEqual(str(value), 'True')
        self.assertEqual(KawaiiPhysicsToolset.set_graph_nodes_property(
            anim_blueprint, 'bUseSimpleWorldCollision', 'false', [HAIR_TAG], True), 0)
        self.assertEqual(KawaiiPhysicsToolset.get_graph_node_property(
            first_handle, 'bUseSimpleWorldCollision'), 'True')

    def test_describe_graph_nodes_returns_json_array(self):
        anim_blueprint = self._create_anim_blueprint()
        first = self._place_test_node(anim_blueprint, _make_request('TwintailA_L'))
        self._place_test_node(anim_blueprint, _make_request('TwintailB_L'))

        descriptions = json.loads(
            KawaiiPhysicsToolset.describe_graph_nodes(anim_blueprint))

        self.assertEqual(len(descriptions), 2)
        root_bone_names = sorted(
            description['root_bone'] for description in descriptions)
        self.assertEqual(root_bone_names, ['TwintailA_L', 'TwintailB_L'])
        self.assertEqual(
            descriptions[0]['bUseSimpleWorldCollision'],
            'False',
        )
        self.assertIn('bAllowWorldCollision', descriptions[0])
        self.assertIn('SimpleWorldCollisionSkeletalMeshCollision', descriptions[0])

        KawaiiPhysicsToolset.set_graph_node_properties(
            first, json.dumps({'PhysicsSettings': PHYSICS_SETTINGS_VALUE}))
        settings = json.loads(KawaiiPhysicsToolset.describe_graph_node_settings(
            anim_blueprint, [], False))
        self.assertEqual(len(settings), 2)
        by_bone = {entry['root_bone']: entry for entry in settings}
        self.assertIn('Damping=0.3', by_bone['TwintailA_L']['settings']['PhysicsSettings'])
        self.assertIsInstance(by_bone['TwintailA_L']['external_forces'], list)
        self.assertNotIn('ExternalForces', by_bone['TwintailA_L']['settings'])
        self.assertEqual(set(by_bone), {'TwintailA_L', 'TwintailB_L'})

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
        self.assertAlmostEqual(float(value.split('Damping=', 1)[1].split(',', 1)[0]), 0.33)
        description = 'KawaiiPhysics Python toolset description round trip'
        KawaiiPhysicsToolset.set_preset_target_tags(preset, [REAPPLY_TAG], True)
        target_tags = preset.get_editor_property('target_tags')
        exact_match = preset.get_editor_property('target_tags_exact_match')
        KawaiiPhysicsToolset.set_preset_description(preset, description)
        self.assertTrue(exact_match)
        self.assertTrue(_target_tags_contain(target_tags, REAPPLY_TAG))
        self.assertEqual(KawaiiPhysicsToolset.get_preset_description(preset), description)

    def test_set_get_graph_node_tag_round_trip(self):
        handle = self._place_test_node(request=_make_request('TwintailA_L'))

        KawaiiPhysicsToolset.set_graph_node_tag(handle, REAPPLY_TAG)
        tag_name = KawaiiPhysicsToolset.get_graph_node_tag(handle)

        self.assertEqual(tag_name, REAPPLY_TAG)

        KawaiiPhysicsToolset.set_graph_node_root_bone(handle, 'TwintailB_L')
        bone_name = KawaiiPhysicsToolset.get_graph_node_root_bone(handle)
        self.assertEqual(bone_name, 'TwintailB_L')

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
            unreal.KawaiiPhysicsPlacementMatchKey.NONE,
            '',
            comment,
            prompt,
        )
        unreal.BlueprintEditorLibrary.compile_blueprint(anim_blueprint)
        comments = KawaiiPhysicsToolset.get_anim_graph_comments(anim_blueprint, '')

        self.assertEqual(len(handles), 1)
        self.assertEqual(len(comments), 1)
        self.assertEqual(comments[0].get_editor_property('title'), comment_prefix + comment)
        self.assertEqual(comments[0].get_editor_property('prompt'), prompt)
        self.assertTrue(comments[0].get_editor_property('mcp_comment'))


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

    def test_apply_preset_dry_run_reports_without_applying(self):
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
        report = KawaiiPhysicsToolset.apply_preset_to_project(preset, True, False)
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

        apply_report = KawaiiPhysicsToolset.apply_preset_to_project(preset, False, False)
        applied_diff = KawaiiPhysicsToolset.does_graph_node_match_preset(
            handle, preset, False, False)
        self.assertEqual(len(apply_report), 1)
        self.assertIn('WindScale', self._entry_diff_names(apply_report[0]))
        self.assertEqual(applied_diff, [])

    def test_audit_nodes_reports_placed_node(self):
        anim_blueprint = self._create_anim_blueprint()
        source_handle = self._place_test_node(
            anim_blueprint,
            _make_request('TwintailA_L', HAIR_TAG),
        )
        target_handle = self._place_test_node(
            anim_blueprint, _make_request('TwintailB_L', REAPPLY_TAG))
        preset = self._export_test_preset(source_handle, 'KPP_ApplyKeepsBoneAndTag')
        KawaiiPhysicsToolset.apply_preset_to_graph_node(target_handle, preset, False, False)
        self._save_asset(anim_blueprint)

        entries = self._audit_entries()
        self.assertEqual(len(entries), 2)
        self.assertEqual(
            {(self._entry_root_bone_name(entry), self._entry_tag_name(entry)) for entry in entries},
            {('TwintailA_L', HAIR_TAG), ('TwintailB_L', REAPPLY_TAG)},
        )
        filtered = self._audit_entries([REAPPLY_TAG], True)
        self.assertEqual(len(filtered), 1)
        self.assertEqual(self._entry_root_bone_name(filtered[0]), 'TwintailB_L')
        self.assertEqual(self._entry_tag_name(filtered[0]), REAPPLY_TAG)

    def test_find_bones_by_pattern_returns_matches(self):
        bone_names = KawaiiPhysicsToolset.find_bones_by_pattern(
            self.skeleton,
            'TwintailA_L',
        )

        self.assertEqual(bone_names, ['TwintailA_L'])
        empty_matches = KawaiiPhysicsToolset.find_bones_by_pattern(self.skeleton, '')
        self.assertEqual(empty_matches, [])

    def test_match_by_root_bone_updates_existing_node(self):
        anim_blueprint = self._create_anim_blueprint()
        first_request = _make_request('TwintailA_L', HAIR_TAG)
        second_request = _make_request('TwintailA_L', REAPPLY_TAG)

        self._place_test_node(
            anim_blueprint,
            first_request,
            unreal.KawaiiPhysicsPlacementMatchKey.ROOT_BONE,
        )
        second_handle = self._place_test_node(
            anim_blueprint,
            second_request,
            unreal.KawaiiPhysicsPlacementMatchKey.ROOT_BONE,
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
            unreal.KawaiiPhysicsPlacementMatchKey.NONE,
            '',
        )

        self.assertTrue(any(error.startswith('Warning:') for error in errors))
        self.assertTrue(any('Skeleton does not match' in error for error in errors))
        self.assertEqual(len(handles), 1)

    def test_set_graph_node_properties_sets_values_in_order(self):
        handle = self._place_test_node()

        names = KawaiiPhysicsToolset.set_graph_node_properties(
            handle,
            json.dumps({
                'WindScale': 2.5,
                'bUseSimpleWorldCollision': True,
                'PhysicsSettings': PHYSICS_SETTINGS_VALUE,
            }),
        )

        self.assertEqual(
            list(names),
            ['WindScale', 'bUseSimpleWorldCollision', 'PhysicsSettings'],
        )
        self.assertAlmostEqual(
            float(KawaiiPhysicsToolset.get_graph_node_property(handle, 'WindScale')),
            2.5,
        )
        self.assertEqual(
            KawaiiPhysicsToolset.get_graph_node_property(handle, 'bUseSimpleWorldCollision'),
            'True',
        )
        self.assertIn(
            'Damping=0.3',
            KawaiiPhysicsToolset.get_graph_node_property(handle, 'PhysicsSettings'),
        )

    def test_set_graph_node_properties_invalid_json_raises(self):
        handle = self._place_test_node()

        with self.assertToolRaisesRuntimeError() as cm:
            KawaiiPhysicsToolset.set_graph_node_properties(handle, 'not json')
        self.assertIn('properties_json is not valid JSON', str(cm.exception))

        with self.assertToolRaisesRuntimeError() as cm:
            KawaiiPhysicsToolset.set_graph_node_properties(handle, '[1, 2]')
        self.assertIn('properties_json must be a JSON object', str(cm.exception))

        with self.assertToolRaisesRuntimeError() as cm:
            KawaiiPhysicsToolset.set_graph_node_properties(handle, '')
        self.assertIn('properties_json must not be empty', str(cm.exception))

    def test_set_graph_node_properties_unknown_property_raises(self):
        handle = self._place_test_node()

        with self.assertToolRaisesRuntimeError() as cm:
            KawaiiPhysicsToolset.set_graph_node_properties(
                handle,
                '{"WindScale": 1.5, "NoSuchProperty_XYZ": 1}',
            )

        self.assertIn('NoSuchProperty_XYZ', str(cm.exception))
        # 失敗より前のプロパティは設定済みのまま残る
        self.assertAlmostEqual(
            float(KawaiiPhysicsToolset.get_graph_node_property(handle, 'WindScale')),
            1.5,
        )

    def test_add_kawaii_physics_node_with_tag_and_properties(self):
        anim_blueprint = self._create_anim_blueprint()

        handle = KawaiiPhysicsToolset.add_kawaii_physics_node(
            anim_blueprint,
            'TwintailA_L',
            ['TwintailD_L'],
            ['TwintailA_R'],
            REAPPLY_TAG,
            '{"WindScale": 2.0, "bUseSimpleWorldCollision": true}',
            '',
            '',
            '',
        )

        self.assertTrue(KawaiiPhysicsToolset.is_graph_node_handle_valid(handle))
        self.assertEqual(KawaiiPhysicsToolset.get_graph_node_root_bone(handle), 'TwintailA_L')
        self.assertEqual(KawaiiPhysicsToolset.get_graph_node_tag(handle), REAPPLY_TAG)
        self.assertAlmostEqual(
            float(KawaiiPhysicsToolset.get_graph_node_property(handle, 'WindScale')),
            2.0,
        )
        self.assertEqual(
            KawaiiPhysicsToolset.get_graph_node_property(handle, 'bUseSimpleWorldCollision'),
            'True',
        )
        self.assertIn('TwintailD_L', KawaiiPhysicsToolset.get_graph_node_property(handle, 'ExcludeBones'))
        self.assertIn('TwintailA_R', KawaiiPhysicsToolset.get_graph_node_property(handle, 'AdditionalRootBones'))
        self.assertEqual(
            len(KawaiiPhysicsToolset.collect_kawaii_physics_graph_nodes(anim_blueprint, [], False)),
            1,
        )
        self._assert_compiles_without_errors(anim_blueprint)

        second_handle = KawaiiPhysicsToolset.add_kawaii_physics_node(
            anim_blueprint, 'TwintailA_L', ['TwintailD_L'], ['TwintailA_R'],
            REAPPLY_TAG, '{"WindScale": 2.0, "bUseSimpleWorldCollision": true}',
            '', '', '')
        self.assertTrue(KawaiiPhysicsToolset.is_graph_node_handle_valid(second_handle))
        self.assertEqual(len(KawaiiPhysicsToolset.collect_kawaii_physics_graph_nodes(
            anim_blueprint, [], False)), 2)

    def test_add_kawaii_physics_node_unregistered_tag_raises(self):
        anim_blueprint = self._create_anim_blueprint()

        with self.assertToolRaisesRuntimeError() as cm:
            KawaiiPhysicsToolset.add_kawaii_physics_node(
                anim_blueprint,
                'TwintailA_L',
                [],
                [],
                'KawaiiPhysics.Test.NotRegisteredForPythonToolset',
            )

        self.assertIn('No valid gameplay tags were resolved', str(cm.exception))
        self.assertEqual(
            len(KawaiiPhysicsToolset.collect_kawaii_physics_graph_nodes(anim_blueprint, [], False)),
            0,
        )

    def test_add_kawaii_physics_node_unknown_root_bone_raises(self):
        anim_blueprint = self._create_anim_blueprint()

        with self.assertToolRaisesRuntimeError() as cm:
            KawaiiPhysicsToolset.add_kawaii_physics_node(anim_blueprint, 'TotallyBogusBone_XYZ')

        self.assertIn('TotallyBogusBone_XYZ', str(cm.exception))

    def test_add_kawaii_physics_node_invalid_properties_json_adds_nothing(self):
        anim_blueprint = self._create_anim_blueprint()

        with self.assertToolRaisesRuntimeError() as cm:
            KawaiiPhysicsToolset.add_kawaii_physics_node(
                anim_blueprint,
                'TwintailA_L',
                [],
                [],
                '',
                'not json',
            )

        self.assertIn('properties_json is not valid JSON', str(cm.exception))
        self.assertEqual(
            len(KawaiiPhysicsToolset.collect_kawaii_physics_graph_nodes(anim_blueprint, [], False)),
            0,
        )

    def test_add_shared_publisher_node_with_default_tag_and_properties(self):
        anim_blueprint = self._create_anim_blueprint()
        animation = self._load_animation()
        KawaiiPhysicsToolset.set_anim_graph_input_animation(anim_blueprint, animation)

        handle = KawaiiPhysicsToolset.add_shared_publisher_node(
            anim_blueprint,
            'KawaiiPhysics.Shared.Default',
            True,
            json.dumps({
                'SharedWind': '(ParameterMode=Advanced,ConstantForce=8.0,SwayForce=12.0)',
                'bEnabled': True,
            }),
        )

        self.assertTrue(unreal.KawaiiPhysicsEditorLibrary.is_shared_publisher_graph_node_handle_valid(handle))
        self.assertIn(
            'KawaiiPhysics.Shared.Default',
            KawaiiPhysicsToolset.get_shared_publisher_node_property(handle, 'SharedGroupTag'),
        )
        shared_wind = KawaiiPhysicsToolset.get_shared_publisher_node_property(handle, 'SharedWind')
        self.assertIn('ParameterMode=Advanced', shared_wind)
        self.assertIn('ConstantForce=8', shared_wind)
        self.assertIn('SwayForce=12', shared_wind)
        self.assertEqual(
            KawaiiPhysicsToolset.get_shared_publisher_node_property(handle, 'bEnabled'),
            'True',
        )
        self.assertEqual(len(KawaiiPhysicsToolset.collect_shared_publisher_nodes(anim_blueprint)), 1)
        self._assert_compiles_without_errors(anim_blueprint)

    def test_add_shared_publisher_node_reuse_existing(self):
        anim_blueprint = self._create_anim_blueprint()

        KawaiiPhysicsToolset.add_shared_publisher_node(anim_blueprint, REAPPLY_TAG)
        KawaiiPhysicsToolset.add_shared_publisher_node(anim_blueprint, REAPPLY_TAG, True)
        self.assertEqual(len(KawaiiPhysicsToolset.collect_shared_publisher_nodes(anim_blueprint)), 1)

        KawaiiPhysicsToolset.add_shared_publisher_node(anim_blueprint, REAPPLY_TAG, False)
        self.assertEqual(len(KawaiiPhysicsToolset.collect_shared_publisher_nodes(anim_blueprint)), 2)

    def test_add_shared_publisher_node_invalid_properties_json_adds_nothing(self):
        anim_blueprint = self._create_anim_blueprint()

        with self.assertToolRaisesRuntimeError() as cm:
            KawaiiPhysicsToolset.add_shared_publisher_node(
                anim_blueprint,
                'KawaiiPhysics.Shared.Default',
                True,
                'not json',
            )

        self.assertIn('properties_json is not valid JSON', str(cm.exception))
        self.assertEqual(len(KawaiiPhysicsToolset.collect_shared_publisher_nodes(anim_blueprint)), 0)

    def test_set_shared_publisher_node_property_unknown_property_raises(self):
        anim_blueprint = self._create_anim_blueprint()
        handle = KawaiiPhysicsToolset.add_shared_publisher_node(anim_blueprint)

        with self.assertToolRaisesRuntimeError() as cm:
            KawaiiPhysicsToolset.set_shared_publisher_node_property(handle, 'NoSuchProperty_XYZ', '1')
        self.assertIn('NoSuchProperty_XYZ', str(cm.exception))

        with self.assertToolRaisesRuntimeError() as cm:
            KawaiiPhysicsToolset.get_shared_publisher_node_property(handle, 'NoSuchProperty_XYZ')
        self.assertIn('NoSuchProperty_XYZ', str(cm.exception))

    def test_transient_handle_json_round_trip(self):
        # Id は Python から直接読み書きできないため、start/stop 系ツールの handle 変換を往復で確かめる
        handle = toolset_module._handle_from_json('{"id": 42}')
        self.assertEqual(toolset_module._handle_to_dict(handle), {'id': 42})
        self.assertEqual(json.loads(toolset_module._handle_to_json(toolset_module._handle_from_json('7'))), {'id': 7})
        self.assertEqual(toolset_module._handle_to_dict(None), {'id': 0})

    def test_set_anim_graph_input_animation_skeleton_mismatch_raises(self):
        mismatch_animation = unreal.EditorAssetLibrary.load_asset(SKIRT_ANIMATION_PATH)
        if mismatch_animation is None:
            self.skipTest(f'Mismatch animation fixture is unavailable: {SKIRT_ANIMATION_PATH}')
        self.assertIsInstance(mismatch_animation, unreal.AnimSequenceBase)
        anim_blueprint = self._create_anim_blueprint()

        with self.assertToolRaisesRuntimeError() as cm:
            KawaiiPhysicsToolset.set_anim_graph_input_animation(anim_blueprint, mismatch_animation)
        self.assertIn('Unable to set the AnimGraph input animation', str(cm.exception))

    def test_layout_anim_graph_refits_comment(self):
        anim_blueprint = KawaiiPhysicsToolset.create_anim_blueprint(
            TEST_FOLDER,
            'ABP_LayoutAnimGraph',
            self.skeleton,
            self._load_animation(),
        )
        comment = 'Twintail layout'
        comment_prefix = unreal.get_default_object(
            unreal.KawaiiPhysicsDeveloperSettings,
        ).get_editor_property('mcp_comment_prefix')
        KawaiiPhysicsToolset.add_kawaii_physics_node(
            anim_blueprint, 'TwintailA_L', comment=comment)
        # 2 回目の追加で最初のノードは上流側へ動くが、MCP コメント枠もレイアウトで追従する
        KawaiiPhysicsToolset.add_kawaii_physics_node(anim_blueprint, 'TwintailA_R')

        KawaiiPhysicsToolset.layout_anim_graph(anim_blueprint)

        comments = [
            info for info in KawaiiPhysicsToolset.get_anim_graph_comments(anim_blueprint)
            if info.get_editor_property('title') == comment_prefix + comment
        ]
        self.assertEqual(len(comments), 1)
        comment_info = comments[0]
        comment_node = comment_info.get_editor_property('comment_node')
        self.assertIsNotNone(comment_node)
        self.assertIsInstance(comment_node, unreal.Object)
        node_size = comment_info.get_editor_property('node_size')
        self.assertGreater(node_size.x, 0.0)
        self.assertGreater(node_size.y, 0.0)
        self._assert_compiles_without_errors(anim_blueprint)

    def test_tool_schema_requires_only_params_without_defaults(self):
        # ToolsetRegistry はデコレータ越しに Python の既定値を見られないため、ソースから各ツールの引数を読む
        source_tree = ast.parse(inspect.getsource(toolset_module))
        class_node = next(
            node for node in source_tree.body
            if isinstance(node, ast.ClassDef) and node.name == 'KawaiiPhysicsToolset'
        )
        expected_required = {}
        for node in class_node.body:
            if isinstance(node, ast.FunctionDef):
                args = node.args.args
                expected_required[node.name] = [
                    arg.arg for arg in args[:len(args) - len(node.args.defaults)]
                ]

        schema = json.loads(unreal.ToolsetRegistry.get_toolset_json_schema(KawaiiPhysicsToolset))
        self.assertTrue(schema['tools'])
        for tool in schema['tools']:
            tool_name = tool['name'].rsplit('.', 1)[-1]
            with self.subTest(tool=tool_name):
                self.assertIn(tool_name, expected_required)
                input_schema = tool.get('inputSchema', {})
                self.assertEqual(
                    sorted(input_schema.get('required', [])),
                    sorted(expected_required[tool_name]),
                )

        library_calls = {
            (node.value.attr, node.attr)
            for node in ast.walk(source_tree)
            if isinstance(node, ast.Attribute)
            and isinstance(node.value, ast.Attribute)
            and isinstance(node.value.value, ast.Name)
            and node.value.value.id == 'unreal'
            and node.value.attr.startswith('KawaiiPhysics')
            and node.value.attr.endswith('Library')
        }
        self.assertTrue(library_calls)
        for library_name, function_name in sorted(library_calls):
            with self.subTest(library=library_name, function=function_name):
                self.assertTrue(hasattr(getattr(unreal, library_name), function_name))

    def test_compile_anim_blueprint_reports_unknown_root_bone_warning(self):
        anim_blueprint = self._create_anim_blueprint()
        handle = KawaiiPhysicsToolset.add_kawaii_physics_node(anim_blueprint, 'TwintailA_L')
        KawaiiPhysicsToolset.set_graph_node_root_bone(handle, 'TotallyBogusBone_XYZ')

        messages = self._assert_compiles_without_errors(anim_blueprint)

        self.assertTrue(any(
            message.startswith('Warning:') and 'RootBone is empty' in message
            for message in messages
        ))

    def test_get_simple_world_collision_debug_info_without_entry(self):
        skeletal_mesh_component = unreal.new_object(unreal.SkeletalMeshComponent)
        self.assertIsInstance(skeletal_mesh_component, unreal.SkeletalMeshComponent)

        debug_info = KawaiiPhysicsToolset.get_simple_world_collision_debug_info(
            skeletal_mesh_component,
        )

        self.assertFalse(debug_info.get_editor_property('has_entry'))

    def test_unknown_actor_label_returns_empty(self):
        cases = [
            ('collision debug info', lambda: list(
                KawaiiPhysicsToolset.find_simple_world_collision_debug_info(
                    UNKNOWN_ACTOR_LABEL, True)), []),
            ('actor search', lambda: list(KawaiiPhysicsToolset.find_kawaii_physics_actors(
                UNKNOWN_ACTOR_LABEL, True)), []),
            ('start multiplier', lambda: json.loads(
                KawaiiPhysicsToolset.start_physics_settings_multiplier_on_actor(
                    UNKNOWN_ACTOR_LABEL, '{"Damping": 2.0}', 1.0, 0.2, 0.5,
                    [], False, True)), []),
            ('stop multiplier', lambda: KawaiiPhysicsToolset.stop_physics_settings_multiplier_on_actor(
                UNKNOWN_ACTOR_LABEL, '{"id": 1}', 0.5, [], False, True), 0),
            ('start wind', lambda: json.loads(KawaiiPhysicsToolset.start_procedural_wind_gust_on_actor(
                UNKNOWN_ACTOR_LABEL, 1.0, 1.0, 0.1, 0.3, [], [], False, True)), []),
            ('start wind direction', lambda: json.loads(
                KawaiiPhysicsToolset.start_procedural_wind_gust_on_actor(
                    UNKNOWN_ACTOR_LABEL, 1.0, 1.0, 0.1, 0.3,
                    [1.0, 0.0, 0.0], [], False, True)), []),
            ('stop external force', lambda: KawaiiPhysicsToolset.stop_transient_external_force_on_actor(
                UNKNOWN_ACTOR_LABEL, '{"id": 1}', 0.5, [], False, True), 0),
            ('set alpha', lambda: KawaiiPhysicsToolset.set_alpha_on_actor(
                UNKNOWN_ACTOR_LABEL, 1.0, [], False, True), 0),
            ('get alpha', lambda: KawaiiPhysicsToolset.get_alpha_on_actor(
                UNKNOWN_ACTOR_LABEL, [], False, True), -1.0),
            ('collider count', lambda: KawaiiPhysicsToolset.get_simple_world_collider_count_on_actor(
                UNKNOWN_ACTOR_LABEL, [], False, True), 0),
            ('runtime description', lambda: json.loads(
                KawaiiPhysicsToolset.describe_kawaii_physics_runtime_on_actor(
                    UNKNOWN_ACTOR_LABEL, True)), []),
            ('runtime reset', lambda: KawaiiPhysicsToolset.reset_dynamics_on_actor(
                UNKNOWN_ACTOR_LABEL, True), 0),
        ]
        for name, call, expected in cases:
            with self.subTest(case=name):
                if name not in ('runtime description', 'runtime reset'):
                    self._require_world()
                try:
                    self.assertEqual(call(), expected)
                except RuntimeError as error:
                    if name == 'collision debug info':
                        self.skipTest(f'No world is available for the debug info lookup: {error}')
                    raise

    def test_execute_console_command_succeeds(self):
        self._require_world()

        for prefer_pie in (True, False):
            with self.subTest(prefer_pie=prefer_pie):
                self.assertTrue(KawaiiPhysicsToolset.execute_console_command(
                    'stat none', prefer_pie))

    def test_get_bone_sampler_result_without_start_reports_not_started(self):
        # 失敗した start はサンプラ状態を作らないため、未開始のまま結果を問い合わせられる
        result = json.loads(KawaiiPhysicsToolset.get_bone_sampler_result())

        self.assertFalse(result['done'])
        self.assertEqual(result.get('error'), 'not started')
        self.assertTrue(KawaiiPhysicsToolset.stop_bone_sampler())

    def test_editor_world_getter_matches_editor_subsystem(self):
        editor_subsystem = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
        if editor_subsystem is None or editor_subsystem.get_game_world() is not None:
            self.skipTest('Requires the editor without PIE.')
        editor_world = editor_subsystem.get_editor_world()
        if editor_world is None:
            self.skipTest('No editor world is available in this environment.')

        self.assertEqual(
            unreal.KawaiiPhysicsEditorLibrary.get_editor_world_ignoring_play_mode(),
            editor_world,
        )
        self.assertEqual(toolset_module._resolve_world(False), editor_world)
        self.assertEqual(toolset_module._resolve_world(True), editor_world)

    def test_set_get_graph_node_external_forces_round_trip(self):
        anim_blueprint = self._create_anim_blueprint()
        handle = self._place_test_node(anim_blueprint, _make_request('TwintailA_L'))

        count = KawaiiPhysicsToolset.set_graph_node_external_forces(
            handle,
            json.dumps([{
                '_structType': BASIC_EXTERNAL_FORCE_STRUCT,
                'ForceDir': {'X': 0, 'Y': 1, 'Z': 0},
                'RandomForceScaleRange': {'Min': 30, 'Max': 30},
            }]),
        )
        forces = json.loads(KawaiiPhysicsToolset.get_graph_node_external_forces(handle))

        self.assertEqual(count, 1)
        self.assertEqual(len(forces), 1)
        self.assertEqual(forces[0]['_structType'], BASIC_EXTERNAL_FORCE_STRUCT)
        self.assertAlmostEqual(forces[0]['ForceDir']['Y'], 1.0)
        self.assertAlmostEqual(forces[0]['RandomForceScaleRange']['Min'], 30.0)
        self.assertTrue(forces[0]['bIsEnabled'])

        self._assert_compiles_without_errors(anim_blueprint)

    def test_compute_collision_limit_offset_converts_to_bone_local(self):
        with self.subTest(case="fixed offset"):
            bone_transform = unreal.Transform(
                unreal.Vector(0.0, 0.0, 0.0),
                toolset_module._make_rotator([-90.0, 0.0, 0.0]),
                unreal.Vector(1.0, 1.0, 1.0),
            )

            offset_location, offset_rotation = toolset_module._compute_collision_limit_offset(
                bone_transform,
                [8.0, 0.0, -75.0],
                [],
            )

            for actual, expected in zip(offset_location, [75.0, 0.0, 8.0]):
                self.assertAlmostEqual(actual, expected, places=3)
            self.assertAlmostEqual(offset_rotation[0], 90.0, places=2)
            rotator = toolset_module._make_rotator(offset_rotation)
            forward = rotator.get_forward_vector()
            up = rotator.get_up_vector()
            for actual, expected in zip([forward.x, forward.y, forward.z], [0.0, 0.0, 1.0]):
                self.assertAlmostEqual(actual, expected, places=3)
            for actual, expected in zip([up.x, up.y, up.z], [-1.0, 0.0, 0.0]):
                self.assertAlmostEqual(actual, expected, places=3)

        with self.subTest(case="round trip"):
            bone_transform = unreal.Transform(
                unreal.Vector(3.0, -4.0, 120.0),
                toolset_module._make_rotator([-35.0, 60.0, 10.0]),
                unreal.Vector(1.0, 1.0, 1.0),
            )
            location = [12.0, 5.0, 90.0]
            rotation = [15.0, -20.0, 5.0]

            offset_location, offset_rotation = toolset_module._compute_collision_limit_offset(
                bone_transform,
                location,
                rotation,
            )
            # ランタイムと同じ Offset * BoneCS で目標のコンポーネント空間トランスフォームへ戻る
            composed_location = bone_transform.transform_location(unreal.Vector(*offset_location))
            composed_rotation = bone_transform.transform_rotation(
                toolset_module._make_rotator(offset_rotation))
            target_rotation = toolset_module._make_rotator(rotation)

            for actual, expected in zip(
                    [composed_location.x, composed_location.y, composed_location.z],
                    location):
                self.assertAlmostEqual(actual, expected, places=2)
            for actual_axis, expected_axis in (
                    (composed_rotation.get_forward_vector(), target_rotation.get_forward_vector()),
                    (composed_rotation.get_up_vector(), target_rotation.get_up_vector())):
                for actual, expected in zip(
                        [actual_axis.x, actual_axis.y, actual_axis.z],
                        [expected_axis.x, expected_axis.y, expected_axis.z]):
                    self.assertAlmostEqual(actual, expected, places=3)

    def test_count_import_text_array_elements(self):
        self.assertEqual(toolset_module._count_import_text_array_elements(''), 0)
        self.assertEqual(toolset_module._count_import_text_array_elements('()'), 0)
        self.assertEqual(
            toolset_module._count_import_text_array_elements(
                '((DrivingBone=(BoneName="a(b"),Radius=1),(Radius=2))'),
            2,
        )
        self.assertEqual(
            toolset_module._append_import_text_array_element('()', '(Radius=1)'),
            '((Radius=1))',
        )
        self.assertEqual(
            toolset_module._append_import_text_array_element('((Radius=1))', '(Radius=2)'),
            '((Radius=1),(Radius=2))',
        )

    def test_add_collision_limit_appends_each_shape(self):
        anim_blueprint = self._create_anim_blueprint()
        handle = self._place_test_node(anim_blueprint, _make_request('TwintailA_L'))

        self.assertEqual(
            KawaiiPhysicsToolset.add_collision_limit(
                handle, 'Sphere', 'TwintailA_L', [0.0, 10.0, 150.0], [], 7.0),
            1,
        )
        self.assertEqual(
            KawaiiPhysicsToolset.add_collision_limit(
                handle, 'sphere', 'TwintailA_L', [0.0, 12.0, 150.0], [], 3.0, 5.0, 10.0, [], 'Inner'),
            2,
        )
        self.assertEqual(
            KawaiiPhysicsToolset.add_collision_limit(
                handle, 'Capsule', 'TwintailA_L', [0.0, 10.0, 140.0], [90.0, 0.0, 0.0], 4.0, 5.0, 20.0),
            1,
        )
        self.assertEqual(
            KawaiiPhysicsToolset.add_collision_limit(
                handle, 'TaperedCapsule', 'TwintailA_L', [0.0, 10.0, 140.0], [], 4.0, 2.0, 20.0),
            1,
        )
        self.assertEqual(
            KawaiiPhysicsToolset.add_collision_limit(
                handle, 'Box', 'TwintailA_L', [0.0, 10.0, 140.0], [], 5.0, 5.0, 10.0, [3.0, 4.0, 5.0]),
            1,
        )
        self.assertEqual(
            KawaiiPhysicsToolset.add_collision_limit(handle, 'Planar', '', [0.0, 0.0, 5.0]),
            1,
        )

        spherical_limits = KawaiiPhysicsToolset.get_graph_node_property(handle, 'SphericalLimits')
        self.assertIn('TwintailA_L', spherical_limits)
        self.assertIn('Inner', spherical_limits)
        self.assertIn('Radius1=2', KawaiiPhysicsToolset.get_graph_node_property(handle, 'TaperedCapsuleLimits'))
        self.assertIn('Extent=(X=3', KawaiiPhysicsToolset.get_graph_node_property(handle, 'BoxLimits'))
        # DrivingBone 無しの Planar はコンポーネント空間の値がそのままオフセットになる
        self.assertIn('Z=5', KawaiiPhysicsToolset.get_graph_node_property(handle, 'PlanarLimits'))
        self._assert_compiles_without_errors(anim_blueprint)

    def test_add_collision_limit_places_limit_at_bone_location(self):
        handle = self._place_test_node()
        bone_transform = toolset_module._unpack_bool_out(
            unreal.KawaiiPhysicsEditorLibrary.get_graph_node_reference_bone_transform(
                handle,
                unreal.Name('TwintailA_L'),
            ),
            None,
        )
        self.assertIsNotNone(bone_transform)
        bone_location = bone_transform.translation

        KawaiiPhysicsToolset.add_collision_limit(
            handle,
            'Sphere',
            'TwintailA_L',
            [bone_location.x, bone_location.y, bone_location.z],
        )

        # ボーン位置に置いた球はボーンローカルのオフセットが 0 になる
        spherical_limits = KawaiiPhysicsToolset.get_graph_node_property(handle, 'SphericalLimits')
        offset_text = spherical_limits.split('OffsetLocation=(', 1)[1].split(')', 1)[0]
        offset_values = [
            abs(float(part.split('=', 1)[1]))
            for part in offset_text.split(',')
        ]
        self.assertEqual(len(offset_values), 3)
        for value in offset_values:
            self.assertLess(value, 0.01)

    def test_add_collision_limit_invalid_arguments_raise(self):
        handle = self._place_test_node()

        with self.assertToolRaisesRuntimeError() as cm:
            KawaiiPhysicsToolset.add_collision_limit(handle, 'Cone', 'TwintailA_L', [0.0, 0.0, 0.0])
        self.assertIn('Unknown shape', str(cm.exception))

        with self.assertToolRaisesRuntimeError() as cm:
            KawaiiPhysicsToolset.add_collision_limit(handle, 'Sphere', '', [0.0, 0.0, 0.0])
        self.assertIn('driving_bone must not be empty', str(cm.exception))

        with self.assertToolRaisesRuntimeError() as cm:
            KawaiiPhysicsToolset.add_collision_limit(
                handle, 'Sphere', 'TotallyBogusBone_XYZ', [0.0, 0.0, 0.0])
        self.assertIn('TotallyBogusBone_XYZ', str(cm.exception))

        with self.assertToolRaisesRuntimeError() as cm:
            KawaiiPhysicsToolset.add_collision_limit(handle, 'Sphere', 'TwintailA_L', [0.0, 0.0])
        self.assertIn('location must have 3 elements', str(cm.exception))

        self.assertEqual(
            toolset_module._count_import_text_array_elements(
                KawaiiPhysicsToolset.get_graph_node_property(handle, 'SphericalLimits')),
            0,
        )

    # ===== Task D: bone sampler space =====

    def test_bone_sampler_component_space_ignores_actor_location(self):
        self._require_world()
        mesh = _load_asset_checked(GRAYCHAN_MESH_PATH)
        actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        actor = actor_subsystem.spawn_actor_from_class(
            unreal.SkeletalMeshActor,
            unreal.Vector(5000.0, 0.0, 0.0),
        )
        if actor is None:
            self.skipTest('Unable to spawn a SkeletalMeshActor in the editor world.')
        try:
            actor.set_actor_label(BONE_SAMPLER_ACTOR_LABEL)
            component = actor.get_editor_property('skeletal_mesh_component')
            if hasattr(component, 'set_skeletal_mesh_asset'):
                component.set_skeletal_mesh_asset(mesh)
            else:
                component.set_skeletal_mesh(mesh)

            means = {}
            for space in ('component', 'world'):
                started = json.loads(KawaiiPhysicsToolset.start_bone_sampler(
                    BONE_SAMPLER_ACTOR_LABEL, '^TwintailA_L$', 1, False, space))
                self.assertEqual(started['space'], space)
                # Slate のティックを待たずに採取コールバックを直接呼ぶ
                toolset_module._bone_sampler_tick(0.0)
                result = json.loads(KawaiiPhysicsToolset.get_bone_sampler_result())
                self.assertTrue(result['done'])
                self.assertEqual(result['space'], space)
                means[space] = result['bones']['TwintailA_L']['mean']

            self.assertAlmostEqual(means['world'][0] - means['component'][0], 5000.0, places=1)
            self.assertAlmostEqual(means['world'][1], means['component'][1], places=1)
            self.assertAlmostEqual(means['world'][2], means['component'][2], places=1)
        finally:
            KawaiiPhysicsToolset.stop_bone_sampler()
            # 他テストが未開始状態を前提にするため採取状態を消す
            toolset_module._BONE_SAMPLER = None
            actor_subsystem.destroy_actor(actor)

    # ===== AnimNode Function / post process / runtime inspection =====

    def test_bind_graph_node_function_creates_reuses_and_clears(self):
        anim_blueprint = self._create_anim_blueprint()
        handle = KawaiiPhysicsToolset.add_kawaii_physics_node(
            anim_blueprint, 'TwintailA_L')
        created = json.loads(KawaiiPhysicsToolset.bind_graph_node_function(
            handle, 'UpDaTe', 'UpdateKawaiiPhysicsTest'))
        self.assertEqual(created, {
            'function_name': 'UpdateKawaiiPhysicsTest', 'created': True})
        with self.assertToolRaisesRuntimeError():
            KawaiiPhysicsToolset.bind_graph_node_function(handle, 'missing', 'Other')
        with self.assertToolRaisesRuntimeError() as cm:
            KawaiiPhysicsToolset.bind_graph_node_function(handle, 'update', 'AnimGraph')
        self.assertIn('already in use', str(cm.exception))

    def test_set_post_process_anim_blueprint_on_mesh_copy(self):
        mesh = unreal.EditorAssetLibrary.duplicate_asset(
            GRAYCHAN_MESH_PATH, TEST_FOLDER + 'GrayChan_PostProcessTest')
        self.assertIsInstance(mesh, unreal.SkeletalMesh)
        anim_blueprint = KawaiiPhysicsToolset.create_post_process_anim_blueprint(
            self.skeleton, TEST_FOLDER, 'ABP_PostProcessAssignmentTest')
        self._assert_compiles_without_errors(anim_blueprint)
        self.assertTrue(KawaiiPhysicsToolset.set_post_process_anim_blueprint(
            mesh, anim_blueprint))
        self.assertEqual(mesh.get_editor_property('post_process_anim_blueprint'),
                         anim_blueprint.generated_class())
        self.assertTrue(KawaiiPhysicsToolset.set_post_process_anim_blueprint(mesh))
        self.assertIsNone(mesh.get_editor_property('post_process_anim_blueprint'))

    def test_set_post_process_anim_blueprint_rejects_invalid_blueprint(self):
        mesh = unreal.EditorAssetLibrary.duplicate_asset(
            GRAYCHAN_MESH_PATH, TEST_FOLDER + 'GrayChan_PostProcessRejectTest')
        self.assertIsInstance(mesh, unreal.SkeletalMesh)
        original = mesh.get_editor_property('post_process_anim_blueprint')

        # SequencePlayer 入力の通常 ABP は InPose を持たないため拒否される
        animation = _load_asset_checked(GRAYCHAN_IDLE_ANIMATION_PATH)
        player_blueprint = KawaiiPhysicsToolset.create_anim_blueprint(
            TEST_FOLDER, 'ABP_PostProcessPlayerInputTest', self.skeleton, animation)
        with self.assertToolRaisesRuntimeError() as cm:
            KawaiiPhysicsToolset.set_post_process_anim_blueprint(mesh, player_blueprint)
        self.assertIn('Input Pose named InPose', str(cm.exception))
        self.assertEqual(mesh.get_editor_property('post_process_anim_blueprint'), original)

        # 別スケルトン向けの Post Process ABP も拒否される
        chain_skeleton = unreal.EditorAssetLibrary.load_asset(CHAIN_SKELETON_PATH)
        if chain_skeleton is None:
            self.skipTest(f'Mismatch skeleton fixture is unavailable: {CHAIN_SKELETON_PATH}')
        chain_blueprint = KawaiiPhysicsToolset.create_post_process_anim_blueprint(
            chain_skeleton, TEST_FOLDER, 'ABP_PostProcessSkeletonMismatchTest')
        with self.assertToolRaisesRuntimeError() as cm:
            KawaiiPhysicsToolset.set_post_process_anim_blueprint(mesh, chain_blueprint)
        self.assertIn('does not match', str(cm.exception))
        self.assertEqual(mesh.get_editor_property('post_process_anim_blueprint'), original)

    def test_runtime_description_and_reset_on_skeletal_mesh_actor(self):
        self._require_world()
        mesh = _load_asset_checked(GRAYCHAN_MESH_PATH)
        anim_blueprint = self._create_anim_blueprint()
        KawaiiPhysicsToolset.add_kawaii_physics_node(anim_blueprint, 'TwintailA_L')
        self._assert_compiles_without_errors(anim_blueprint)
        actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        actor = actor_subsystem.spawn_actor_from_class(
            unreal.SkeletalMeshActor, unreal.Vector(5000.0, 0.0, 0.0))
        if actor is None:
            self.skipTest('Unable to spawn a SkeletalMeshActor in the editor world.')
        try:
            actor.set_actor_label('KP_PyToolsetRuntimeTest')
            component = actor.get_editor_property('skeletal_mesh_component')
            if hasattr(component, 'set_skeletal_mesh_asset'):
                component.set_skeletal_mesh_asset(mesh)
            else:
                component.set_skeletal_mesh(mesh)
            component.set_anim_instance_class(anim_blueprint.generated_class())
            descriptions = json.loads(
                KawaiiPhysicsToolset.describe_kawaii_physics_runtime_on_actor(
                    'KP_PyToolsetRuntimeTest', False))
            self.assertEqual(len(descriptions), 1)
            self.assertEqual(set(descriptions[0]), {
                'actor', 'component', 'animation_mode', 'anim_instance_class', 'anim_class',
                'is_sequencer_instance', 'post_process_instance_class',
                'node_count', 'alpha'})
            anim_instance = component.get_anim_instance()
            self.assertEqual(
                descriptions[0]['anim_instance_class'],
                str(anim_instance.get_class().get_name()) if anim_instance is not None else None)
            self.assertEqual(
                descriptions[0]['anim_class'],
                str(anim_blueprint.generated_class().get_name()))
            self.assertEqual(set(descriptions[0]['node_count']), {
                'main', 'linked', 'post_process', 'total'})
            self.assertEqual(KawaiiPhysicsToolset.reset_dynamics_on_actor(
                'KP_PyToolsetRuntimeTest', False), 1)
        finally:
            actor_subsystem.destroy_actor(actor)

    # ===== Task F: alpha return shape =====

    def test_unpack_float_out_handles_return_shapes(self):
        container = unreal.GameplayTagContainer()

        self.assertEqual(toolset_module._unpack_float_out((True, 0.5, container)), 0.5)
        self.assertEqual(toolset_module._unpack_float_out((0.25, container)), 0.25)
        self.assertEqual(toolset_module._unpack_float_out(0.75), 0.75)
        self.assertIsNone(toolset_module._unpack_float_out((False, 0.0, container)))
        self.assertIsNone(toolset_module._unpack_float_out(None))
        self.assertIsNone(toolset_module._unpack_float_out(container))

    def test_unpack_bool_result_handles_return_shapes(self):
        container = unreal.GameplayTagContainer()

        self.assertTrue(toolset_module._unpack_bool_result((True, container)))
        self.assertFalse(toolset_module._unpack_bool_result((False, container)))
        self.assertTrue(toolset_module._unpack_bool_result(True))
        self.assertFalse(toolset_module._unpack_bool_result(False))
        self.assertTrue(toolset_module._unpack_bool_result(container))
        self.assertFalse(toolset_module._unpack_bool_result(None))

    def test_alpha_helpers_on_component_without_anim_instance(self):
        skeletal_mesh_component = unreal.new_object(unreal.SkeletalMeshComponent)

        self.assertIsNone(toolset_module._get_alpha_on_component(
            skeletal_mesh_component,
            unreal.GameplayTagContainer(),
            False,
        ))
        self.assertFalse(toolset_module._set_alpha_on_component(
            skeletal_mesh_component,
            0.5,
            unreal.GameplayTagContainer(),
            False,
        ))


if __name__ == '__main__':
    unittest.main()
