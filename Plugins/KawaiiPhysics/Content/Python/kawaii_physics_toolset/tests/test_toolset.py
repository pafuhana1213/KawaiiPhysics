from __future__ import annotations

import ast
import inspect
import json
import os
import runpy
import tempfile
import time
from types import SimpleNamespace
import unittest
from unittest import mock

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
SKIRT_MESH_PATH = '/Game/KawaiiPhysicsSample/Model/Skirt/Meshs/SKM_Skirt'
SKIRT_SKELETON_PATH = '/Game/KawaiiPhysicsSample/Model/Skirt/Meshs/SK_Skirt'
SKIRT_CONSTRAINTS_PATH = '/Game/KawaiiPhysicsSample/Examples/05_Advanced/DA_5_2_SkirtConstraints'
SKIRT_RING_ROOTS = ([f'skirt_{i:02d}_01_l' for i in range(1, 9)] +
                    [f'skirt_{i:02d}_01_r' for i in range(8, 0, -1)])
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


def _is_list_annotation(annotation) -> bool:
    # UE 5.8 の MCP は省略可能なリスト引数を変換できないため、リスト引数は既定値があってもスキーマ上は必須になる
    return (isinstance(annotation, ast.Subscript) and isinstance(annotation.value, ast.Name)
            and annotation.value.id in ('list', 'dict', 'set'))


class KawaiiPhysicsToolsetTestCase(ToolCallTestCase):
    """Test KawaiiPhysicsToolset tool calls."""

    def test_skirt_geometry_clearance_and_penetration(self):
        sphere = {'limit_type': 'Spherical', 'location': [0, 0, 0],
                  'radius0': 2.0, 'inner_sphere': False}
        clear = toolset_module._geom_point_clearance
        depth = toolset_module._geom_segment_penetration
        self.assertAlmostEqual(clear(sphere, [4, 0, 0], 1), 1)
        self.assertAlmostEqual(clear(sphere, [0, 0, 0], 0), -2)
        self.assertAlmostEqual(depth(sphere, [-3, 0, 0], [3, 0, 0]), 2)
        self.assertAlmostEqual(depth(sphere, [2, -3, 0], [2, 3, 0]), 0)
        self.assertAlmostEqual(depth(sphere, [0, 0, 0], [0, 0, 0]), 2)
        sphere['inner_sphere'] = True
        self.assertAlmostEqual(clear(sphere, [0, 0, 0], 0.5), 1.5)
        self.assertAlmostEqual(clear(sphere, [3, 0, 0], 0), -1)
        self.assertAlmostEqual(depth(sphere, [0, 0, 0], [3, 0, 0]), 1)
        capsule = {'limit_type': 'Capsule', 'start': [0, 0, 1],
                   'end': [0, 0, -1], 'radius0': 1.0}
        self.assertAlmostEqual(clear(capsule, [2, 0, 0], 0.5), 0.5)
        self.assertAlmostEqual(depth(capsule, [-2, 0, 0], [2, 0, 0]), 1)
        self.assertAlmostEqual(depth(capsule, [1, -2, 0], [1, 2, 0]), 0)
        taper = {'limit_type': 'TaperedCapsule', 'start': [0, 0, 0],
                 'end': [4, 0, 0], 'radius0': 2.0, 'radius1': 1.0}
        self.assertAlmostEqual(clear(taper, [-2, 0, 0], 0), 0, places=5)
        self.assertAlmostEqual(clear(taper, [5, 0, 0], 0), 0, places=5)
        self.assertAlmostEqual(depth(taper, [0, -3, 0], [0, 3, 0]), 2, places=3)
        taper['end'] = [0, 0, 0]
        self.assertAlmostEqual(clear(taper, [0, 0, 0], 0), -2)
        box = {'limit_type': 'Box', 'location': [0, 0, 0],
               'rotation': [0, 0, 2**-0.5, 2**-0.5], 'extent': [2, 1, 1]}
        self.assertAlmostEqual(clear(box, [0, 0, 0], 0), -1)
        self.assertAlmostEqual(clear(box, [0, 3, 0], 0), 1)
        self.assertAlmostEqual(depth(box, [0, -3, 0], [0, 3, 0]), 1, places=3)
        plane = {'limit_type': 'Planar', 'location': [0, 0, 0],
                 'plane_normal': [0, 0, 1]}
        self.assertAlmostEqual(clear(plane, [0, 0, 2], 0.5), 1.5)
        self.assertAlmostEqual(clear(plane, [0, 0, -2], 0.5), -2.5)
        self.assertAlmostEqual(depth(plane, [0, 0, 2], [0, 0, -3]), 3)
        self.assertAlmostEqual(toolset_module._geom_segment_distance(
            [0, 0, 0], [0, 0, 0], [3, 0, 0], [3, 0, 0]), 3)
        self.assertEqual(toolset_module._geom_closest_point_segment(
            [3, 0, 0], [1, 0, 0], [1, 0, 0]), (1, 0, 0))
        self.assertAlmostEqual(toolset_module._geom_segment_distance(
            [-2, 0, 0], [2, 0, 0], [0, -2, 0], [0, 2, 0]), 0)

    def test_tapered_capsule_closed_distance_matches_dense_sampling(self):
        cases = [
            ([0, 0, 0], [4, 0, 0], 2.0, 1.0, [2, 2, 0]),
            ([0, 0, 0], [4, 0, 0], 2.0, 1.0, [-3, 1, 0]),
            ([1, 2, 3], [3, 5, 7], 0.5, 1.75, [3, 3, 4]),
            ([0, 0, 0], [2, 0, 0], 4.0, 1.0, [2, 2, 0]),
            ([0, 0, 0], [0, 0, 0], 1.0, 2.0, [1, 2, 0]),
        ]
        for start, end, r0, r1, point in cases:
            limit = {'limit_type': 'TaperedCapsule', 'start': start, 'end': end,
                     'radius0': r0, 'radius1': r1}
            sampled = min(
                toolset_module._geom_norm(toolset_module._geom_sub(
                    point, toolset_module._geom_lerp(start, end, i / 2000)))
                - (r0 + (r1 - r0) * i / 2000)
                for i in range(2001))
            self.assertAlmostEqual(
                toolset_module._geom_tapered_distance(limit, point), sampled,
                delta=1e-3)

    def test_tapered_capsule_penetration_144_segments_four_limits_under_one_second(self):
        limits = [
            {'limit_type': 'TaperedCapsule', 'start': [x, y, -2],
             'end': [x + 1, y, 2], 'radius0': 6.0, 'radius1': 4.0}
            for x, y in ((-1, -1), (1, -1), (-1, 1), (1, 1))]
        segments = [([x / 2 - 3, y / 2 - 3, -2],
                     [x / 2 - 3, y / 2 - 3, 2])
                    for x in range(12) for y in range(12)]
        start = time.perf_counter()
        depths = [toolset_module._geom_segment_penetration(limit, a, b)
                  for limit in limits for a, b in segments]
        elapsed = time.perf_counter() - start
        self.assertEqual(len(depths), 576)
        self.assertTrue(any(value > 0 for value in depths))
        self.assertLess(elapsed, 1.0)

    def test_penetration_frame_96_bones_144_segments_four_limits_under_10ms(self):
        positions = {f'b{i}': [10.0 * (i % 8), 10.0 * (i // 8), 0.0]
                     for i in range(96)}
        pairs = ([(f'b{i}', f'b{i + 1}') for i in range(96) if i % 8 != 7] +
                 [(f'b{i}', f'b{i + 8}') for i in range(60)])
        self.assertEqual(len(pairs), 144)
        limits = [{
            'limit_type': 'TaperedCapsule', 'start': [x, y, -2.0],
            'end': [x, y, 2.0], 'radius0': 1.5, 'radius1': 1.0,
            'source_type': 'AnimNode', 'source_array': 'TaperedCapsuleLimits',
            'source_index': i, 'driving_bone': 'root'}
            for i, (x, y) in enumerate(((5, 0), (25, 20), (45, 50), (65, 80)))]
        target = {'identity': {'component': 'Mesh'},
                  'endpoint_order': tuple(positions), 'previous': None, 'maxima': {},
                  'segment_specs': [(a, b, 'vertical' if i >= 84 else 'horizontal',
                                     (a, b)) for i, (a, b) in enumerate(pairs)]}
        elapsed_ms = []
        for _ in range(5):
            target['previous'] = None
            stats = toolset_module._new_penetration_stats()
            started = time.perf_counter()
            valid = toolset_module._accumulate_penetration_frame(
                target, positions, limits, stats, 1.0, 1)
            elapsed_ms.append((time.perf_counter() - started) * 1000.0)
            self.assertTrue(valid)
            self.assertEqual(stats['all']['samples'], 144)
            self.assertGreater(stats['all']['max'], 0.0)
            self.assertGreater(stats['all']['samples_over_threshold'], 0)
        self.assertLess(min(elapsed_ms), 10.0, elapsed_ms)

    def test_ring_constraints_pure(self):
        hierarchy = {'a': ['a1'], 'a1': ['a2'], 'a2': [],
                     'b': ['b1'], 'b1': [], 'c': ['c1'], 'c1': ['c2'], 'c2': []}
        build = toolset_module._build_ring_constraints
        closed = build(hierarchy, ['a', 'b', 'c'], 1, 2, True, '', False)
        self.assertEqual(closed['count'], 4)
        self.assertEqual(len(closed['missing']), 2)
        self.assertTrue(closed['unequal_lengths'])
        self.assertEqual(build(hierarchy, ['a', 'b', 'c'], 1, 1, False, '', False)['count'], 2)
        self.assertEqual(build(hierarchy, ['a', 'c'], 1, 1, True, '', False)['count'], 1)
        hierarchy['a1'] = ['a2', 'a3']
        self.assertEqual(build(hierarchy, ['a', 'c'], 1, -1, True, '', False)['branched'][0]['bone'], 'a1')
        self.assertEqual(build(hierarchy, ['a', 'c'], 1, -1, True, '', False)['count'], 1)

    def test_ring_constraints_reject_empty_result_pure(self):
        build = toolset_module._build_ring_constraints
        with self.assertRaisesRegex(ValueError, 'columns=.*missing='):
            build({'a': ['a1'], 'a1': []}, ['a'], 0, -1, True, '', False)
        with self.assertRaisesRegex(ValueError, 'columns=.*missing='):
            build({'a': [], 'b': []}, ['a', 'b'], 1, 1, False, '', False)

    def test_ring_constraints_skirt_asset(self):
        mesh = unreal.EditorAssetLibrary.load_asset(SKIRT_MESH_PATH)
        asset = unreal.EditorAssetLibrary.load_asset(SKIRT_CONSTRAINTS_PATH)
        if mesh is None or asset is None:
            self.skipTest('Skirt mesh or constraint asset is unavailable.')
        result = json.loads(KawaiiPhysicsToolset.build_ring_bone_constraints(
            mesh, SKIRT_RING_ROOTS, 1, 4, True, '', False))
        self.assertEqual(result['count'], 64)
        expected = {frozenset((pair['bone1'], pair['bone2']))
                    for pair in result['pairs']}
        actual = {frozenset((str(item.get_editor_property('bone_reference1').get_editor_property('bone_name')),
                             str(item.get_editor_property('bone_reference2').get_editor_property('bone_name'))))
                  for item in asset.get_editor_property('bone_constraints_data')}
        self.assertEqual(expected, actual)

    def test_radius_length_rates_pure(self):
        children = {'a': ['a1'], 'a1': ['a2'], 'a2': [],
                    'b': ['b1'], 'b1': ['b2'], 'b2': []}
        lengths = {'a1': 2.0, 'a2': 3.0, 'b1': 2.0, 'b2': 3.0}
        chains = toolset_module._radius_chain_rates(
            children, lengths, [('a', set()), ('b', {'b2'})], 1.0)
        self.assertEqual([r['length_rate'] for r in chains[0]['records']],
                         [0.0, 2/6, 5/6, 1.0])
        self.assertEqual([r['length_rate'] for r in chains[1]['records']],
                         [0.0, 2/3, 1.0])
        radius, keys, bones, warnings = toolset_module._radius_keys_and_bones(
            chains, [2.0, 3.0, 4.0], 1.0)
        self.assertEqual(radius, 4.0)
        self.assertEqual(keys[-1]['time'], 1.0)
        self.assertTrue(any('unequal bone counts' in w for w in warnings))
        self.assertEqual(len(bones), 5)

    def test_radius_keys_tip_dummy_uses_its_own_depth_pure(self):
        children = {f'{root}{depth}': [f'{root}{depth + 1}'] if depth < 3 else []
                    for root in ('a', 'b') for depth in range(4)}
        lengths = {f'{root}{depth}': 1.0 for root in ('a', 'b')
                   for depth in range(1, 4)}
        chains = toolset_module._radius_chain_rates(
            children, lengths, [('a0', set()), ('b0', set())], 10.0)
        _, keys, _, warnings = toolset_module._radius_keys_and_bones(
            chains, [1, 2, 3, 4, 5, 6], 10.0)
        self.assertAlmostEqual(keys[-1]['value'], 5 / 6)
        self.assertIn('radius_by_depth has 1 unused entries (depth > 4).', warnings)

    def test_constraint_data_to_pair_uses_enum_name_pure(self):
        class Data:
            def get_editor_property(self, name):
                if name.startswith('bone_reference'):
                    return SimpleNamespace(get_editor_property=lambda _: name)
                return {'exclude_from_subdivision': False,
                        'override_compliance': True,
                        'compliance_type': SimpleNamespace(name='LEATHER')}[name]
        self.assertEqual(toolset_module._constraint_data_to_pair(Data())['compliance_type'],
                         'Leather')

    def test_bone_constraints_data_asset_pairs(self):
        factory = unreal.DataAssetFactory()
        factory.set_editor_property('data_asset_class',
                                    unreal.KawaiiPhysicsBoneConstraintsDataAsset)
        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            'DA_PairsTest', TEST_FOLDER,
            unreal.KawaiiPhysicsBoneConstraintsDataAsset, factory)
        self.assertIsNotNone(asset)
        first = [{'bone1': 'a', 'bone2': 'b', 'compliance_type': 'Leather'}]
        result = json.loads(KawaiiPhysicsToolset.set_bone_constraints_data_asset_pairs(
            asset, json.dumps({'pairs': first}), 'replace', False))
        self.assertEqual(result['after_count'], 1)
        self.assertEqual(str(asset.get_editor_property('bone_constraints_data')[0]
                             .get_editor_property('bone_reference1').get_editor_property('bone_name')), 'a')
        appended = [{'bone1': 'b', 'bone2': 'a'}, {'bone1': 'b', 'bone2': 'c'}]
        result = json.loads(KawaiiPhysicsToolset.set_bone_constraints_data_asset_pairs(
            asset, json.dumps(appended), 'append', False))
        self.assertEqual(result['after_count'], 2)
        self.assertEqual(len(result['skipped_existing']), 1)
        dry = json.loads(KawaiiPhysicsToolset.set_bone_constraints_data_asset_pairs(
            asset, json.dumps(first), 'replace', True))
        self.assertEqual(dry['after_count'], 1)
        self.assertEqual(len(asset.get_editor_property('bone_constraints_data')), 2)
        for mode in ('replace', 'append'):
            with self.assertToolRaisesRuntimeError():
                KawaiiPhysicsToolset.set_bone_constraints_data_asset_pairs(
                    asset, json.dumps({'pairs': []}), mode, False)
        self.assertEqual(len(asset.get_editor_property('bone_constraints_data')), 2)
        with self.assertToolRaisesRuntimeError():
            KawaiiPhysicsToolset.set_bone_constraints_data_asset_pairs(
                asset, json.dumps([{'bone1': 'c', 'bone2': 'd'},
                                   {'bone1': 'd', 'bone2': 'c'}]), 'replace', False)
        for invalid in ([{'bone1': '', 'bone2': 'd'}],
                        [{'bone1': 'c', 'bone2': 'c'}],
                        [{'bone1': 'c', 'bone2': 'd',
                          'compliance_type': 'Invalid'}]):
            with self.assertToolRaisesRuntimeError():
                KawaiiPhysicsToolset.set_bone_constraints_data_asset_pairs(
                    asset, json.dumps(invalid), 'replace', False)
        self.assertEqual(len(asset.get_editor_property('bone_constraints_data')), 2)
        result = json.loads(KawaiiPhysicsToolset.set_bone_constraints_data_asset_pairs(
            asset, json.dumps(first), 'replace', False))
        self.assertEqual(len(result['removed']), 1)
        self.assertEqual(len(asset.get_editor_property('bone_constraints_data')), 1)

    def test_radius_by_depth_skirt_node(self):
        mesh = unreal.EditorAssetLibrary.load_asset(SKIRT_MESH_PATH)
        skeleton = unreal.EditorAssetLibrary.load_asset(SKIRT_SKELETON_PATH)
        if mesh is None or skeleton is None:
            self.skipTest('Skirt mesh or skeleton is unavailable.')
        self.skeleton = skeleton
        blueprint = self._create_anim_blueprint('ABP_RadiusByDepth')
        handle = self._place_test_node(
            blueprint, _make_request(root_bone_pattern='skirt_01_01_l'))
        self._set_graph_node_property(
            handle, 'AdditionalRootBones',
            '((RootBone=(BoneName="skirt_02_01_l")),'
            '(RootBone=(BoneName="skirt_03_01_l")))')
        self._set_graph_node_property(handle, 'DummyBoneLength', '3.0')
        radii = [2.0, 2.0, 3.0, 4.0, 4.5]
        with self.assertToolRaisesRuntimeError():
            KawaiiPhysicsToolset.set_graph_node_radius_by_depth(handle, radii, None, True)
        old_settings = KawaiiPhysicsToolset.get_graph_node_property(handle, 'PhysicsSettings')
        dry = json.loads(KawaiiPhysicsToolset.set_graph_node_radius_by_depth(
            handle, radii, mesh, True))
        self.assertTrue(dry['dry_run'])
        self.assertEqual(old_settings,
                         KawaiiPhysicsToolset.get_graph_node_property(handle, 'PhysicsSettings'))
        result = json.loads(KawaiiPhysicsToolset.set_graph_node_radius_by_depth(
            handle, radii, mesh, False))
        self.assertEqual(result['radius'], 4.5)
        self.assertEqual(len(result['keys']), 6)
        for bone in result['bones']:
            self.assertLessEqual(abs(bone['error']), 0.05)
        settings = KawaiiPhysicsToolset.get_graph_node_property(handle, 'PhysicsSettings')
        curve = KawaiiPhysicsToolset.get_graph_node_property(handle, 'RadiusCurveData')
        self.assertIn('Radius=4.5', settings)
        self.assertIn('RCIM_Linear', curve)
        self._set_graph_node_property(handle, 'BoneSubdivisionCount', '1')
        warned = json.loads(KawaiiPhysicsToolset.set_graph_node_radius_by_depth(
            handle, radii, mesh, True))
        self.assertTrue(any('BoneSubdivisionCount' in w for w in warned['warnings']))
        curve_factory = unreal.CurveFactory()
        curve_factory.set_editor_property('curve_class', unreal.CurveFloat)
        external = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            'Curve_ExternalRadius', TEST_FOLDER, unreal.CurveFloat, curve_factory)
        self.assertIsNotNone(external)
        self._set_graph_node_property(
            handle, 'RadiusCurveData',
            f'(ExternalCurve={external.get_path_name()})')
        with self.assertToolRaisesRuntimeError():
            KawaiiPhysicsToolset.set_graph_node_radius_by_depth(handle, radii, mesh, False)

    def test_penetration_statistics_pure(self):
        stats = toolset_module._new_penetration_stats()
        add = toolset_module._accumulate_penetration_sample
        add(stats, 'vertical', 0.0, 1.0, False, {'frame': 1})
        add(stats, 'vertical', 2.0, 1.0, False, {'frame': 2})
        add(stats, 'vertical', 0.0, 1.0, True, None)
        add(stats, 'vertical', 4.0, 1.0, False, {'frame': 4})
        result = toolset_module._penetration_stats_result(stats)
        self.assertEqual(result['vertical']['samples'], 3)
        self.assertEqual(result['vertical']['mean_positive'], 3.0)
        self.assertEqual(result['vertical']['samples_over_threshold'], 2)
        self.assertEqual(result['vertical']['stale_or_unknown'], 1)
        self.assertEqual(result['vertical']['worst']['frame'], 4)
        self.assertEqual(result['all']['max'], 4.0)

    def test_penetration_unchanged_frames_are_measured_pure(self):
        target = {'identity': {'component': 'Mesh'},
                  'endpoint_order': ('a', 'b'), 'previous': None,
                  'unchanged_frames': 0, 'maxima': {},
                  'segment_specs': [('a', 'b', 'vertical', ('a', 'b'))]}
        positions = {'a': [0.0, 0.0, 0.0], 'b': [1.0, 0.0, 0.0]}
        limits = [{'limit_type': 'Spherical', 'inner_sphere': False,
                   'location': [0.0, 0.0, 0.0], 'radius0': 2.0,
                   'source_type': 'AnimNode', 'source_array': 'SphericalLimits',
                   'source_index': 0, 'driving_bone': 'a'}]
        stats = toolset_module._new_penetration_stats()
        sample = toolset_module._accumulate_penetration_frame
        self.assertTrue(sample(target, positions, limits, stats, 1.0, 1))
        self.assertTrue(sample(target, positions, limits, stats, 1.0, 2))
        self.assertEqual(stats['all']['samples'], 2)
        self.assertEqual(stats['all']['stale_or_unknown'], 0)
        self.assertEqual(target['unchanged_frames'], 1)
        self.assertTrue(sample(target, positions, [], stats, 1.0, 3))
        self.assertEqual(stats['all']['samples'], 3)
        self.assertEqual(target['unchanged_frames'], 2)
        self.assertFalse(sample(target, {}, [], stats, 1.0, 4))
        self.assertEqual(stats['all']['stale_or_unknown'], 1)

    def test_multiple_actor_accumulation_and_result_pure(self):
        def actor(label):
            segment = {'bone1': 'a', 'bone2': 'b', 'category': 'vertical'}
            target = {'identity': {'component': label}, 'segments': [segment],
                      'endpoint_order': ('a', 'b'), 'previous': None,
                      'unchanged_frames': 0, 'maxima': {},
                      'segment_specs': [('a', 'b', 'vertical', ('a', 'b'))]}
            return {'targets': [target], 'stats': toolset_module._new_penetration_stats(),
                    'frames_collected': 0, 'unchanged_frames': 0, 'excluded': 0,
                    'reasons': [], 'world': {}, 'error': '', 'notes': []}

        primary, extra = actor('Candidate'), actor('Baseline')
        state = {'actor': 'Candidate', 'actor_states': {'Candidate': primary,
                 'Baseline': extra}, 'threshold': 1.0, 'frames_seen': 1,
                 'frames': 2, 'done': False, 'error': '', 'world': {},
                 'fixed_frame_rate': 30.0, 'cost_total_ms': 0.0,
                 'cost_samples': 0, 'cost_max_ms': 0.0, 'notes': []}
        positions = {'a': [0, 0, 0], 'b': [1, 0, 0]}
        def sample(radius):
            return {'node': (positions, [{
                'limit_type': 'Spherical', 'inner_sphere': False,
                'location': [0, 0, 0], 'radius0': radius,
                'source_type': 'AnimNode', 'source_array': 'SphericalLimits',
                'source_index': 0, 'driving_bone': 'a'}])}
        for actor_state in (primary, extra):
            actor_state['targets'][0]['node_id'] = 'node'
        toolset_module._accumulate_penetration_actor_tick(state, primary, sample(2), 0.1)
        toolset_module._accumulate_penetration_actor_tick(state, extra, sample(0.5), 0.1)
        state['frames_seen'] = 2
        toolset_module._accumulate_penetration_actor_tick(state, primary, sample(2), 0.2)
        toolset_module._accumulate_penetration_actor_tick(state, extra, sample(0.5), 0.2)
        state['done'] = True
        result = toolset_module._penetration_sampler_result(state)
        self.assertEqual(result['frames_collected'], 2)
        self.assertEqual(result['status'], 'penetration')
        self.assertEqual(result['actors']['Candidate']['statistics'], result['statistics'])
        self.assertEqual(result['actors']['Baseline']['status'], 'ok')
        self.assertEqual(result['actors']['Baseline']['frames_collected'], 2)
        self.assertEqual(result['actors']['Baseline']['unchanged_frames'], 1)
        self.assertEqual(result['actors']['Candidate']['top_events'][0]['time'], 0.1)

    def test_multiple_actor_record_paths_and_missing_result_pure(self):
        used = {os.path.normcase('C:/records/run.jsonl')}
        path = toolset_module._extra_record_path('C:/records/run.jsonl', 'Base / A', used)
        self.assertEqual(path, 'C:/records/run__Base_A.jsonl')
        self.assertEqual(toolset_module._extra_record_path(
            'C:/records/run.jsonl', 'Base / A', used),
            'C:/records/run__Base_A__2.jsonl')
        primary = {'targets': [], 'start_result': {'status': 'not_found'}}
        missing = {'targets': [], 'start_result': {'status': 'no_nodes'}}
        state = {'actor': 'Candidate', 'actor_states': {'Candidate': primary,
                 'Baseline': missing}}
        self.assertEqual(toolset_module._penetration_sampler_result(state)['actors']
                         ['Baseline']['status'], 'no_nodes')

    def test_penetration_error_overrides_statistics_pure(self):
        stats = toolset_module._new_penetration_stats()
        toolset_module._accumulate_penetration_sample(
            stats, 'vertical', 2.0, 1.0, False, {'frame': 1})
        state = {'targets': [], 'stats': stats, 'error': 'PIE ended', 'done': True,
                 'excluded': 0, 'reasons': [], 'world': {}, 'frames_collected': 1,
                 'unchanged_frames': 0, 'frames_seen': 1, 'frames': 2,
                 'threshold': 1.0, 'fixed_frame_rate': None, 'cost_total_ms': 0.0,
                 'cost_samples': 0, 'cost_max_ms': 0.0, 'notes': []}
        result = toolset_module._penetration_sampler_result(state)
        self.assertEqual(result['status'], 'error')
        self.assertEqual(result['error'], 'PIE ended')
        self.assertEqual(result['statistics']['all']['samples'], 1)

    def test_penetration_error_status_with_empty_exception_message_pure(self):
        state = {'targets': [], 'stats': toolset_module._new_penetration_stats(),
                 'error': 'RuntimeError: ', 'done': True, 'excluded': 0,
                 'reasons': [], 'world': {}, 'frames_collected': 0,
                 'unchanged_frames': 0, 'frames_seen': 0, 'frames': 1,
                 'threshold': 1.0, 'fixed_frame_rate': None,
                 'cost_total_ms': 0.0, 'cost_samples': 0,
                 'cost_max_ms': 0.0, 'notes': []}
        result = toolset_module._penetration_sampler_result(state)
        self.assertEqual(result['status'], 'error')
        self.assertEqual(result['error'], 'RuntimeError: ')

    def test_penetration_failed_start_hides_previous_result_pure(self):
        toolset_module._COLLISION_PENETRATION_SAMPLER = {
            'done': True, 'failed_start': {'status': 'ok'}}
        prepared = {'targets': [], 'start_result': {'status': 'not_found',
                    'checked': 0, 'excluded': 0, 'reasons': [],
                    'fixed_frame_rate': None}}
        try:
            with mock.patch.object(toolset_module, '_prepare_penetration_actor',
                                   return_value=prepared):
                started = json.loads(KawaiiPhysicsToolset.start_collision_penetration_sampler(
                    'Missing', [], 1, True, [], None, 0, -1, True, 1.0, 0,
                    30.0, None, [], []))
            current = json.loads(KawaiiPhysicsToolset.get_collision_penetration_sampler_result())
            self.assertEqual(current['status'], started['status'])
            self.assertNotEqual(current['status'], 'ok')
        finally:
            toolset_module._COLLISION_PENETRATION_SAMPLER = None

    def test_record_header_contains_closed_pure(self):
        make = toolset_module._make_record_header
        self.assertFalse(make('Actor', {}, 0, [], [], False)['closed'])
        self.assertTrue(make('Actor', {}, 0, [], [], True)['closed'])

    def test_bone_sample_nan_frame_breaks_step_and_all_nan_reports_null_pure(self):
        accumulator = toolset_module._make_bone_sample_accumulator()
        for sample in ([0, 0, 0], [float('nan'), 0, 0], [3, 0, 0]):
            toolset_module._accumulate_bone_sample(accumulator, sample)
        self.assertEqual(accumulator['step_count'], 0)
        self.assertEqual(accumulator['count'], 2)
        self.assertTrue(accumulator['nan'])
        empty = toolset_module._make_bone_sample_accumulator()
        toolset_module._accumulate_bone_sample(empty, [float('nan'), 0, 0])
        try:
            toolset_module._BONE_SAMPLER = {
                'bones': {'all_nan': empty}, 'done': True, 'frames_collected': 1,
                'space': 'component', 'error': ''}
            result = json.loads(KawaiiPhysicsToolset.get_bone_sampler_result())
            for field in ('min', 'max', 'mean'):
                self.assertIsNone(result['bones']['all_nan'][field])
            self.assertEqual(result['bones']['all_nan']['rms_step'], 0.0)
        finally:
            toolset_module._BONE_SAMPLER = None

    def test_bone_sampler_targets_qualify_duplicate_component_names_pure(self):
        components = [SimpleNamespace(get_all_socket_names=lambda: ['bone'],
                                      get_name=lambda: 'CharacterMesh0',
                                      get_owner=lambda label=label: SimpleNamespace(label=label))
                      for label in ('A', 'B')]
        with mock.patch.object(toolset_module, '_actor_label',
                               side_effect=lambda owner: owner.label):
            targets = toolset_module._make_bone_sampler_targets(components, 'bone')
        self.assertEqual([key for key, _, _ in targets],
                         ['A.CharacterMesh0.bone', 'B.CharacterMesh0.bone'])

    def test_simple_world_collider_count_calls_library_directly_pure(self):
        component = object()
        library = SimpleNamespace(get_simple_world_collider_count_on_component=
                                  mock.Mock(return_value=3))
        fake_unreal = SimpleNamespace(KawaiiPhysicsLibrary=library)
        with mock.patch.object(toolset_module, 'unreal', fake_unreal), \
                mock.patch.object(toolset_module, '_find_skeletal_mesh_components_by_label',
                                  return_value=[component]), \
                mock.patch.object(toolset_module, '_make_tag_container', return_value='tags'):
            count = KawaiiPhysicsToolset.get_simple_world_collider_count_on_actor(
                'Actor', [], False, True)
        self.assertEqual(count, 3)
        library.get_simple_world_collider_count_on_component.assert_called_once_with(
            component, 'tags', False)

    def test_init_unreal_logs_skills_import_error_pure(self):
        import builtins
        real_import = builtins.__import__
        registration = SimpleNamespace(register_toolsets=mock.Mock())
        warning = mock.Mock()
        fake_unreal = SimpleNamespace(log_warning=warning)

        def import_module(name, globals=None, locals=None, fromlist=(), level=0):
            fromlist = fromlist or ()
            if name == 'kawaii_physics_toolset' and 'registration' in fromlist:
                return SimpleNamespace(registration=registration)
            if name == 'kawaii_physics_toolset' and 'skills' in fromlist:
                raise ImportError('skills unavailable')
            if name == 'unreal':
                return fake_unreal
            return real_import(name, globals, locals, fromlist, level)

        path = os.path.normpath(os.path.join(
            os.path.dirname(toolset_module.__file__), '..', 'init_unreal.py'))
        with mock.patch('builtins.__import__', side_effect=import_module):
            runpy.run_path(path)
        registration.register_toolsets.assert_called_once_with()
        warning.assert_called_once_with(
            'KawaiiPhysics Toolset registration failed: skills unavailable')

    def test_penetration_stopped_before_requested_frames_pure(self):
        stats = toolset_module._new_penetration_stats()
        toolset_module._accumulate_penetration_sample(
            stats, 'vertical', 2.0, 1.0, False, {'frame': 1})
        state = {'targets': [], 'stats': stats, 'error': '', 'done': True,
                 'excluded': 0, 'reasons': [], 'world': {}, 'frames_collected': 1,
                 'unchanged_frames': 0, 'frames_seen': 1, 'frames': 2,
                 'threshold': 1.0, 'fixed_frame_rate': None, 'cost_total_ms': 0.0,
                 'cost_samples': 0, 'cost_max_ms': 0.0, 'notes': []}
        result = toolset_module._penetration_sampler_result(state)
        self.assertEqual(result['status'], 'stopped')
        self.assertEqual(result['frames_requested'], 2)
        self.assertEqual(result['frames_collected'], 1)
        self.assertEqual(result['statistics']['all']['samples'], 1)
        state['frames_collected'] = 2
        self.assertEqual(toolset_module._penetration_sampler_result(state)['status'],
                         'penetration')
        state['frames_collected'] = 1
        state['error'] = 'PIE ended'
        result = toolset_module._penetration_sampler_result(state)
        self.assertEqual(result['status'], 'error')
        self.assertEqual(result['error'], 'PIE ended')

    def test_penetration_start_requires_segments_and_limits_on_same_node_pure(self):
        def node(name, bones, limits):
            return {'component': 'Mesh', 'anim_instance_class': 'ABP_C',
                    'node_index': 0 if name == 'a' else 1, 'root_bone': name,
                    'tag': '', 'bones': bones, 'limits': limits, 'constraints': []}
        bones_a = [{'index': 0, 'key': 'a', 'dummy_type': 'None', 'parent_index': -1},
                   {'index': 1, 'key': 'a1', 'dummy_type': 'None', 'parent_index': 0}]
        bones_b = [{'index': 0, 'key': 'b', 'dummy_type': 'None', 'parent_index': -1}]
        select = toolset_module._build_penetration_targets
        targets, checked, _, _, status = select(
            [node('a', bones_a, [{'enabled': False}]),
             node('b', bones_b, [{'enabled': True}])], [], 0, -1, True)
        self.assertEqual(status, 'nothing_checked')
        self.assertEqual(checked, 0)
        self.assertEqual(targets, [])
        targets, checked, _, _, status = select(
            [node('a', bones_a, [{'enabled': True}])], [], 0, -1, True)
        self.assertEqual(status, 'running')
        self.assertEqual(checked, 1)
        self.assertEqual(len(targets), 1)

    def test_penetration_segment_classification_pure(self):
        bones = [
            {'index': 0, 'key': 'a', 'dummy_type': 'None', 'parent_index': -1},
            {'index': 1, 'key': 'a#inter1', 'dummy_type': 'InterBone', 'parent_index': 0},
            {'index': 2, 'key': 'a1', 'dummy_type': 'None', 'parent_index': 1},
            {'index': 3, 'key': 'a1#tip', 'dummy_type': 'Tip', 'parent_index': 2},
            {'index': 4, 'key': 'b', 'dummy_type': 'None', 'parent_index': -1},
            {'index': 5, 'key': 'b1', 'dummy_type': 'None', 'parent_index': 4},
            {'index': 6, 'key': 'c', 'dummy_type': 'None', 'parent_index': -1},
            {'index': 7, 'key': 'c1', 'dummy_type': 'None', 'parent_index': 6},
            {'index': 8, 'key': '#bridge8', 'dummy_type': 'Bridge', 'parent_index': -1},
        ]
        node = {'bones': bones, 'constraints': [
            {'bone_index1': 2, 'bone_index2': 5},
            {'bone_index1': 2, 'bone_index2': 7},
            {'bone_index1': 1, 'bone_index2': 8}]}
        build = toolset_module._build_penetration_segments
        closed = build(node, ['a', 'b', 'c'], 0, 1, True)
        self.assertEqual(sum(s['category'] == 'horizontal' for s in closed), 6)
        self.assertEqual(sum(s['category'] == 'vertical' for s in closed), 3)
        self.assertFalse(any('#inter' in s['bone1'] or '#bridge' in s['bone2']
                             for s in closed))
        opened = build(node, ['a', 'b', 'c'], 1, 1, False)
        self.assertEqual(sum(s['category'] == 'horizontal' for s in opened), 2)
        self.assertEqual(sum(s['category'] == 'vertical' for s in opened), 0)
        self.assertEqual(sum(s['category'] == 'other' for s in opened), 1)
        unrestricted = build(node, [], 0, -1, True)
        self.assertTrue(any(s['bone2'] == 'a1#tip' for s in unrestricted))
        self.assertEqual(sum(s['category'] == 'other' for s in unrestricted), 2)

    def test_penetration_segments_exclude_only_two_pinned_endpoints_pure(self):
        node = {'bones': [
            {'index': 0, 'key': 'a', 'dummy_type': 'None', 'parent_index': -1,
             'skip_simulate': True},
            {'index': 1, 'key': 'b', 'dummy_type': 'None', 'parent_index': 0,
             'skip_simulate': True},
            {'index': 2, 'key': 'c', 'dummy_type': 'None', 'parent_index': 1,
             'skip_simulate': False}], 'constraints': []}
        segments = toolset_module._build_penetration_segments(node, [], 0, -1, True)
        self.assertEqual([(s['bone1'], s['bone2']) for s in segments], [('b', 'c')])
        checked, excluded = toolset_module._collision_checked_bones(node)
        self.assertEqual([bone['key'] for bone in checked], ['c'])
        self.assertEqual(excluded, 2)

    def test_runtime_info_to_dict_limit_shape(self):
        vec = lambda x, y, z: SimpleNamespace(x=x, y=y, z=z)
        quat = SimpleNamespace(x=0, y=0, z=0, w=1)
        tag = SimpleNamespace(get_editor_property=lambda name: 'KawaiiPhysics.Test')
        bone = SimpleNamespace(index=0, bone_name='a', dummy_type='None',
            parent_index=-1, location=vec(1, 2, 3), pose_location=vec(2, 3, 4),
            radius=1.0, length_rate_from_root=0.5)
        limit = SimpleNamespace(limit_type='Spherical', source_type='AnimNode',
            source_array_name='SphericalLimits', source_index=2, driving_bone='a',
            enabled=True, location=vec(0, 0, 0), rotation=quat,
            start=vec(0, 0, 0), end=vec(0, 0, 0), radius0=3.0, radius1=3.0,
            extent=vec(0, 0, 0), plane_normal=vec(0, 0, 1), inner_sphere=False)
        info = SimpleNamespace(bones=[bone], limits=[limit], constraints=[],
            anim_instance_class_name='ABP_C', node_index=0, root_bone='a', tag=tag,
            simulation_space='ComponentSpace', evaluated=True, non_uniform_scale=False,
            num_convex_limits=0, convex_fallback_shape='ConvexHull')
        result = toolset_module._runtime_info_to_dict(info, 'Mesh')
        self.assertEqual(result['bones'][0]['key'], 'a')
        self.assertFalse(result['bones'][0]['skip_simulate'])
        bone.skip_simulate = True
        self.assertTrue(toolset_module._runtime_info_to_dict(info, 'Mesh')['bones'][0]['skip_simulate'])
        self.assertEqual(result['tag'], 'KawaiiPhysics.Test')
        self.assertEqual(set(result['limits'][0]), {
            'limit_type', 'source_type', 'source_array', 'source_index', 'driving_bone',
            'enabled', 'location', 'rotation', 'start', 'end', 'radius0', 'radius1',
            'extent', 'plane_normal', 'inner_sphere'})
        self.assertEqual(result['limits'][0]['rotation'], [0, 0, 0, 1])
        keyed = [
            {'index': 0, 'bone_name': 'a', 'dummy_type': 'None', 'parent_index': -1},
            {'index': 1, 'bone_name': 'None', 'dummy_type': 'InterBone', 'parent_index': 0},
            {'index': 2, 'bone_name': 'None', 'dummy_type': 'Tip', 'parent_index': 1},
            {'index': 3, 'bone_name': 'None', 'dummy_type': 'Bridge', 'parent_index': -1},
        ]
        toolset_module._assign_runtime_bone_keys(keyed)
        self.assertEqual([item['key'] for item in keyed],
                         ['a', 'a#inter1', 'a#tip', '#bridge3'])

    def test_runtime_sample_info_reads_endpoints_and_active_shapes_only(self):
        vec = lambda x, y, z: SimpleNamespace(x=x, y=y, z=z)
        bone0 = SimpleNamespace(index=0, dummy_type='None', parent_index=-1,
                                location=vec(1, 2, 3))
        bone1 = SimpleNamespace(index=1, dummy_type='None', parent_index=0)
        active = SimpleNamespace(enabled=True, limit_type='TAPERED_CAPSULE',
            source_type='AnimNode', source_array_name='TaperedCapsuleLimits',
            source_index=0, driving_bone='a', start=vec(0, 0, -1),
            end=vec(0, 0, 1), radius0=2.0, radius1=1.0)
        disabled = SimpleNamespace(enabled=False)
        target = {'topology': ((0, 'None', -1), (1, 'None', 0)),
                  'keys_by_index': {0: 'a'}, 'endpoint_keys': {'a', 'a#tip'}}
        positions, limits = toolset_module._runtime_sample_info(
            SimpleNamespace(bones=[bone0, bone1], limits=[active, disabled]), target)
        self.assertEqual(positions, {'a': [1.0, 2.0, 3.0]})
        self.assertEqual(len(limits), 1)
        self.assertEqual(limits[0]['radius1'], 1.0)
        bone0.bone_name = 'a'
        bone1.bone_name = 'None'
        bone1.dummy_type = 'TIP'
        bone1.location = vec(4, 5, 6)
        positions, _ = toolset_module._runtime_sample_info(
            SimpleNamespace(bones=[bone0, bone1], limits=[active]), target)
        self.assertEqual(positions['a#tip'], [4.0, 5.0, 6.0])

    def test_sampler_fixed_frame_rate_save_set_and_restore_pure(self):
        class FakeEngine:
            def __init__(self):
                self.values = {'bUseFixedFrameRate': False, 'FixedFrameRate': 60.0}
                self.writes = []

            def get_editor_property(self, name):
                return self.values[name]

            def set_editor_property(self, name, value):
                self.writes.append((name, value))
                self.values[name] = value

        engine = FakeEngine()
        state = {'fixed_frame_rate': None, 'notes': []}
        toolset_module._enable_sampler_fixed_frame_rate(state, 30.0, engine)
        self.assertEqual(engine.values,
                         {'bUseFixedFrameRate': True, 'FixedFrameRate': 30.0})
        self.assertEqual(state['fixed_frame_rate'], 30.0)
        self.assertTrue(any('Game time advanced at a fixed rate' in note
                            for note in state['notes']))
        toolset_module._restore_sampler_fixed_frame_rate(state)
        self.assertEqual(engine.values,
                         {'bUseFixedFrameRate': False, 'FixedFrameRate': 60.0})
        writes_after_restore = list(engine.writes)
        toolset_module._restore_sampler_fixed_frame_rate(state)
        self.assertEqual(engine.writes, writes_after_restore)

    def test_sampler_fixed_frame_rate_disabled_pure(self):
        state = {'fixed_frame_rate': None, 'notes': []}
        toolset_module._enable_sampler_fixed_frame_rate(state, 0.0, None)
        self.assertIsNone(state['fixed_frame_rate'])
        self.assertEqual(state['notes'], [])

    def test_skirt_check_tools_unknown_actor_and_guards(self):
        self._require_world()
        toolset_module._stop_collision_penetration_sampler_impl()
        toolset_module._COLLISION_PENETRATION_SAMPLER = None
        describe = json.loads(KawaiiPhysicsToolset.describe_kawaii_physics_bones_on_actor(
            UNKNOWN_ACTOR_LABEL, False, [], '', False))
        clearance = json.loads(KawaiiPhysicsToolset.check_collision_clearance_on_actor(
            UNKNOWN_ACTOR_LABEL, False, [], '', 'both', 0.0, 20))
        start = json.loads(KawaiiPhysicsToolset.start_collision_penetration_sampler(
            UNKNOWN_ACTOR_LABEL, [], 10, False, [], '', 0, -1, True, 1.0, 0, 30.0))
        for result in (describe, clearance, start):
            self.assertEqual(result['status'], 'not_found')
            self.assertEqual(result['checked'], 0)
            self.assertIn('world', result)
        # 失敗した start の後は前回の結果ではなく、その start と同じ status を返す
        self.assertEqual(json.loads(KawaiiPhysicsToolset.get_collision_penetration_sampler_result())['status'],
                         'not_found')
        self.assertTrue(KawaiiPhysicsToolset.stop_collision_penetration_sampler())
        with self.assertToolRaisesRuntimeError():
            KawaiiPhysicsToolset.check_collision_clearance_on_actor(
                UNKNOWN_ACTOR_LABEL, False, [], '', 'invalid', 0.0, 20)
        for arguments in ((0, 0, 1.0, 0, -1, []),
                          (10, -1, 1.0, 0, -1, []),
                          (10, 0, -1.0, 0, -1, []),
                          (10, 0, 1.0, -1, -1, []),
                          (10, 0, 1.0, 2, 1, []),
                          (10, 0, 1.0, 0, -1, ['a', 'a'])):
            frames, warmup, threshold, min_depth, max_depth, roots = arguments
            with self.assertToolRaisesRuntimeError():
                KawaiiPhysicsToolset.start_collision_penetration_sampler(
                    UNKNOWN_ACTOR_LABEL, roots, frames, False, [], '',
                    min_depth, max_depth, True, threshold, warmup, 30.0)
        with self.assertToolRaisesRuntimeError():
            KawaiiPhysicsToolset.start_collision_penetration_sampler(
                UNKNOWN_ACTOR_LABEL, [], 10, False, [], '',
                0, -1, True, 1.0, 0, 14.0)

    def test_penetration_record_path_validation(self):
        with tempfile.TemporaryDirectory() as directory:
            paths = ('relative.jsonl', os.path.join(directory, 'record.txt'),
                     os.path.join(directory, 'missing', 'record.jsonl'))
            for path in paths:
                with self.subTest(path=path), self.assertToolRaisesRuntimeError():
                    KawaiiPhysicsToolset.start_collision_penetration_sampler(
                        UNKNOWN_ACTOR_LABEL, [], record_path=path)

    def test_penetration_extra_actor_labels_validation(self):
        # 文字列以外の要素は ufunction の型変換で弾かれツールまで届かないので扱わない
        for labels in (['Baseline', 'Baseline'], [UNKNOWN_ACTOR_LABEL],
                       [''], ['Baseline', '']):
            with self.subTest(labels=labels), self.assertToolRaisesRuntimeError():
                KawaiiPhysicsToolset.start_collision_penetration_sampler(
                    UNKNOWN_ACTOR_LABEL, [], extra_actor_labels=labels)

    def test_penetration_record_extra_bones_arguments(self):
        class ArrayLike:
            def __iter__(self):
                return iter(('leg_l', 'leg_r'))

        convert = toolset_module._record_extra_bone_names
        self.assertEqual(convert(('leg_l', 'leg_r')), ['leg_l', 'leg_r'])
        self.assertEqual(convert(ArrayLike()), ['leg_l', 'leg_r'])
        for value in (['leg_l', 2], [''], 'leg_l', 2):
            with self.subTest(value=value), self.assertRaises(ValueError):
                convert(value)

    def test_skirt_runtime_getter_smoke(self):
        self._require_world()
        mesh = unreal.EditorAssetLibrary.load_asset(SKIRT_MESH_PATH)
        blueprint = unreal.EditorAssetLibrary.load_asset(
            '/Game/KawaiiPhysicsSample/Examples/05_Advanced/ABP_5_1_BoneConstraint_On')
        if mesh is None or blueprint is None:
            self.skipTest('Skirt mesh or AnimBlueprint is unavailable.')
        actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        actor = actor_subsystem.spawn_actor_from_class(
            unreal.SkeletalMeshActor, unreal.Vector(5100.0, 0.0, 0.0))
        if actor is None:
            self.skipTest('Unable to spawn a SkeletalMeshActor in the editor world.')
        try:
            actor.set_actor_label('KP_PyToolsetSkirtRuntimeTest')
            component = actor.get_editor_property('skeletal_mesh_component')
            if hasattr(component, 'set_skeletal_mesh_asset'):
                component.set_skeletal_mesh_asset(mesh)
            else:
                component.set_skeletal_mesh(mesh)
            component.set_anim_instance_class(blueprint.generated_class())
            result = json.loads(KawaiiPhysicsToolset.describe_kawaii_physics_bones_on_actor(
                'KP_PyToolsetSkirtRuntimeTest', False, [], '', True))
            nodes = [node for entry in result['components'] for node in entry['nodes']]
            self.assertGreaterEqual(len(nodes), 1)
            self.assertTrue(any(node['root_bone'] == 'skirt_01_01_l' for node in nodes))
            self.assertIn(result['status'], ('ok', 'not_evaluated'))
        finally:
            actor_subsystem.destroy_actor(actor)

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

    def test_set_graph_node_properties_reapply_same_values_does_not_raise(self):
        handle = self._place_test_node()
        values = '{"PhysicsSettings": "(Damping=0.3)", "DummyBoneLength": 2.1234567}'
        KawaiiPhysicsToolset.set_graph_node_properties(handle, values)
        result = KawaiiPhysicsToolset.set_graph_node_properties(handle, values)
        self.assertEqual(list(result), ['PhysicsSettings', 'DummyBoneLength'])

    def test_graph_node_settings_diff_and_restore(self):
        anim_blueprint = self._create_anim_blueprint()
        handle = self._place_test_node(anim_blueprint, _make_request('TwintailA_L'))
        snapshot = KawaiiPhysicsToolset.describe_graph_node_settings(anim_blueprint)

        own_diff = json.loads(KawaiiPhysicsToolset.diff_graph_node_settings(
            anim_blueprint, snapshot))
        self.assertEqual(own_diff['status'], 'ok')
        self.assertEqual(own_diff['changed'], [])
        self.assertEqual(own_diff['missing_in_reference'], [])

        KawaiiPhysicsToolset.set_graph_node_properties(
            handle, json.dumps({'WindScale': 2.5}))
        changed = json.loads(KawaiiPhysicsToolset.diff_graph_node_settings(
            anim_blueprint, snapshot))
        self.assertEqual(changed['status'], 'differs')
        self.assertEqual([item['name'] for item in changed['changed']], ['WindScale'])

        before_dry_run = KawaiiPhysicsToolset.get_graph_node_property(handle, 'WindScale')
        dry_run = json.loads(KawaiiPhysicsToolset.apply_graph_node_settings(
            anim_blueprint, snapshot, dry_run=True))
        self.assertEqual(dry_run['status'], 'dry_run')
        self.assertEqual(dry_run['written'], [])
        self.assertIn('WindScale', dry_run['would_write'])
        self.assertEqual(KawaiiPhysicsToolset.get_graph_node_property(
            handle, 'WindScale'), before_dry_run)

        restored = json.loads(KawaiiPhysicsToolset.apply_graph_node_settings(
            anim_blueprint, snapshot))
        self.assertEqual(restored['status'], 'ok')
        self.assertEqual(restored['written'], ['WindScale'])
        self.assertEqual(json.loads(KawaiiPhysicsToolset.diff_graph_node_settings(
            anim_blueprint, snapshot))['status'], 'ok')

    def test_graph_node_settings_snapshot_selects_same_index(self):
        anim_blueprint = self._create_anim_blueprint()
        self._place_test_node(anim_blueprint, _make_request('TwintailA_L'))
        second = self._place_test_node(anim_blueprint, _make_request('TwintailB_L'))
        snapshot = KawaiiPhysicsToolset.describe_graph_node_settings(anim_blueprint)
        KawaiiPhysicsToolset.set_graph_node_properties(
            second, json.dumps({'WindScale': 3.0}))

        changed = json.loads(KawaiiPhysicsToolset.diff_graph_node_settings(
            anim_blueprint, snapshot, 1))
        self.assertEqual([item['name'] for item in changed['changed']], ['WindScale'])
        restored = json.loads(KawaiiPhysicsToolset.apply_graph_node_settings(
            anim_blueprint, snapshot, 1))
        self.assertEqual(restored['written'], ['WindScale'])
        self.assertEqual(json.loads(KawaiiPhysicsToolset.diff_graph_node_settings(
            anim_blueprint, snapshot, 1))['status'], 'ok')

    def test_graph_node_settings_restores_external_forces(self):
        anim_blueprint = self._create_anim_blueprint()
        handle = self._place_test_node(anim_blueprint, _make_request('TwintailA_L'))
        snapshot = KawaiiPhysicsToolset.describe_graph_node_settings(anim_blueprint)
        KawaiiPhysicsToolset.set_graph_node_external_forces(
            handle, json.dumps([{'_structType': BASIC_EXTERNAL_FORCE_STRUCT}]))

        changed = json.loads(KawaiiPhysicsToolset.diff_graph_node_settings(
            anim_blueprint, snapshot))
        self.assertIn('external_forces', [item['name'] for item in changed['changed']])
        restored = json.loads(KawaiiPhysicsToolset.apply_graph_node_settings(
            anim_blueprint, snapshot))
        self.assertEqual(restored['status'], 'ok')
        self.assertEqual(restored['written'], ['external_forces'])
        self.assertEqual(json.loads(KawaiiPhysicsToolset.diff_graph_node_settings(
            anim_blueprint, snapshot))['status'], 'ok')

    def test_import_text_normalization_preserves_quoted_text(self):
        normalize = toolset_module._normalize_import_text
        self.assertEqual(normalize('(Radius=1.0, Damping=1.000000)'),
                         normalize('( Radius = 1.000000 ,Damping=1 )'))
        self.assertNotEqual(normalize('(BoneName="A B")'),
                            normalize('(BoneName="AB")'))

    def test_import_bone_names_handles_special_names_pure(self):
        cases = [
            ('(BoneName="Bip001-Skirt01")', ['Bip001-Skirt01']),
            ('(BoneName="skirt.01 L")', ['skirt.01 L']),
            ('(BoneName="スカート_0")', ['スカート_0']),
            ('(BoneName=None)', ['None']),
            ('((BoneName="a\\"b"),(BoneName="c"))', ['a"b', 'c']),
            ('', []),
        ]
        for source, expected in cases:
            with self.subTest(source=source):
                self.assertEqual(toolset_module._import_bone_names(source), expected)

    def test_import_text_satisfies_partial_struct_and_rounding_pure(self):
        check = toolset_module._import_text_satisfies
        cases = [
            ('(Damping=0.300000,Stiffness=0.050000)', '(Damping=0.3)', True),
            ('(Damping=0.300000,Stiffness=0.050000)', '(Damping=0.31)', False),
            ('2.123457', '2.1234567', True),
            ('X_Positive', 'EBoneForwardAxis::X_Positive', True),
            ('((A=1),(A=2))', '((A=1),(A=2))', True),
            ('((A=1),(A=2))', '((A=1))', False),
            ('(Bone=(BoneName="Leg"))', '(Bone=(BoneName="Leg"))', True),
            ('(Bone=(BoneName="Leg"))', '(Bone=(BoneName="Arm"))', False),
        ]
        for actual, requested, expected in cases:
            with self.subTest(actual=actual, requested=requested):
                self.assertEqual(check(actual, requested), expected)
        self.assertTrue(check('(Bone=(BoneName="Leg"),PreviewBone=(BoneName="A"))',
                              '(Bone=(BoneName="Leg"),PreviewBone=(BoneName="B"))',
                              'SyncBones'))

    def test_normalize_import_text_treats_negative_zero_as_zero_pure(self):
        normalize = toolset_module._normalize_import_text
        self.assertEqual(normalize('(X=-0.000000)'), normalize('(X=0)'))

    def test_sync_bones_import_text_ignores_runtime_and_preview_fields(self):
        normalize = toolset_module._normalize_import_text
        current = (
            '((Bone=(BoneName="Leg"),GlobalScale=(X=1,Y=1,Z=1),'
            'TargetRoots=((Bone=(BoneName="Skirt"),bIncludeChildBones=True,'
            'ChildTargets=((ModifyBoneIndex=3,PreviewBone=(BoneName="Tip"))),'
            'IsShowPreviewBone=True,PreviewBone=(BoneName="Root"),'
            'TranslationBySyncBone=(X=1,Y=2,Z=3),ScaleByLengthRateCurve=0.5,'
            'LengthRateFromSyncTargetRoot=0.2,ModifyBoneIndex=2)),'
            'InitialPoseLocation=(X=1,Y=2,Z=3),DeltaDistance=(X=1,Y=0,Z=0),'
            'ScaledDeltaDistance=(X=2,Y=0,Z=0)))'
        )
        reference = (
            '((Bone=(BoneName="Leg"),GlobalScale=(X=1,Y=1,Z=1),'
            'TargetRoots=((Bone=(BoneName="Skirt"),bIncludeChildBones=True,'
            'ChildTargets=(),IsShowPreviewBone=False,PreviewBone=(BoneName="Other"),'
            'TranslationBySyncBone=(X=0,Y=0,Z=0),ScaleByLengthRateCurve=1,'
            'LengthRateFromSyncTargetRoot=0.8,ModifyBoneIndex=9)),'
            'InitialPoseLocation=(X=0,Y=0,Z=0),DeltaDistance=(X=0,Y=0,Z=0),'
            'ScaledDeltaDistance=(X=0,Y=0,Z=0)))'
        )
        self.assertEqual(normalize(current, 'SyncBones'),
                         normalize(reference, 'SyncBones'))
        self.assertNotEqual(normalize(current), normalize(reference))
        self.assertNotEqual(normalize(current, 'SyncBones'),
                            normalize(reference.replace('GlobalScale=(X=1',
                                                        'GlobalScale=(X=0'), 'SyncBones'))

    def test_sampler_world_time_uses_gameplay_statics(self):
        world = object()
        get_time = mock.Mock(return_value=12.5)
        gameplay_statics = SimpleNamespace(get_time_seconds=get_time)
        with mock.patch.object(toolset_module, '_resolve_world', return_value=world), \
                mock.patch.object(toolset_module, 'unreal',
                                  SimpleNamespace(GameplayStatics=gameplay_statics)):
            self.assertEqual(toolset_module._sampler_world_time_seconds(True), 12.5)
        get_time.assert_called_once_with(world)

    def test_compact_runtime_bone_summary(self):
        node = {
            'component': 'Mesh', 'anim_instance_class': 'AnimClass',
            'node_index': 1, 'root_bone': 'Root', 'tag': '', 'evaluated': True,
            'bones': [{'dummy_type': kind} for kind in
                      ('None', 'Tip', 'InterBone', 'Bridge', 'None')],
            'limits': [{'limit_type': kind} for kind in
                       ('Spherical', 'Capsule', 'Spherical')],
            'constraints': [{}, {}],
        }
        summary = toolset_module._compact_runtime_bone_summary(node)
        self.assertEqual(summary['total_bones'], 5)
        self.assertEqual(summary['bones_by_dummy_type'],
                         {'None': 2, 'Tip': 1, 'InterBone': 1, 'Bridge': 1})
        self.assertEqual(summary['constraint_count'], 2)
        self.assertEqual(summary['limits_by_type'], {'Spherical': 2, 'Capsule': 1})
        self.assertEqual(summary['node']['root_bone'], 'Root')
        for name in ('bones', 'limits', 'constraints'):
            self.assertNotIn(name, summary)

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
                first_default = len(args) - len(node.args.defaults)
                expected_required[node.name] = [
                    arg.arg for index, arg in enumerate(args)
                    if index < first_default or _is_list_annotation(arg.annotation)
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
