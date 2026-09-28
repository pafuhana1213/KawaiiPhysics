"""Pure Python checks for trajectory analysis."""

import math
import unittest

from kawaii_physics_toolset.motion_metrics import (compute_motion_metrics,
                                                    compare_motion_metrics)


def recording(positions, poses=None, angles=None, second=None, extra=None):
    poses = poses or [0.0] * len(positions)
    angles = angles or [0.0] * len(positions)
    bones = [{'key': 'root', 'bone_name': 'root', 'dummy_type': 'None',
              'parent_key': None, 'depth': 0},
             {'key': 'tip', 'bone_name': 'tip', 'dummy_type': 'None',
              'parent_key': 'root', 'depth': 1}]
    header = {'type': 'header', 'version': 1, 'fixed_frame_rate': 30.0,
              'extra_bones': ['leg'] if extra else [],
              'nodes': [{'node': {'root_bone': 'root'}, 'columns': [['root', 'tip']],
                         'bones': bones, 'constraints': [['root', 'tip']],
                         'rest': {'root': [0, 0, 0], 'tip': [10, 0, 0]}}]}
    frames = []
    for index, position in enumerate(positions):
        angle = math.radians(angles[index]) / 2
        tip = second[index] if second else position + 10
        values = {'root': [position, 0, 0, poses[index], 0, 0],
                  'tip': [tip, 0, 0, poses[index] + 10, 0, 0]}
        leg = extra[index] if extra else 0
        frames.append({'type': 'frame', 'frame': index,
                       'nodes': [{'bones': values,
                                  'final': {'root': [position, 0, 0, 0, 0,
                                                     math.sin(angle), math.cos(angle)],
                                            'tip': [tip, 0, 0, 0, 0, 0, 1]}}],
                       'extra': {'leg': [leg, 0, 0, 0, 0, 0, 1]} if extra else {}})
    return {'header': header, 'frames': frames}


class MotionMetricsTests(unittest.TestCase):
    def test_smooth_swing_and_noise(self):
        smooth = [2 * math.sin(index * 0.15) for index in range(80)]
        noisy = [value + (0.5 if index % 2 else -0.5)
                 for index, value in enumerate(smooth)]
        clean = compute_motion_metrics(recording(smooth))['overall']
        noisy_result = compute_motion_metrics(recording(noisy))['overall']
        self.assertEqual(clean['angular']['spikes'], 0)
        self.assertLess(clean['jitter']['mean'], 0.02)
        self.assertGreater(noisy_result['jitter']['mean'], clean['jitter']['mean'] * 10)

    def test_quaternion_sign_and_real_jump(self):
        sample = recording([0.0] * 6)
        sample['frames'][3]['nodes'][0]['final']['root'][3:7] = [0, 0, 0, -1]
        self.assertEqual(compute_motion_metrics(sample)['overall']['angular']['spikes'], 0)
        self.assertEqual(compute_motion_metrics(sample)['overall']['angular']['flips'], 0)
        sample = recording([0.0] * 6, angles=[0, 0, 0, 90, 90, 90])
        self.assertEqual(compute_motion_metrics(sample)['overall']['angular']['spikes'], 1)
        self.assertEqual(compute_motion_metrics(sample)['overall']['angular']['flips'], 0)

    def test_stretch_and_stiffness(self):
        stretched = compute_motion_metrics(recording([0.0] * 8,
                                                     second=[15.0] * 8))['overall']
        self.assertAlmostEqual(stretched['stretch']['constraint']['mean_abs'], 0.5)
        self.assertAlmostEqual(stretched['stretch']['vertical']['p95'], 0.5)
        baseline = compute_motion_metrics(recording([5.0] * 8))
        stiff = compute_motion_metrics(recording([1.0] * 8))
        self.assertTrue(compare_motion_metrics(stiff, baseline)['flags']['stiffer'])

    def test_rest_spacing_ignores_moving_pose(self):
        sample = recording([0, 0, 0], poses=[0, 10, 20], second=[10, 15, 20])
        for frame in sample['frames']:
            frame['nodes'][0]['bones']['tip'][3] = frame['nodes'][0]['bones']['root'][3] + 30
        result = compute_motion_metrics(sample)['overall']['stretch']['vertical']
        self.assertAlmostEqual(result['max'], 1.0)
        self.assertAlmostEqual(result['min'], 0.0)

    def test_ring_neighbors_without_constraints(self):
        sample = recording([0] * 3)
        node = sample['header']['nodes'][0]
        node['constraints'] = []
        node['columns'] = [['root', 'tip'], ['other_root', 'other_tip']]
        node['bones'] += [
            {'key': 'other_root', 'bone_name': 'other_root', 'dummy_type': 'None',
             'parent_key': None, 'depth': 0},
            {'key': 'other_tip', 'bone_name': 'other_tip', 'dummy_type': 'None',
             'parent_key': 'other_root', 'depth': 1}]
        node['rest'].update(other_root=[0, 10, 0], other_tip=[10, 10, 0])
        for frame in sample['frames']:
            frame['nodes'][0]['bones'].update(other_root=[0, 10, 0, 0, 10, 0],
                                               other_tip=[10, 15, 0, 10, 10, 0])
        horizontal = compute_motion_metrics(sample)['overall']['stretch']['horizontal']
        self.assertGreater(horizontal['p95'], 0)
        self.assertIsNone(compute_motion_metrics(sample)['overall']['stretch']['constraint']['p95'])

    def test_collapse_fraction_for_stuck_segment(self):
        sample = recording([0] * 4, second=[10, 10 / 3, 10 / 3, 10 / 3])
        result = compute_motion_metrics(sample)['overall']
        self.assertAlmostEqual(result['collapse_fraction'], 0.75)
        self.assertAlmostEqual(result['rows']['1']['collapse_fraction'], 0.75)

    def test_collapse_fraction_includes_tip_dummy_segment(self):
        sample = recording([0] * 4)
        node = sample['header']['nodes'][0]
        node['columns'] = [['root', 'row05', 'tip']]
        node['bones'].insert(1, {'key': 'row05', 'bone_name': 'row05',
                                 'dummy_type': 'None', 'parent_key': 'root',
                                 'depth': 1})
        node['bones'][-1]['dummy_type'] = 'Tip'
        node['bones'][-1]['parent_key'] = 'row05'
        node['bones'][-1]['depth'] = 2
        node['rest']['row05'] = [10, 0, 0]
        node['rest']['tip'] = [20, 0, 0]
        for index, frame in enumerate(sample['frames']):
            bones = frame['nodes'][0]['bones']
            bones['row05'] = [10, 0, 0, 10, 0, 0]
            bones['tip'] = [10 if index == 3 else 20, 0, 0, 20, 0, 0]
        result = compute_motion_metrics(sample)['overall']
        self.assertAlmostEqual(result['collapse_fraction'], 1 / 8)
        self.assertEqual(result['rows']['1']['collapse_fraction'], 0)
        self.assertAlmostEqual(result['rows']['2']['collapse_fraction'], 1 / 4)

    def test_flip_count_and_worst_frame(self):
        sample = recording([0] * 6, angles=[0, 30, 60, 90, 120, 290])
        angular = compute_motion_metrics(sample)['overall']['angular']
        self.assertEqual(angular['spikes'], 5)
        self.assertEqual(angular['flips'], 1)
        self.assertEqual(angular['worst_flip']['frame'], 5)
        self.assertAlmostEqual(angular['worst_flip']['degrees'], 170)

    def test_lift_p50_of_frame_maxima(self):
        sample = recording([0] * 5)
        for frame, z in zip(sample['frames'], [0, 1, 2, 3, 40]):
            frame['nodes'][0]['bones']['tip'][2] = z
        lift = compute_motion_metrics(sample)['overall']['lift']
        self.assertEqual(lift['p50'], 2)
        self.assertAlmostEqual(lift['p90'], 25.2)
        self.assertEqual(lift['max'], 40)

    def test_lifts_flag_uses_p50_without_max(self):
        baseline = recording([0] * 5)
        candidate = recording([0] * 5)
        for frame in baseline['frames']:
            frame['nodes'][0]['bones']['tip'][2] = 2
        for frame, z in zip(candidate['frames'], [2, 2, 2, 2, 40]):
            frame['nodes'][0]['bones']['tip'][2] = z
        baseline_metrics = compute_motion_metrics(baseline)
        candidate_metrics = compute_motion_metrics(candidate)
        comparison = compare_motion_metrics(candidate_metrics, baseline_metrics)
        self.assertEqual(candidate_metrics['overall']['lift']['max'], 40)
        self.assertEqual(candidate_metrics['overall']['lift']['p50'], 2)
        self.assertFalse(comparison['flags']['lifts'])
        for frame in candidate['frames']:
            frame['nodes'][0]['bones']['tip'][2] = 4
        comparison = compare_motion_metrics(compute_motion_metrics(candidate),
                                            baseline_metrics)
        self.assertTrue(comparison['flags']['lifts'])

    def test_stale_frames_do_not_contribute_or_bridge_differences(self):
        sample = recording([0, 1, 1, 5, 6])
        sample['frames'][2]['stale'] = True
        result = compute_motion_metrics(sample)['overall']
        self.assertEqual(result['stale_frames'], 1)
        self.assertEqual(result['frame_count'], 5)
        self.assertAlmostEqual(result['deviation']['mean'], 3.0)
        self.assertIsNone(result['jitter']['mean'])

    def test_near_zero_baseline_does_not_raise_flags(self):
        baseline = compute_motion_metrics(recording([0] * 5))
        candidate = compute_motion_metrics(recording([0] * 5,
                                                      second=[10.0001] * 5))
        comparison = compare_motion_metrics(candidate, baseline)
        self.assertFalse(any(comparison['flags'].values()))
        self.assertEqual(comparison['tolerance']['stretches_min'], 0.05)

    def test_follow_lag_four_frames(self):
        # Uneven speed changes make the lag unambiguous.
        increments = [1, 4, 2, 7, 3, 8, 1, 5, 9, 2, 6, 4] * 3
        leg = [0.0]
        for value in increments:
            leg.append(leg[-1] + value)
        skirt = [0.0] * len(leg)
        for index in range(4, len(leg)):
            skirt[index] = leg[index - 4]
        result = compute_motion_metrics(recording(skirt, extra=leg))
        self.assertEqual(result['nodes'][0]['follow']['leg']['best_lag'], 4)


if __name__ == '__main__':
    unittest.main()
