"""Tests for the invariant checks, run with: python -m unittest discover Tools/Verification/tests"""

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

    def test_empty_cells_are_violations(self):
        cell = make_cell([])
        self.assertEqual(check_invariants.check_cell(cell)['violations'], ['no_samples'])
        cell = make_cell([[0.0, 0.0, -10.0]], bone_names=())
        self.assertEqual(check_invariants.check_cell(cell)['violations'], ['no_simulated_bones'])


if __name__ == '__main__':
    unittest.main()
