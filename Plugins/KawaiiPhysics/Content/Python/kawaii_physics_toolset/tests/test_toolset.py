import unittest

import unreal

from kawaii_physics_toolset.toolset import KawaiiPhysicsToolset
from toolset_registry.tests.toolset_testcase import ToolCallTestCase


TEST_FOLDER = '/Game/Test/PyToolset/'
GRAYCHAN_SKELETON_PATH = '/Game/KawaiiPhysicsSample/GrayChan/Mesh/GrayChan_Skeleton'
HAIR_PRESET_PATH = '/Game/KawaiiPhysicsSample/Presets/KPP_Hair_Soft'


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


def _make_request(root_bone_pattern: str = 'Twintail[A-E]_L') -> unreal.KawaiiPhysicsNodePlacementRequest:
    request = unreal.KawaiiPhysicsNodePlacementRequest()
    request.set_editor_property('root_bone_pattern', root_bone_pattern)
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

    def _place_test_node(self) -> unreal.KawaiiPhysicsGraphNodeHandle:
        anim_blueprint = self._create_anim_blueprint()
        request = _make_request()
        handles = KawaiiPhysicsToolset.add_kawaii_physics_nodes(
            anim_blueprint,
            [request],
            unreal.KawaiiPhysicsPlacementUpsertKey.NONE,
            '',
        )
        self.assertEqual(len(handles), 1)
        return handles[0]

    def test_create_anim_blueprint(self):
        anim_blueprint = self._create_anim_blueprint()
        self.assertEqual(anim_blueprint.get_editor_property('target_skeleton'), self.skeleton)

    def test_create_anim_blueprint_skeleton_none_raises(self):
        with self.assertToolRaisesRuntimeError():
            KawaiiPhysicsToolset.create_anim_blueprint(TEST_FOLDER, 'ABP_Invalid', None)

    def test_validate_add_collect_success(self):
        anim_blueprint = self._create_anim_blueprint()
        request = _make_request()

        errors = KawaiiPhysicsToolset.validate_placement_requests(anim_blueprint, [request])
        self.assertEqual(errors, [])

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


if __name__ == '__main__':
    unittest.main()
