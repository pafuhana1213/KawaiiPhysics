"""Tests for performance result noise summaries."""

from __future__ import annotations

import copy
import json
import pathlib
import statistics
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import measure_noise  # noqa: E402
from test_compare_perf import dump, make_result, set_runtime  # noqa: E402


class MeasureNoiseResultsTest(unittest.TestCase):
    def test_from_results_cv_bootstrap_and_aa(self):
        first = make_result()
        second = copy.deepcopy(first)
        second['created'] = '2026-11-04T00:01:00+09:00'
        set_runtime(second, 11.0)
        with tempfile.TemporaryDirectory() as directory:
            directory = pathlib.Path(directory)
            paths = [directory / 'a.json', directory / 'b.json']
            for path, document in zip(paths, (first, second)):
                path.write_text(dump(document), encoding='utf-8')
            report = measure_noise.summarize_results(paths)
            metric = report['metrics']['kp_runtime_ms']
            self.assertAlmostEqual(metric['cv_percent'], statistics.stdev([10] * 3 + [11] * 3)
                                   / 10.5 * 100)
            self.assertEqual([item['percent'] for item in metric['three_sigma_percent_by_file']],
                             [0.0, 0.0])
            self.assertEqual(metric['aa_false_positives'], 1)
            self.assertAlmostEqual(metric['aa_pairs'][0]['delta_percent'], 10.0)
            self.assertEqual(metric['aa_pairs'][0]['threshold_percent'], 2.0)
            output = directory / 'noise.json'
            command = [sys.executable, str(pathlib.Path(measure_noise.__file__)),
                       '--from-results', *(str(path) for path in paths), '--out', str(output)]
            run = subprocess.run(command, capture_output=True, text=True, encoding='utf-8')
            self.assertEqual(run.returncode, 0, run.stderr)
            self.assertEqual(json.loads(run.stdout), json.loads(output.read_text(encoding='utf-8')))

    def test_bootstrap_nonconstant_values(self):
        first = make_result()
        second = copy.deepcopy(first)
        second['created'] = '2026-11-04T00:01:00+09:00'
        first['summary']['metrics']['kp_runtime_ms'].update(values=[9.9, 10.0, 10.1])
        with tempfile.TemporaryDirectory() as directory:
            directory = pathlib.Path(directory)
            paths = [directory / 'a.json', directory / 'b.json']
            for path, document in zip(paths, (first, second)):
                path.write_text(dump(document), encoding='utf-8')
            metric = measure_noise.summarize_results(paths)['metrics']['kp_runtime_ms']
            expected = measure_noise.compare_perf.bootstrap_sigma([9.9, 10.0, 10.1], 3)
            self.assertAlmostEqual(metric['three_sigma_percent_by_file'][0]['percent'],
                                   3 * expected / 10 * 100)

    def test_zero_reference_metric_is_incomparable(self):
        first = make_result()
        second = copy.deepcopy(first)
        second['created'] = '2026-11-04T00:01:00+09:00'
        for document in (first, second):
            document['summary']['metrics']['kp_eval_ms'].update(values=[0.0] * 3,
                                                                  median=0.0)
        with tempfile.TemporaryDirectory() as directory:
            directory = pathlib.Path(directory)
            paths = [directory / 'a.json', directory / 'b.json']
            for path, document in zip(paths, (first, second)):
                path.write_text(dump(document), encoding='utf-8')
            metric = measure_noise.summarize_results(paths)['metrics']['kp_eval_ms']
            self.assertIsNone(metric['three_sigma_percent_by_file'][0]['percent'])
            self.assertIsNone(metric['aa_pairs'][0]['exceeds_threshold'])
            self.assertEqual(metric['aa_false_positives'], 0)

    def test_from_results_rejects_incomparable_and_duplicate_inputs(self):
        for change, expected in (
            (lambda doc: doc.update(positive_control={'overrides': {'TargetFramerate': '120'}}),
             '陽性対照'),
            (lambda doc: doc.update(condition_key='different'), 'condition_key'),
            (lambda doc: doc['machine'].update(machine_id='other'), 'machine_id'),
        ):
            with self.subTest(expected=expected), tempfile.TemporaryDirectory() as directory:
                root = pathlib.Path(directory)
                first, second = make_result(), make_result()
                second['created'] = '2026-11-04T00:01:00+09:00'
                change(second)
                paths = [root / 'a.json', root / 'b.json']
                for path, document in zip(paths, (first, second)):
                    path.write_text(dump(document), encoding='utf-8')
                with self.assertRaisesRegex(ValueError, expected):
                    measure_noise.summarize_results(paths)
                command = [sys.executable, str(pathlib.Path(measure_noise.__file__)),
                           '--from-results', *(str(path) for path in paths)]
                self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            first = root / 'a.json'
            second = root / 'b.json'
            first.write_text(dump(make_result()), encoding='utf-8')
            second.write_bytes(first.read_bytes())
            with self.assertRaisesRegex(ValueError, '重複'):
                measure_noise.summarize_results([first, second])
            with self.assertRaisesRegex(ValueError, '重複'):
                measure_noise.summarize_results([first, first])

    def test_from_results_checks_every_pair(self):
        # 30 -> 29.775 and 30 -> 30.225 are each within 1% of the first file, but the last two
        # differ by about 1.5%, so the pair the A/A analysis would compare is not comparable.
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            paths = []
            for index, value in enumerate((30.0, 29.775, 30.225)):
                document = make_result()
                document['created'] = f'2026-11-04T00:0{index}:00+09:00'
                document['audit']['fixtures'] = {'force_curve': {
                    'count': 5, 'expected': {}, 'ok': True,
                    'amounts': {'NumExternalForces': value}}}
                path = root / f'{index}.json'
                path.write_text(dump(document), encoding='utf-8')
                paths.append(path)
            with self.assertRaisesRegex(ValueError, '1.json と .*2.json'):
                measure_noise.summarize_results(paths)

    def test_from_results_rejects_changed_provenance_and_amounts(self):
        changes = (
            (lambda doc: doc['provenance'].update(commit='two'), 'provenance.commit'),
            (lambda doc: doc['provenance'].update(plugin_diff_sha256='different'),
             'provenance.plugin_diff_sha256'),
            (lambda doc: doc['summary']['amounts'].update(NumNodes=21.0),
             'summary.amounts.NumNodes'),
            (lambda doc: doc['summary']['amounts'].update(NumExtra=1.0),
             'summary.amounts.NumExtra'),
            # A fixture-level change that stays inside the 1% total must still be rejected.
            (lambda doc: doc['audit'].update(fixtures={'force_curve': {
                'count': 5, 'expected': {}, 'ok': True,
                'amounts': {'NumExternalForces': 25.0}}}), 'fixture の量'),
        )
        for change, expected in changes:
            with self.subTest(expected=expected), tempfile.TemporaryDirectory() as directory:
                root = pathlib.Path(directory)
                first, second = make_result(), make_result()
                for document in (first, second):
                    document['audit']['fixtures'] = {'force_curve': {
                        'count': 5, 'expected': {}, 'ok': True,
                        'amounts': {'NumExternalForces': 30.0}}}
                second['created'] = '2026-11-04T00:01:00+09:00'
                change(second)
                paths = [root / 'a.json', root / 'b.json']
                for path, document in zip(paths, (first, second)):
                    path.write_text(dump(document), encoding='utf-8')
                with self.assertRaisesRegex(ValueError, expected):
                    measure_noise.summarize_results(paths)
                command = [sys.executable, str(pathlib.Path(measure_noise.__file__)),
                           '--from-results', *(str(path) for path in paths)]
                self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)


if __name__ == '__main__':
    unittest.main()
