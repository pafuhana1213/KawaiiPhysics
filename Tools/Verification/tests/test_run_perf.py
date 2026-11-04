"""Unit tests for the CSV reader and performance suite expansion."""

from __future__ import annotations

import json
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import run_perf


HEADER = ('Frame,KawaiiPhysics/GameThread/Eval,KawaiiPhysics/AllWorkers/Eval,'
          'KawaiiPhysics/GameThread/EditorSync,KawaiiPhysics/GameThread/SimulateModifyBones,'
          'KawaiiPhysics/NumNodes,KawaiiPhysics/AllWorkers/NumSubsteps')
META = '[HasHeaderRowAtEnd],1,[engineversion],5.8.4,[config],Development'


class CsvCaptureTests(unittest.TestCase):
    def parse(self, lines: list[str]):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / 'capture.csv'
            path.write_text('\n'.join(lines) + '\n', encoding='utf-8')
            return run_perf.read_csv_capture(path)

    def test_trailing_header_metadata_and_mean(self):
        rows, metadata = self.parse([HEADER, '0,2,3,0.5,1,4,2',
                                     '1,4,5,1,2,6,4', HEADER, META])
        metrics, amounts = run_perf.aggregate_csv(rows, 'serial', 'on')
        self.assertEqual(metadata['engineversion'], '5.8.4')
        self.assertEqual(len(rows), 2)
        self.assertEqual(metrics['kp_eval_ms'], 7)
        self.assertEqual(metrics['kp_runtime_ms'], 6.25)
        self.assertEqual(amounts, {'NumNodes': 5, 'NumSubsteps': 3})

    def test_columns_added_mid_capture_follow_trailing_header(self):
        # A stat that first appears mid-capture extends only the trailing header; earlier rows
        # are prefixes of it, later rows carry the new column.
        first = 'Frame,KawaiiPhysics/GameThread/Eval'
        full = first + ',KawaiiPhysics/NumWorldCollisionSweeps,Other/Stat'
        rows, _ = self.parse([first, '0,2', '1,4,6,1', full, META])
        metrics, amounts = run_perf.aggregate_csv(rows, 'serial', 'on')
        self.assertEqual(metrics['kp_eval_ms'], 3)
        self.assertEqual(amounts['NumWorldCollisionSweeps'], 3)
        for lines in ([first, '0', full, META],              # shorter than the leading header
                      [first, '0,2,6,1,9', full, META],      # longer than the trailing header
                      ['Frame,Other', '0,2', full, META]):   # leading header is not a prefix
            with self.subTest(lines=lines), self.assertRaises(ValueError):
                self.parse(lines)

    def test_parallel_accepts_worker_eval(self):
        header = 'Frame,KawaiiPhysics/AllWorkers/Eval,KawaiiPhysics/AllWorkers/NumNodes'
        rows, _ = self.parse([header, '0,2,3', header, META])
        metrics, amounts = run_perf.aggregate_csv(rows, 'parallel', 'on')
        self.assertEqual(metrics['kp_eval_ms'], 2)
        self.assertEqual(amounts['NumNodes'], 3)
        with self.assertRaisesRegex(ValueError, 'Eval timing'):
            run_perf.aggregate_csv(rows, 'serial', 'on')

    def test_missing_eval_and_truncated_capture(self):
        header = 'Frame,KawaiiPhysics/NumNodes'
        rows, _ = self.parse([header, '0,0', header, META])
        with self.assertRaisesRegex(ValueError, 'Eval timing'):
            run_perf.aggregate_csv(rows, 'serial', 'on')
        for lines in ([HEADER, '0,0,0,0,0,0,0'],
                      [HEADER, '0,0,0'],
                      [HEADER, '0,0,0,0,0,0,0', HEADER]):
            with self.subTest(lines=lines), self.assertRaises(ValueError):
                self.parse(lines)

    def test_all_zero_off_is_valid(self):
        rows, _ = self.parse([HEADER, '0,0,0,0,0,0,0', HEADER, META])
        metrics, amounts = run_perf.aggregate_csv(rows, 'serial', 'off')
        self.assertTrue(run_perf.check_controls([
            {'state': 'off', 'index': 1, 'metrics': metrics, 'amounts': amounts}], {})['negative_ok'])

    def test_controls_fail_without_relaxing_threshold(self):
        runs = [
            {'state': 'on', 'index': 0, 'metrics': {}, 'amounts': {'NumNodes': 3}},
            {'state': 'off', 'index': 1, 'metrics': {'kp_eval_ms': 0.0011,
                                                    'kp_simulate_ms': 0}, 'amounts': {'NumNodes': 0}},
        ]
        controls = run_perf.check_controls(runs, {'NumNodes': 4})
        self.assertFalse(controls['amounts_ok'])
        self.assertFalse(controls['negative_ok'])
        self.assertEqual(len(controls['problems']), 2)


class SuiteTests(unittest.TestCase):
    def setUp(self):
        self.suite = json.loads(run_perf.DEFAULT_SUITE.read_text(encoding='utf-8'))

    def test_expansion_and_expected_amounts(self):
        cells = run_perf.expand_suite(self.suite)
        self.assertEqual(len(cells), sum(group['count'] for group in self.suite['groups']))
        self.assertEqual(len({tuple(cell['location']) for cell in cells}), len(cells))
        self.assertEqual(run_perf.expected_amounts(self.suite)['NumNodes'], 55)
        self.assertNotIn('TargetFramerate', cells[0].get('overrides', {}))

    def test_positive_control_does_not_change_condition_key(self):
        original = run_perf.expand_suite(self.suite)
        positive = run_perf.expand_suite(self.suite, True)
        self.assertEqual([cell['location'] for cell in original],
                         [cell['location'] for cell in positive])
        self.assertTrue(all(cell['overrides']['TargetFramerate'] == '120'
                            for cell in positive))
        conditions = {'suite': 'performance', 'profile': 'serial', 'fps': self.suite['fps']}
        key = run_perf.condition_key(conditions)
        self.assertEqual(key, run_perf.condition_key(conditions))
        self.assertNotEqual(key, run_perf.condition_key({**conditions, 'fps': 120}))

    def test_fixture_audit_detects_missing_work_with_shared_stat(self):
        suite = {'fixtures': {'first': {'expect': {'NumNodes': 1}},
                              'second': {'expect': {'NumNodes': 1}}}}
        first = run_perf.audit_fixture(suite, {'fixture': 'first', 'count': 5},
                                       {'NumNodes': 0})
        second = run_perf.audit_fixture(suite, {'fixture': 'second', 'count': 5},
                                        {'NumNodes': 100})
        self.assertFalse(first['ok'])
        self.assertTrue(second['ok'])
        self.assertEqual(first['expected'], {'NumNodes': 5})

    def test_settings_mismatch_and_condition_key(self):
        expected = self.suite['settings']
        actual = {**expected, 'parallel_anim_evaluation': 0, 'parallel_anim_update': 0}
        actual['time_dilation'] = 1  # ランナーの JSONL は整数表記で出力する。
        cvars = self.suite['profiles']['serial']['cvars']
        run_perf.validate_settings(actual, expected, cvars)
        for changed in ({**actual, 'max_substeps': 3},
                        {**actual, 'parallel_anim_update': 1}):
            with self.assertRaisesRegex(ValueError, 'settings'):
                run_perf.validate_settings(changed, expected, cvars)
        conditions = {'settings': expected}
        altered = {'settings': {**expected, 'max_substeps': 3}}
        self.assertNotEqual(run_perf.condition_key(conditions), run_perf.condition_key(altered))

    def test_effective_cvars_mismatch_missing_and_numeric_spelling(self):
        serial = self.suite['profiles']['serial']['cvars']
        parallel = self.suite['profiles']['parallel']['cvars']
        self.assertEqual(parallel, ['a.ParallelAnimEvaluation=1', 'a.ParallelAnimUpdate=1'])
        actual = {'a.ParallelAnimEvaluation': '1.0', 'a.ParallelAnimUpdate': '1',
                  'a.AnimNode.KawaiiPhysics.Enable': '0.0'}
        run_perf.validate_effective_cvars(actual, parallel +
                                          ['a.AnimNode.KawaiiPhysics.Enable=0'])
        # The runner reports bool CVars as "true"/"false" (seen on hardware for the Enable CVar).
        run_perf.validate_effective_cvars({**actual, 'a.AnimNode.KawaiiPhysics.Enable': 'false'},
                                          ['a.AnimNode.KawaiiPhysics.Enable=0'])
        with self.assertRaisesRegex(ValueError, 'effective_cvars'):
            run_perf.validate_effective_cvars({**actual, 'a.AnimNode.KawaiiPhysics.Enable': 'true'},
                                              ['a.AnimNode.KawaiiPhysics.Enable=0'])
        for changed, requested in ((actual, serial),
                                   ({**actual, 'a.ParallelAnimUpdate': '0'}, parallel),
                                   ({**actual, 'a.ParallelAnimUpdate': None}, parallel),
                                   ({'a.ParallelAnimEvaluation': '1'}, parallel),
                                   (actual, parallel + ['a.AnimNode.KawaiiPhysics.Enable=1'])):
            with self.subTest(changed=changed, requested=requested):
                with self.assertRaisesRegex(ValueError, 'effective_cvars'):
                    run_perf.validate_effective_cvars(changed, requested)

    def test_capture_window_requires_exact_csv_and_frame_count(self):
        runner = {'csv': {'start_runner_frame': 1}, 'frame_ms': [1, 2, 3]}
        self.assertEqual(run_perf.capture_window(runner, 2, 2), [2, 3])
        with self.assertRaisesRegex(ValueError, 'frame_ms'):
            run_perf.capture_window(runner, 3, 3)
        with self.assertRaisesRegex(ValueError, 'CSV'):
            run_perf.capture_window(runner, 2, 1)

    def test_transitive_asset_signature_tracks_content_and_cycles(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            content = root / 'Content'
            content.mkdir()
            (content / 'A.uasset').write_bytes(b'/Game/B.B')
            (content / 'B.uasset').write_bytes(b'/Game/C.C /Game/Missing.Missing')
            (content / 'C.uasset').write_bytes(b'/Game/A.A one')
            suite = {'fixtures': {'only': {'mesh': '/Game/A.A', 'anim_class': '/Game/A.A'}},
                     'level': '/KawaiiPhysicsDev/Levels/Missing'}
            paths = run_perf.collect_asset_files(suite, root)
            self.assertEqual([path.name for path in paths], ['A.uasset', 'B.uasset', 'C.uasset'])
            before = run_perf.asset_signature(paths, root)
            (content / 'C.uasset').write_bytes(b'/Game/A.A two')
            self.assertNotEqual(before, run_perf.asset_signature(paths, root))


if __name__ == '__main__':
    unittest.main()
