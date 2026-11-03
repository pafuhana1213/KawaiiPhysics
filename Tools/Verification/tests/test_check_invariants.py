"""Tests for the invariant checks, run with: python -m unittest discover Tools/Verification/tests"""

import math
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

import check_invariants  # noqa: E402

IDENTITY = [0.0, 0.0, 0.0, 1.0]


def make_cell(frame_positions, bone_names=('root', 'tip')):
    """Build a two-bone cell: root stays put and tip is simulated at the given positions."""
    transforms = []
    for position in frame_positions:
        transforms += [0.0, 0.0, 0.0, *IDENTITY, *position, *IDENTITY]
    return {'bones': [['root', -1], ['tip', 0]],
            'ref_pose': [0.0, 0.0, 0.0, *IDENTITY, 0.0, 0.0, -10.0, *IDENTITY],
            'nodes': [{'bone_names': list(bone_names)}],
            'transforms': transforms}


class QuaternionAngleTest(unittest.TestCase):
    def test_same_rotation_measures_zero_even_when_not_normalized(self):
        rotation = [0.0, -0.70710678118, 0.0, -0.70710678118]
        self.assertLess(check_invariants._angle_between(rotation, rotation), 1e-9)
        scaled = [value * 1.0000001 for value in rotation]
        self.assertLess(check_invariants._angle_between(rotation, scaled), 1e-6)

    def test_known_angles(self):
        half = math.radians(10.0) / 2
        rotated = [0.0, 0.0, math.sin(half), math.cos(half)]
        self.assertAlmostEqual(check_invariants._angle_between(IDENTITY, rotated), 10.0, places=6)
        flipped = [-value for value in rotated]
        self.assertAlmostEqual(check_invariants._angle_between(IDENTITY, flipped), 10.0, places=6)


class CheckCellTest(unittest.TestCase):
    def test_swinging_bone_passes(self):
        result = check_invariants.check_cell(make_cell(
            [[0.0, 0.0, -10.0], [1.0, 0.0, -9.95], [2.0, 0.0, -9.8]]))
        self.assertEqual(result['violations'], [])
        self.assertFalse(result['still'])

    def test_motionless_bone_is_still_but_not_a_violation(self):
        result = check_invariants.check_cell(make_cell([[0.0, 0.0, -10.0]] * 3))
        self.assertEqual(result['violations'], [])
        self.assertTrue(result['still'])

    def test_nonfinite_value_is_reported(self):
        cell = make_cell([[0.0, 0.0, -10.0], [0.0, 0.0, -10.0]])
        cell['transforms'][7 * 3] = None
        result = check_invariants.check_cell(cell)
        self.assertEqual(result['nonfinite_values'], 1)
        self.assertIn('nonfinite', result['violations'])

    def test_stretch_is_reported(self):
        result = check_invariants.check_cell(make_cell(
            [[0.0, 0.0, -10.0], [0.0, 0.0, -20.0]]))
        self.assertIn('stretched', result['violations'])
        self.assertEqual(result['worst']['stretch'], {'bone': 'tip', 'frame': 1})

    def test_node_root_may_follow_the_pose_away_from_its_parent(self):
        result = check_invariants.check_cell(make_cell(
            [[0.0, 0.0, -10.0], [0.0, 0.0, -20.0]], bone_names=('tip',)))
        self.assertNotIn('stretched', result['violations'])
        self.assertIsNone(result['max_stretch_ratio'])

    def test_compression_is_reported(self):
        result = check_invariants.check_cell(make_cell(
            [[0.0, 0.0, -10.0], [0.0, 0.0, -2.0]]))
        self.assertIn('compressed', result['violations'])

    def test_jump_is_reported(self):
        result = check_invariants.check_cell(make_cell(
            [[0.0, 0.0, -10.0], [0.0, 0.0, -10.0], [500.0, 0.0, -10.0]]),
            {'max_stretch_ratio': 1000.0})
        self.assertIn('jump', result['violations'])

    def test_explosion_is_reported(self):
        positions = [[index * 90.0, 0.0, -10.0] for index in range(30)]
        result = check_invariants.check_cell(make_cell(positions),
                                             {'max_stretch_ratio': 1000.0})
        self.assertIn('exploded', result['violations'])
        self.assertNotIn('jump', result['violations'])

    def test_broken_quaternion_is_reported(self):
        cell = make_cell([[0.0, 0.0, -10.0]])
        cell['transforms'][7 + 6] = 5.0
        self.assertIn('bad_quaternion', check_invariants.check_cell(cell)['violations'])

    def test_requested_but_empty_node_records_are_a_violation(self):
        cell = make_cell([[0.0, 0.0, -10.0], [1.0, 0.0, -9.95]])
        cell['node_records'] = []
        self.assertIn('no_node_records', check_invariants.check_cell(cell)['violations'])

    def test_empty_cells_are_violations(self):
        cell = make_cell([])
        self.assertEqual(check_invariants.check_cell(cell)['violations'], ['no_samples'])
        cell = make_cell([[0.0, 0.0, -10.0]], bone_names=())
        self.assertEqual(check_invariants.check_cell(cell)['violations'], ['no_simulated_bones'])


class NodeRecordTest(unittest.TestCase):
    def _record(self, point, kind, *, inner=0, enabled=1, dummy='None'):
        cell = make_cell([[0, 0, -10]])
        limit = [kind, enabled, inner, 0, 0, 0, 0, 0, 0, 1,
                 0, 0, -2, 0, 0, 2, 3, 1, 3, 3, 3, 0, 0, 1]
        cell['node_records'] = [{'root': 'root',
                                 'bones': [{'name': 'tip', 'dummy': dummy, 'parent': -1,
                                            'skip': False, 'angle_parent': -1,
                                            'angle_mode': 'Cone'}],
                                 'bone_frames': [*point, *point, 1, 0],
                                 'limit_frames': [limit]}]
        return cell

    def test_all_limit_shapes(self):
        cases = [(1, [0, 0, 0], 4), (2, [0, 0, 0], 4),
                 (5, [0, 0, -2], 4), (3, [0, 0, 0], 4),
                 (4, [0, 0, -2], 3)]
        for kind, point, expected in cases:
            with self.subTest(kind=kind):
                result = check_invariants.check_cell(self._record(point, kind),
                                                     {'max_penetration_cm': 0.5})
                self.assertAlmostEqual(result['max_penetration_cm'], expected, places=4)
                self.assertIn('penetrating', result['violations'])
        inner = check_invariants.check_cell(self._record([5, 0, 0], 1, inner=1))
        self.assertAlmostEqual(inner['max_penetration_cm'], 3)

    def test_disabled_and_dummy_are_ignored(self):
        for options in ({'enabled': 0}, {'dummy': 'Bridge'}, {'dummy': 'Tip'}):
            result = check_invariants.check_cell(self._record([0, 0, 0], 1, **options))
            self.assertEqual(result['max_penetration_cm'], 0)

    def test_limit_angle_and_exclusions(self):
        cell = make_cell([[0, 0, -10]])
        bones = [{'name': 'root', 'dummy': 'None', 'parent': -1, 'skip': False,
                  'angle_parent': -1, 'angle_mode': 'Cone'},
                 {'name': 'tip', 'dummy': 'None', 'parent': 0, 'skip': False,
                  'angle_parent': 0, 'angle_mode': 'Cone'}]
        cell['node_records'] = [{'root': 'root', 'bones': bones,
                                 'bone_frames': [0, 0, 0, 0, 0, 0, 1, 0,
                                                 1, 0, 0, 0, 1, 0, 1, 30],
                                 'limit_frames': [[]]}]
        result = check_invariants.check_cell(cell)
        self.assertAlmostEqual(result['max_limit_angle_excess_degrees'], 60)
        self.assertIn('limit_angle_exceeded', result['violations'])
        cell['node_records'][0]['bone_frames'][-1] = 0
        self.assertEqual(check_invariants.check_cell(cell)[
            'max_limit_angle_excess_degrees'], 0)
        cell['node_records'][0]['bone_frames'][-1] = 30
        bones[1]['angle_parent'] = -1
        self.assertEqual(check_invariants.check_cell(cell)[
            'max_limit_angle_excess_degrees'], 0)


if __name__ == '__main__':
    unittest.main()
