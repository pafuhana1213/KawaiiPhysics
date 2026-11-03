"""Synthetic matrix judgments and known issue handling."""

import copy
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import judge_matrix  # noqa: E402
from test_check_invariants import make_cell  # noqa: E402


def fixture(moving=True):
    positions = [[0, 0, -10], [1, 0, -9.95], [2, 0, -9.8]] if moving else [[0, 0, -10]] * 3
    cell = make_cell(positions)
    cell['nodes'][0].update({'space': 'EKawaiiPhysicsSimulationSpace::BaseBoneSpace',
                             'base': 'root'})
    return cell


def setup(kind='equal', moving=True):
    cells = {'a': fixture(moving), 'b': fixture(moving)}
    recording = {'run': {'version': 2, 'settings': {}, 'delta_seconds': [1 / 60] * 3},
                 'cells': cells}
    expanded = {'cells': [{'id': 'a', 'overrides': {}}, {'id': 'b', 'overrides': {}}],
                'tags': {'a': {'feature': 'off'}, 'b': {'feature': 'on'}},
                'profiles': {'base': {'fps': 60}},
                'relations': [{'id': 'pair', 'kind': kind, 'a': 'a', 'b': 'b',
                               'profile': 'base', 'reason': 'test',
                               'max_cm': 0.01, 'max_degrees': 0.01,
                               'min_cm': 1, 'min_motion_cm': 0.1}]}
    return expanded, {'base': recording}


class JudgeTest(unittest.TestCase):
    def test_equal_and_not_equal(self):
        expanded, recordings = setup()
        self.assertTrue(judge_matrix.judge(expanded, recordings)['relations'][0]['ok'])
        recordings['base']['cells']['b']['transforms'][21] += 3
        self.assertEqual(judge_matrix.judge(expanded, recordings)['relations'][0]['failure'],
                         'not_equal')

    def test_vacuous_and_not_sensitive(self):
        expanded, recordings = setup(moving=False)
        self.assertEqual(judge_matrix.judge(expanded, recordings)['relations'][0]['failure'],
                         'vacuous')
        expanded['relations'][0]['kind'] = 'differ'
        self.assertEqual(judge_matrix.judge(expanded, recordings)['relations'][0]['failure'],
                         'not_sensitive')

    def test_relation_empty_and_missing(self):
        expanded, recordings = setup()
        expanded['relations'][0]['a'] = None
        self.assertEqual(judge_matrix.judge(expanded, recordings)['relations'][0]['failure'],
                         'relation_empty')
        expanded['relations'][0]['a'] = 'absent'
        self.assertEqual(judge_matrix.judge(expanded, recordings)['relations'][0]['failure'],
                         'cell_missing')

    def test_override_and_profile(self):
        expanded, recordings = setup()
        expanded['cells'][0]['overrides'] = {'SimulationSpace': 'WorldSpace',
                                              'SimulationBaseBone': 'wrong'}
        expanded['profiles']['base']['settings'] = {'fixed_substepping': False}
        expanded['profiles']['base']['cvars'] = ['a.ParallelAnimEvaluation=0']
        judged = judge_matrix.judge(expanded, recordings)
        self.assertIn('override_not_applied', judged['cells']['base']['a']['violations'])
        self.assertEqual(judged['run_violations'][0]['violation'], 'profile_not_applied')

    def test_differ_accepts_a_rotation_only_change(self):
        expanded, recordings = setup(kind='differ')
        recordings['base']['cells']['b']['transforms'][7 + 3:7 + 7] = [0.0, 0.0, 1.0, 0.0]
        expanded['relations'][0]['min_degrees'] = 90
        self.assertTrue(judge_matrix.judge(expanded, recordings)['relations'][0]['ok'])

    def test_stride_compares_the_same_simulation_time(self):
        expanded, recordings = setup()
        positions = [[0, 0, -10], [1, 0, -9.95], [2, 0, -9.8], [3, 0, -9.5]]
        recordings['base']['cells']['a'] = fixture()
        recordings['base']['cells']['a']['transforms'] = make_cell(positions)['transforms']
        recordings['base']['cells']['b'] = fixture()
        recordings['base']['cells']['b']['transforms'] = make_cell(positions[1::2])['transforms']
        expanded['relations'][0].update({'a_stride': 2, 'a_offset': 1})
        self.assertTrue(judge_matrix.judge(expanded, recordings)['relations'][0]['ok'])

    def test_until_frame_ends_the_compared_window(self):
        expanded, recordings = setup()
        recordings['base']['cells']['b']['transforms'][2 * 14 + 7] += 3
        self.assertEqual(judge_matrix.judge(expanded, recordings)['relations'][0]['failure'],
                         'not_equal')
        expanded['relations'][0]['until_frame'] = 2
        self.assertTrue(judge_matrix.judge(expanded, recordings)['relations'][0]['ok'])
        expanded['relations'][0]['skip_frames'] = 2
        self.assertEqual(judge_matrix.judge(expanded, recordings)['relations'][0]['metrics'],
                         {'error': 'The frame window is empty.'})

    def test_base_bone_override_is_read_from_the_bone_reference_text(self):
        expanded, recordings = setup()
        expanded['cells'][0]['overrides'] = {'SimulationSpace': 'BaseBoneSpace',
                                              'SimulationBaseBone': '(BoneName="root")'}
        expanded['cells'][1]['overrides'] = {'SimulationBaseBone': '(BoneName="other")'}
        cells = judge_matrix.judge(expanded, recordings)['cells']['base']
        self.assertNotIn('override_not_applied', cells['a']['violations'])
        self.assertIn('override_not_applied', cells['b']['violations'])

    def test_motionless_equal_is_vacuous_without_an_explicit_minimum(self):
        expanded, recordings = setup(moving=False)
        del expanded['relations'][0]['min_motion_cm']
        self.assertEqual(judge_matrix.judge(expanded, recordings)['relations'][0]['failure'],
                         'vacuous')

    def test_penetration_reduced(self):
        expanded, recordings = setup(kind='penetration_reduced')
        for name, point in (('a', [0, 0, 0]), ('b', [10, 0, 0])):
            recordings['base']['cells'][name]['node_records'] = [{
                'root': 'root',
                'bones': [{'name': 'tip', 'dummy': 'None', 'skip': False,
                           'angle_parent': -1, 'angle_mode': 'Cone', 'parent': -1}],
                'bone_frames': [*point, *point, 1, 0],
                'limit_frames': [[1, 1, 0, 0, 0, 0, 0, 0, 0, 1,
                                  0, 0, 0, 0, 0, 0, 3, 3, 0, 0, 0, 0, 0, 1]]}]
        expanded['relations'][0]['min_reduction_cm'] = 2
        result = judge_matrix.judge(expanded, recordings)['relations'][0]
        self.assertTrue(result['ok'])
        self.assertAlmostEqual(result['metrics']['reduction_cm'], 4)

    def test_limit_angle_reduced_measures_the_free_cell_against_the_limit(self):
        expanded, recordings = setup(kind='limit_angle_reduced')
        bones = [{'name': 'root', 'dummy': 'None', 'skip': True, 'parent': -1,
                  'angle_parent': -1, 'angle_mode': 'Cone'},
                 {'name': 'tip', 'dummy': 'None', 'skip': False, 'parent': 0,
                  'angle_parent': 0, 'angle_mode': 'Cone'}]
        root = [0, 0, 0, 0, 0, 0, 1, 0]
        for name, tip, limit in (('a', [10, 0, 0], 0), ('b', [10, 0, 10], 45)):
            recordings['base']['cells'][name]['node_records'] = [{
                'root': 'root', 'bones': bones, 'limit_frames': [[]],
                'bone_frames': [*root, *tip, 0, 0, 10, 1, limit]}]
        expanded['relations'][0]['min_reduction_degrees'] = 30
        result = judge_matrix.judge(expanded, recordings)['relations'][0]
        self.assertTrue(result['ok'])
        self.assertAlmostEqual(result['metrics']['excess_a_degrees'], 45)
        self.assertAlmostEqual(result['metrics']['excess_b_degrees'], 0)

    def test_override_checks_the_value_set_by_the_last_event(self):
        expanded, recordings = setup()
        expanded['cells'][0]['overrides'] = {'SimulationSpace': 'ComponentSpace'}
        expanded['cells'][0]['events'] = [{'frame': 1,
                                           'overrides': {'SimulationSpace': 'BaseBoneSpace'}}]
        cells = judge_matrix.judge(expanded, recordings)['cells']['base']
        self.assertNotIn('override_not_applied', cells['a']['violations'])

    def test_known_issues_and_bias(self):
        expanded, recordings = setup(moving=False)
        judged = judge_matrix.judge(expanded, recordings)
        stale = judge_matrix.apply_known_issues(judged, [
            {'check': 'cell:a:still', 'reason': 'expected'},
            {'check': 'relation:pair', 'reason': 'expected'},
            {'check': 'cell:missing:*', 'reason': 'stale'},
            {'anim_class': 'X', 'violation': 'still'}])
        self.assertEqual(len(stale), 1)
        self.assertNotIn('still', judged['cells']['base']['a']['violations'])
        self.assertTrue(judged['relations'][0]['ok'])
        report = judge_matrix.axis_report(judged, expanded['tags'])
        self.assertTrue(report['feature']['on']['biased'])


if __name__ == '__main__':
    unittest.main()
