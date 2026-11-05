"""Tests for performance result validation and comparison."""

from __future__ import annotations

import copy
import json
import pathlib
import statistics
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import compare_perf  # noqa: E402


def make_result() -> dict:
    conditions = {'suite': 'performance', 'suite_sha256': 'suite', 'level': '/test',
                  'level_sha256': 'level', 'assets_sha256': 'assets', 'asset_count': 3,
                  'runner_sha256': 'runner', 'profile': 'serial', 'fps': 60,
                  'frames': 200, 'capture': {'start_frame': 100, 'frames': 100},
                  'settings': {'fixed_substepping': True, 'max_substeps': 4,
                               'time_dilation': 1.0},
                  'runs_per_state': 3, 'cvars': [], 'extra_arguments': [], 'affinity': None}
    runs = [{'index': i, 'state': 'on' if i % 2 == 0 else 'off', 'exit_code': 0,
             'seconds': 1.0, 'csv_path': 'capture.csv', 'capture_frames': 100,
             'frame_ms_median': 20.0, 'calib_ms': 30.0, 'metrics': {}, 'amounts': {}}
            for i in range(6)]
    return {'schema': 'kpv-perf-result/1', 'created': '2026-11-04T00:00:00+09:00',
            'machine': {'machine_id': 'test-cpu', 'cpu': 'Test CPU', 'logical_cpus': 8,
                        'power_plan': 'plan'},
            'engine': {'root': 'E:/UE', 'version': '5.8', 'build_config': 'Development'},
            'conditions': conditions, 'condition_key': 'same', 'positive_control': None,
            'audit': {'ok': True, 'fixtures': {}, 'problems': []},
            'provenance': {'commit': 'one', 'dirty': False, 'plugin_diff_sha256': ''},
            'runs': runs,
            'summary': {'metrics': {
                'kp_runtime_ms': {'unit': 'ms', 'gating': True, 'values': [10.0] * 3,
                                  'median': 10.0},
                'kp_eval_ms': {'unit': 'ms', 'gating': False, 'values': [12.0] * 3,
                               'median': 12.0}},
                        'amounts': {'NumSubsteps': 10.0, 'NumNodes': 20.0},
                        'calib_ms': 30.0,
                        'controls': {'negative_ok': True, 'amounts_ok': True,
                                     'problems': []}}}


def sync_runs(document: dict) -> dict:
    """Copy the summary values into the ON runs so the summary stays consistent with them.

    The tests edit the summary for brevity; the validator recomputes it from the runs."""
    on = [run for run in sorted(document['runs'], key=lambda run: run['index'])
          if run['state'] == 'on']
    for name, metric in document['summary']['metrics'].items():
        if name == 'frame_cost_ms' or not isinstance(metric.get('values'), list):
            continue
        for run, value in zip(on, metric['values']):
            run['metrics'][name] = value
    for run in on:
        run['amounts'] = dict(document['summary']['amounts'])
    return document


def dump(document) -> str:
    if isinstance(document, dict) and isinstance(document.get('runs'), list) \
            and isinstance(document.get('summary'), dict):
        document = sync_runs(copy.deepcopy(document))
    return json.dumps(document)


def set_runtime(result: dict, value: float) -> None:
    metric = result['summary']['metrics']['kp_runtime_ms']
    metric['values'] = [value] * 3
    metric['median'] = value


class ComparePerfTest(unittest.TestCase):
    def setUp(self):
        self.baseline = make_result()
        self.current = copy.deepcopy(self.baseline)

    def verdict(self) -> dict:
        return compare_perf.compare(sync_runs(self.current), sync_runs(self.baseline))

    def test_same_warn_improved_and_reference_metric(self):
        self.assertEqual((self.verdict()['verdict'], self.verdict()['exit_code']), ('OK', 0))
        set_runtime(self.current, 11.0)
        self.assertEqual((self.verdict()['verdict'], self.verdict()['exit_code']), ('WARN', 1))
        set_runtime(self.current, 9.0)
        self.assertEqual((self.verdict()['verdict'], self.verdict()['exit_code']), ('IMPROVED', 0))
        set_runtime(self.current, 10.0)
        self.current['summary']['metrics']['kp_eval_ms']['values'] = [20.0] * 3
        self.current['summary']['metrics']['kp_eval_ms']['values'] = [20.0] * 3
        self.current['summary']['metrics']['kp_eval_ms']['median'] = 20.0
        result = self.verdict()
        self.assertEqual(result['exit_code'], 0)
        self.assertIn('WARN', [row['verdict'] for row in result['metrics']])

    def test_summary_must_match_runs(self):
        # A summary that disagrees with its own runs (stale aggregation or an edited file) is
        # rejected instead of being trusted; here the runs grew 30% but the median did not.
        sync_runs(self.current)
        for run in self.current['runs']:
            if run['state'] == 'on':
                run['metrics']['kp_runtime_ms'] = 13.0
        self.current['summary']['metrics']['kp_runtime_ms']['values'] = [13.0] * 3
        result = compare_perf.compare(self.current, sync_runs(self.baseline))
        self.assertEqual((result['verdict'], result['exit_code']), ('INVALID', 5))
        self.assertIn('中央値', compare_perf.render(result))
        self.current = copy.deepcopy(self.baseline)
        sync_runs(self.current)
        self.current['runs'][0]['metrics']['kp_runtime_ms'] = 20.0
        self.assertEqual(compare_perf.compare(self.current, self.baseline)['exit_code'], 5)
        self.current = copy.deepcopy(self.baseline)
        sync_runs(self.current)
        self.current['summary']['amounts']['NumNodes'] = 25.0
        self.assertEqual(compare_perf.compare(self.current, self.baseline)['exit_code'], 5)

    def test_reference_metric_cannot_invalidate_baseline(self):
        # A noisy or unused reference stage must not make the whole baseline unusable.
        noisy = {'unit': 'ms', 'gating': False, 'values': [0.1, 0.5, 0.9], 'median': 0.5}
        unused = {'unit': 'ms', 'gating': False, 'values': [0.0] * 3, 'median': 0.0}
        for document in (self.baseline, self.current):
            document['summary']['metrics']['kp_noisy_ms'] = copy.deepcopy(noisy)
            document['summary']['metrics']['kp_unused_ms'] = copy.deepcopy(unused)
        result = self.verdict()
        self.assertEqual((result['verdict'], result['exit_code']), ('OK', 0))
        rows = {row['name']: row for row in result['metrics']}
        self.assertEqual(rows['kp_unused_ms']['verdict'], 'N/A')
        self.assertIsNone(rows['kp_unused_ms']['delta_percent'])
        self.assertIn('kp_unused_ms', compare_perf.render(result))

    def test_noisy_gating_baseline_is_invalid(self):
        self.baseline['summary']['metrics']['kp_runtime_ms']['values'] = [5.0, 10.0, 15.0]
        self.assertEqual(self.verdict()['exit_code'], 5)

    def test_noisy_and_unexplained_warning(self):
        self.current['summary']['calib_ms'] = 36.0
        set_runtime(self.current, 11.0)
        self.assertEqual((self.verdict()['verdict'], self.verdict()['exit_code']), ('NOISY', 2))
        set_runtime(self.current, 13.0)
        self.assertEqual((self.verdict()['verdict'], self.verdict()['exit_code']), ('WARN', 1))

    def test_stale_ignores_provenance_and_positive_control(self):
        self.current['provenance']['commit'] = 'two'
        self.assertEqual(self.verdict()['exit_code'], 0)
        self.current['positive_control'] = {'overrides': {'TargetFramerate': '120'}}
        self.current['summary']['amounts']['NumSubsteps'] = 15.0
        self.assertEqual(self.verdict()['exit_code'], 0)
        self.current['conditions']['fps'] = 120
        self.current['condition_key'] = 'different'
        result = self.verdict()
        self.assertEqual((result['verdict'], result['exit_code']), ('STALE', 3))
        self.assertIn('conditions.fps', compare_perf.render(result))

    def test_machine_mismatch_fields(self):
        for section, field, value in [('machine', 'machine_id', 'another'),
                                      ('machine', 'power_plan', 'another'),
                                      ('engine', 'version', '5.7')]:
            with self.subTest(field=field):
                self.current = copy.deepcopy(self.baseline)
                self.current[section][field] = value
                result = self.verdict()
                self.assertEqual((result['verdict'], result['exit_code']),
                                 ('MACHINE_MISMATCH', 4))
                self.assertIn(field, compare_perf.render(result))

    def test_runner_build_mismatch_is_machine_mismatch(self):
        self.baseline['engine']['runner_build'] = 'editor'
        self.current['engine']['runner_build'] = 'game_test'
        result = self.verdict()
        self.assertEqual((result['verdict'], result['exit_code']), ('MACHINE_MISMATCH', 4))
        self.assertIn('engine.runner_build', compare_perf.render(result))

    def test_missing_runner_build_only_warns_without_changing_verdict(self):
        for missing in ('baseline', 'current'):
            for runtime, calib, verdict, code in ((10.0, 30.0, 'OK', 0),
                                                  (11.0, 30.0, 'WARN', 1),
                                                  (9.0, 30.0, 'IMPROVED', 0),
                                                  (11.0, 36.0, 'NOISY', 2)):
                with self.subTest(missing=missing, verdict=verdict):
                    self.baseline = make_result()
                    self.current = copy.deepcopy(self.baseline)
                    present = self.current if missing == 'baseline' else self.baseline
                    present['engine']['runner_build'] = 'game_test'
                    set_runtime(self.current, runtime)
                    self.current['summary']['calib_ms'] = calib
                    result = self.verdict()
                    self.assertEqual((result['verdict'], result['exit_code']), (verdict, code))
                    self.assertIn('runner_build が記録されていない（古い結果）', result['warnings'])
                    self.assertEqual(result['differences'], [])
        self.current['summary']['amounts']['NumNodes'] = 21.0
        self.assertEqual(self.verdict()['verdict'], 'WORKLOAD_CHANGED')

    def test_invalid_inputs(self):
        changes = [
            lambda d: d['summary']['metrics'].pop('kp_runtime_ms'),
            lambda d: d['summary']['metrics']['kp_runtime_ms'].update(median=float('nan')),
            lambda d: d['runs'].pop(),
            lambda d: d['runs'][0].update(capture_frames=89),
            lambda d: d['runs'][0].update(exit_code=1),
            lambda d: d['summary']['controls'].update(negative_ok=False),
            lambda d: d['summary']['controls'].update(amounts_ok=False),
            lambda d: d['summary']['amounts'].update(NumNodes='bad'),
        ]
        for change in changes:
            with self.subTest(change=changes.index(change)):
                self.current = copy.deepcopy(self.baseline)
                change(self.current)
                self.assertEqual(self.verdict()['exit_code'], 5)
        self.current = copy.deepcopy(self.baseline)
        self.baseline['summary']['metrics']['kp_runtime_ms']['median'] = 0
        self.assertEqual(self.verdict()['exit_code'], 5)
        self.baseline = make_result()
        self.baseline['summary']['metrics']['kp_runtime_ms']['values'] = [1, 10, 100]
        self.assertIn('基準値として採用できない', compare_perf.render(self.verdict()))
        self.current = make_result()
        self.baseline = make_result()
        self.current['positive_control'] = {'overrides': {'TargetFramerate': '120'}}
        self.assertEqual(self.verdict()['exit_code'], 5)
        self.current = make_result()
        self.current['summary']['metrics']['kp_eval_ms']['unit'] = 'seconds'
        self.current['machine']['power_plan'] = 'other'
        self.assertEqual(self.verdict()['exit_code'], 5)

    def test_workload_changed_and_positive_control(self):
        self.current['summary']['amounts']['NumNodes'] = 21.0
        result = self.verdict()
        self.assertEqual((result['verdict'], result['exit_code']), ('WORKLOAD_CHANGED', 3))
        self.assertIn('量が変わった', compare_perf.render(result))
        self.current['positive_control'] = {'overrides': {'TargetFramerate': '120'}}
        self.current['summary']['amounts']['NumSubsteps'] = 15.0
        self.assertEqual(self.verdict()['exit_code'], 0)
        self.assertIn('WARN が出ることを期待', compare_perf.render(self.verdict()))

    def test_workload_decrease_missing_amount_and_zero(self):
        self.current['summary']['amounts']['NumNodes'] = 19.7
        self.assertEqual((self.verdict()['verdict'], self.verdict()['exit_code']),
                         ('WORKLOAD_CHANGED', 3))
        self.current = copy.deepcopy(self.baseline)
        self.current['summary']['amounts'].pop('NumNodes')
        self.assertEqual(self.verdict()['exit_code'], 3)
        self.current = copy.deepcopy(self.baseline)
        for doc in (self.current, self.baseline):
            doc['summary']['amounts']['NumZero'] = 0.0
        self.assertEqual(self.verdict()['exit_code'], 0)

    def test_baseline_requires_successful_audit(self):
        for audit in (None, {'ok': False, 'fixtures': {}, 'problems': ['失敗']}):
            with self.subTest(audit=audit):
                self.baseline = make_result()
                self.baseline['audit'] = audit
                self.assertEqual((self.verdict()['verdict'], self.verdict()['exit_code']),
                                 ('INVALID', 5))

    def test_current_requires_successful_audit(self):
        for audit in (None, {'ok': False, 'fixtures': {}, 'problems': ['失敗']}):
            with self.subTest(audit=audit):
                self.current = make_result()
                self.current['audit'] = audit
                result = self.verdict()
                self.assertEqual((result['verdict'], result['exit_code']), ('INVALID', 5))
                self.assertIn('現在の結果に --audit', compare_perf.render(result))

    def test_fixture_amount_loss_below_total_one_percent(self):
        for document in (self.baseline, self.current):
            document['summary']['amounts']['NumExternalForces'] = 5645.0
            document['audit']['fixtures'] = {
                'force_curve': {'count': 5, 'expected': {'NumExternalForces': 5},
                                'amounts': {'NumExternalForces': 30.0}, 'ok': True}}
        self.current['summary']['amounts']['NumExternalForces'] = 5615.0
        self.current['audit']['fixtures']['force_curve']['amounts']['NumExternalForces'] = 0.0
        result = self.verdict()
        self.assertEqual((result['verdict'], result['exit_code']), ('WORKLOAD_CHANGED', 3))
        self.assertIn('force_curve.NumExternalForces', compare_perf.render(result))
        # A positive control legitimately changes the workload (doubled substeps), so the audit
        # amounts are not compared; only the substep increase is required.
        self.current['positive_control'] = {'overrides': {'TargetFramerate': '120'}}
        self.current['summary']['amounts']['NumSubsteps'] = 20.0
        self.assertEqual((self.verdict()['verdict'], self.verdict()['exit_code']), ('OK', 0))

    def test_fixture_set_and_amount_name_changes(self):
        self.baseline['audit']['fixtures'] = {
            'force_curve': {'amounts': {'NumExternalForces': 30.0, 'NumZero': 0.0}}}
        self.current['audit']['fixtures'] = {
            'force_basic': {'amounts': {'NumExternalForces': 30.0}}}
        result = self.verdict()
        self.assertEqual((result['verdict'], result['exit_code']), ('WORKLOAD_CHANGED', 3))
        self.assertIn('force_curve', compare_perf.render(result))
        self.current['audit']['fixtures'] = {
            'force_curve': {'amounts': {'NumExternalForces': 30.0}}}
        result = self.verdict()
        self.assertEqual((result['verdict'], result['exit_code']), ('WORKLOAD_CHANGED', 3))
        self.assertIn('force_curve.NumZero', compare_perf.render(result))

    def test_positive_control_baseline_rejected_on_read_and_update(self):
        self.baseline['positive_control'] = {'overrides': {'TargetFramerate': '120'}}
        self.assertEqual((self.verdict()['verdict'], self.verdict()['exit_code']),
                         ('INVALID', 5))
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            current = root / 'positive.json'
            baseline = root / 'baseline.json'
            current.write_text(dump(self.baseline), encoding='utf-8')
            command = [sys.executable, str(pathlib.Path(compare_perf.__file__)), str(current),
                       '--baseline', str(baseline)]
            run = subprocess.run(command + ['--update-baseline', '--yes'], capture_output=True,
                                 text=True, encoding='utf-8')
            self.assertEqual(run.returncode, 5)
            self.assertFalse(baseline.exists())
            baseline.write_text(dump(self.baseline), encoding='utf-8')
            current.write_text(dump(self.current), encoding='utf-8')
            run = subprocess.run(command, capture_output=True, text=True, encoding='utf-8')
            self.assertEqual(run.returncode, 5)

    def test_update_replaces_unusable_old_baseline(self):
        # A baseline written before a contract change (no audit) or a corrupt file must not block
        # its replacement, while a plain comparison against it still fails.
        old = copy.deepcopy(self.baseline)
        old['audit'] = None
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            current = root / 'current.json'
            baseline = root / 'baseline.json'
            current.write_text(dump(self.current), encoding='utf-8')
            command = [sys.executable, str(pathlib.Path(compare_perf.__file__)), str(current),
                       '--baseline', str(baseline)]
            for content in (dump(old), '{broken'):
                with self.subTest(content=content[:10]):
                    baseline.write_text(content, encoding='utf-8')
                    run = subprocess.run(command, capture_output=True, text=True, encoding='utf-8')
                    self.assertEqual(run.returncode, 5)
                    run = subprocess.run(command + ['--update-baseline', '--yes'],
                                         capture_output=True, text=True, encoding='utf-8')
                    self.assertEqual(run.returncode, 0)
                    self.assertIn('BASELINE_REPLACED', run.stdout)
                    self.assertEqual(json.loads(baseline.read_text(encoding='utf-8'))['audit'],
                                     self.current['audit'])

    def test_update_over_stale_baseline_succeeds(self):
        # Rebaselining after a deliberate condition change is a success; a plain comparison
        # against the old baseline is still STALE.
        old = copy.deepcopy(self.baseline)
        old['condition_key'] = 'older'
        old['conditions']['runner_sha256'] = 'older-runner'
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            current = root / 'current.json'
            baseline = root / 'baseline.json'
            current.write_text(dump(self.current), encoding='utf-8')
            baseline.write_text(dump(old), encoding='utf-8')
            command = [sys.executable, str(pathlib.Path(compare_perf.__file__)), str(current),
                       '--baseline', str(baseline)]
            run = subprocess.run(command, capture_output=True, text=True, encoding='utf-8')
            self.assertEqual(run.returncode, 3)
            run = subprocess.run(command + ['--update-baseline', '--yes'], capture_output=True,
                                 text=True, encoding='utf-8')
            self.assertEqual(run.returncode, 0)
            self.assertIn('BASELINE_UPDATED', run.stdout)
            self.assertIn('更新前の比較結果: STALE', run.stdout)
            self.assertEqual(json.loads(baseline.read_text(encoding='utf-8'))['condition_key'],
                             self.current['condition_key'])

    def test_cli_missing_baseline_and_update(self):
        script = pathlib.Path(compare_perf.__file__)
        with tempfile.TemporaryDirectory() as directory:
            directory = pathlib.Path(directory)
            current = directory / 'current.json'
            baseline = directory / 'sub' / 'baseline.json'
            output = directory / 'report.json'
            current.write_text(dump(self.current), encoding='utf-8')
            command = [sys.executable, str(script), str(current), '--baseline', str(baseline),
                       '--json-out', str(output)]
            run = subprocess.run(command, capture_output=True, text=True, encoding='utf-8')
            self.assertEqual(run.returncode, 6)
            self.assertIn('基準値なし', run.stdout)
            self.assertEqual(json.loads(output.read_text(encoding='utf-8'))['exit_code'], 6)
            run = subprocess.run(command + ['--update-baseline'], capture_output=True,
                                 text=True, encoding='utf-8')
            self.assertFalse(baseline.exists())
            self.assertIn('--yes', run.stdout)
            # A noisy batch must not become the first baseline either.
            noisy = copy.deepcopy(self.current)
            noisy['summary']['metrics']['kp_runtime_ms']['values'] = [5.0, 10.0, 15.0]
            current.write_text(dump(noisy), encoding='utf-8')
            run = subprocess.run(command + ['--update-baseline', '--yes'], capture_output=True,
                                 text=True, encoding='utf-8')
            self.assertEqual(run.returncode, 5)
            self.assertFalse(baseline.exists())
            current.write_text(dump(self.current), encoding='utf-8')
            run = subprocess.run(command + ['--update-baseline', '--yes'], capture_output=True,
                                 text=True, encoding='utf-8')
            self.assertEqual(run.returncode, 0)
            self.assertIn('BASELINE_CREATED', run.stdout)
            self.assertTrue(baseline.exists())
            self.assertEqual(json.loads(baseline.read_text(encoding='utf-8'))['baseline']['source'],
                             str(current))
            self.current['summary']['controls']['negative_ok'] = False
            current.write_text(dump(self.current), encoding='utf-8')
            before = baseline.read_bytes()
            run = subprocess.run(command + ['--update-baseline', '--yes'], capture_output=True,
                                 text=True, encoding='utf-8')
            self.assertEqual(run.returncode, 5)
            self.assertEqual(baseline.read_bytes(), before)


class BaselinePathTests(unittest.TestCase):
    def test_default_path_uses_build_with_legacy_fallbacks(self):
        current = make_result()
        editor = 'perf__test-cpu__serial.json'
        game = 'perf__test-cpu__serial__game_test.json'
        self.assertEqual(compare_perf.default_baseline_path(current).name, editor)
        current['engine']['runner_build'] = 'game_test'
        self.assertEqual(compare_perf.default_baseline_path(current).name, game)
        current['conditions']['build'] = 'editor'
        self.assertEqual(compare_perf.default_baseline_path(current).name, editor)
        current['conditions']['build'] = 'game_test'
        self.assertEqual(compare_perf.default_baseline_path(current).name, game)
        self.assertIsNone(compare_perf.default_baseline_path([]))

    def test_build_difference_is_a_condition_difference(self):
        baseline = sync_runs(make_result())
        current = copy.deepcopy(baseline)
        baseline['conditions']['build'] = 'editor'
        current['conditions']['build'] = 'game_test'
        current['condition_key'] = 'different'
        result = compare_perf.compare(current, baseline)
        self.assertEqual(result['verdict'], 'STALE')
        self.assertTrue(any('conditions.build' in value for value in result['differences']))

    def test_update_baseline_uses_same_default_path_for_both_builds(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            path = root / 'current.json'
            for build, suffix in (('editor', ''), ('game_test', '__game_test')):
                current = make_result()
                current['conditions']['build'] = build
                path.write_text(dump(current), encoding='utf-8')
                with mock.patch.object(compare_perf, 'ROOT', root), mock.patch('sys.stdout'):
                    self.assertEqual(compare_perf.main([str(path), '--update-baseline', '--yes']), 0)
                self.assertTrue((root / 'docs/perf/verification' /
                                 f'perf__test-cpu__serial{suffix}.json').is_file())


def make_ab(root: pathlib.Path, ref_values=None, cur_values=None) -> pathlib.Path:
    """Write a four-block session with explicit ON/OFF pairs and consistent summaries."""
    values = {'ref': [10.0] * 8 if ref_values is None else ref_values,
              'cur': [10.0] * 8 if cur_values is None else cur_values}
    documents = {arm: make_result() for arm in values}
    ab = {'schema': 'kpv-perf-ab/1', 'created': '2026-11-04T00:00:00+09:00',
          'machine': documents['ref']['machine'], 'blocks': 4, 'arms': {}, 'order': [],
          'positive_control': None}
    for arm, doc in documents.items():
        doc['conditions'].update(runs_per_state=8, build='game_development', toolchain='14.44.35222')
        doc['runs'] = []
        doc['summary']['metrics'] = {
            name: {'unit': 'ms', 'gating': name == 'kp_runtime_ms',
                   'values': list(values[arm]), 'median': statistics.median(values[arm])}
            for name in ('kp_runtime_ms', 'kp_eval_ms', 'frame_cost_ms')}
        ab['arms'][arm] = {'package_dir': f'packages/{arm}',
                           'result': f'{arm}/result.json'}
    for block in range(4):
        order = ('ref', 'cur', 'cur', 'ref') if block % 2 == 0 else ('cur', 'ref', 'ref', 'cur')
        for arm in order:
            for state in ('on', 'off'):
                doc = documents[arm]
                index, sequence = len(doc['runs']), len(ab['order'])
                started = f'2026-11-04T00:00:{sequence:02d}+09:00'
                value = values[arm][index // 2] if state == 'on' else 0
                run = {'index': index, 'state': state, 'exit_code': 0, 'seconds': 1.0,
                       'block': block, 'sequence': sequence, 'started': started,
                       'csv_path': 'capture.csv', 'capture_frames': 100, 'calib_ms': 30.0,
                       'frame_ms_median': value + 1, 'metrics': {'kp_runtime_ms': value, 'kp_eval_ms': value},
                       'amounts': dict(doc['summary']['amounts']) if state == 'on' else {}}
                doc['runs'].append(run)
                ab['order'].append({'sequence': sequence, 'block': block, 'arm': arm, 'state': state,
                                     'index_in_arm': index, 'started': started, 'seconds': 1.0, 'ok': True})
    for arm, doc in documents.items():
        target = root / arm
        target.mkdir()
        (target / 'result.json').write_text(json.dumps(doc), encoding='utf-8')
    path = root / 'ab.json'
    path.write_text(json.dumps(ab), encoding='utf-8')
    return path


class AbComparePerfTests(unittest.TestCase):
    def change(self, root, arm, update):
        path = root / arm / 'result.json'
        doc = json.loads(path.read_text(encoding='utf-8'))
        update(doc)
        path.write_text(json.dumps(doc), encoding='utf-8')

    def test_ab_equal_warn_improved_and_frame_cost(self):
        for value, verdict, code in ((10, 'OK', 0), (10.5, 'WARN', 1), (9.5, 'IMPROVED', 0)):
            with self.subTest(verdict=verdict), tempfile.TemporaryDirectory() as directory:
                path = make_ab(pathlib.Path(directory), cur_values=[value] * 8)
                result = compare_perf.compare_ab(path)
                self.assertEqual((result['verdict'], result['exit_code']), (verdict, code))
                for row in result['metrics']:
                    self.assertAlmostEqual(row['center_percent'], (value / 10 - 1) * 100)
                    self.assertEqual(row['halfwidth_percent'], 0)
                    self.assertEqual(row['verdict'], verdict)
                self.assertIn('±half%', compare_perf.render(result))

    def test_ab_inconclusive_wide_interval(self):
        ref = [9, 11] * 4
        cur = [value * factor for value, factor in zip(ref, [.98, .98, 1.02, 1.02, 1.02, 1.02, .98, .98])]
        with tempfile.TemporaryDirectory() as directory:
            result = compare_perf.compare_ab(make_ab(pathlib.Path(directory), ref, cur))
            self.assertEqual((result['verdict'], result['exit_code']), ('INCONCLUSIVE', 7))
            row = next(row for row in result['metrics'] if row['gating'])
            self.assertAlmostEqual(row['center_percent'], 0)
            self.assertAlmostEqual(row['halfwidth_percent'], 3.182 * statistics.stdev([-2, 2, 2, -2]) / 2)

    def test_ab_wide_interval_clear_regression_and_improvement(self):
        spread = 3.5 * 3 ** 0.5 / compare_perf.t_critical(4)
        ref = [9, 11] * 4
        for center, verdict, code in ((33, 'WARN', 1), (-33, 'IMPROVED', 0)):
            deltas = [center + sign * spread for sign in (-1, 1, 1, -1)]
            cur = [value * (1 + deltas[index // 2] / 100) for index, value in enumerate(ref)]
            with self.subTest(center=center), tempfile.TemporaryDirectory() as directory:
                result = compare_perf.compare_ab(make_ab(pathlib.Path(directory), ref, cur))
                self.assertEqual((result['verdict'], result['exit_code']), (verdict, code))
                for row in result['metrics']:
                    self.assertAlmostEqual(row['center_percent'], center)
                    self.assertAlmostEqual(row['halfwidth_percent'], 3.5)
                    self.assertEqual(row['verdict'], verdict)

    def test_ab_noisy_one_arm_variance(self):
        with tempfile.TemporaryDirectory() as directory:
            result = compare_perf.compare_ab(make_ab(pathlib.Path(directory), cur_values=[9, 11] * 4))
            self.assertEqual((result['verdict'], result['exit_code']), ('NOISY', 2))
            self.assertIn('CV 比', ' '.join(result['warnings']))
            self.assertIsNone(result['quality'][0]['cv_ratio'])
            json.dumps(result, allow_nan=False)

    def test_ab_cv_ratio_ignores_variation_below_floor(self):
        with tempfile.TemporaryDirectory() as directory:
            result = compare_perf.compare_ab(make_ab(pathlib.Path(directory), cur_values=[9.9, 10.1] * 4))
            self.assertEqual(result['verdict'], 'OK')
            self.assertEqual(result['quality'][0]['problems'], [])

    def test_ab_max_cv_ratio_is_configurable(self):
        with tempfile.TemporaryDirectory() as directory:
            path = make_ab(pathlib.Path(directory), ref_values=[9.8, 10.2] * 4,
                           cur_values=[9, 11] * 4)
            self.assertEqual(compare_perf.compare_ab(path)['verdict'], 'NOISY')
            result = compare_perf.compare_ab(path, max_cv_ratio=6)
            self.assertEqual(result['verdict'], 'OK')
            self.assertAlmostEqual(result['quality'][0]['cv_ratio'], 5)
            self.assertEqual(set(result['quality'][0]), {'name', 'cv', 'cv_ratio', 'problems'})

    def test_ab_warn_has_priority_over_quality(self):
        with tempfile.TemporaryDirectory() as directory:
            result = compare_perf.compare_ab(make_ab(pathlib.Path(directory), cur_values=[9.45, 11.55] * 4))
            self.assertEqual((result['verdict'], result['exit_code']), ('WARN', 1))
            self.assertTrue(result['warnings'])

    def test_ab_asset_changes_and_machine_are_allowed_calib_is_diagnostic(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            path = make_ab(root)
            def update(doc):
                doc['conditions'].update(level_sha256='other', assets_sha256='other')
                doc['machine'].update(machine_id='other')
                doc['summary']['calib_ms'] = 60
                for run in doc['runs']:
                    run['calib_ms'] = 60
            self.change(root, 'cur', update)
            result = compare_perf.compare_ab(path)
            self.assertEqual(result['verdict'], 'OK')
            self.assertEqual(result['info'], ['アセットが変わった'])
            self.assertEqual(result['calib_ms'], {'ref': 30, 'cur': 60})

    def test_ab_amount_change_and_positive_control(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            path = make_ab(root)
            def change_amounts(doc):
                doc['summary']['amounts']['NumNodes'] = 21
                for run in doc['runs']:
                    if run['state'] == 'on':
                        run['amounts']['NumNodes'] = 21
            self.change(root, 'cur', change_amounts)
            self.assertEqual(compare_perf.compare_ab(path)['verdict'], 'WORKLOAD_CHANGED')
            positive = {'overrides': {'TargetFramerate': '120'}}
            self.change(root, 'cur', lambda doc: doc.update(positive_control=positive))
            ab = json.loads(path.read_text(encoding='utf-8'))
            ab['positive_control'] = positive
            path.write_text(json.dumps(ab), encoding='utf-8')
            self.assertEqual(compare_perf.compare_ab(path)['verdict'], 'OK')

    def test_ab_invalid_arm_audit_controls_and_summary(self):
        changes = [lambda d: d.update(schema='broken'), lambda d: d.update(audit=None),
                   lambda d: d['audit'].update(ok=False),
                   lambda d: d['summary']['controls'].update(negative_ok=False),
                   lambda d: d['summary']['controls'].update(amounts_ok=False),
                   lambda d: d['summary']['metrics']['kp_runtime_ms'].update(median=11),
                   lambda d: d['runs'][0].pop('index'),
                   lambda d: d['runs'][0].update(index=[])]
        for arm in ('ref', 'cur'):
            for i, update in enumerate(changes):
                with self.subTest(arm=arm, change=i), tempfile.TemporaryDirectory() as directory:
                    root = pathlib.Path(directory)
                    path = make_ab(root)
                    self.change(root, arm, update)
                    self.assertEqual(compare_perf.compare_ab(path)['exit_code'], 5)

    def test_ab_invalid_document_and_order(self):
        changes = [lambda d: d.update(schema='broken'), lambda d: d.update(blocks=3),
                   lambda d: d.update(order=[]), lambda d: d['order'][0].update(arm='cur'),
                   lambda d: d['order'][0].update(block=1),
                   lambda d: d['order'][0].update(ok=False)]
        keys = ('schema', 'blocks', 'arms', 'order')
        changes += [lambda d, key=key: d.pop(key) for key in keys]
        for i, update in enumerate(changes):
            with self.subTest(change=i), tempfile.TemporaryDirectory() as directory:
                path = make_ab(pathlib.Path(directory))
                ab = json.loads(path.read_text(encoding='utf-8'))
                update(ab)
                path.write_text(json.dumps(ab), encoding='utf-8')
                self.assertEqual(compare_perf.compare_ab(path)['exit_code'], 5)

    def test_ab_run_count_must_match_order(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            path = make_ab(root)
            def update(doc):
                doc['conditions']['runs_per_state'] = 7
                doc['runs'] = doc['runs'][:-2]
                for metric in doc['summary']['metrics'].values():
                    metric['values'] = metric['values'][:-1]
            self.change(root, 'cur', update)
            result = compare_perf.compare_ab(path)
            self.assertEqual(result['verdict'], 'INVALID')
            self.assertIn('cur の runs が order と一致しません', result['warnings'][0])

    def test_ab_zero_reference_is_na(self):
        with tempfile.TemporaryDirectory() as directory:
            result = compare_perf.compare_ab(make_ab(pathlib.Path(directory), ref_values=[0] * 8))
            self.assertEqual(result['verdict'], 'OK')
            self.assertTrue(all(row['verdict'] == 'N/A' for row in result['metrics']))

    def test_ab_cli_exclusions_and_json_output(self):
        with mock.patch('sys.stdout'), mock.patch('sys.stderr'):
            for options in (['current.json', '--ab', 'ab.json'], ['--ab', 'ab.json', '--update-baseline']):
                with self.subTest(options=options), self.assertRaises(SystemExit) as error:
                    compare_perf.main(options)
                self.assertEqual(error.exception.code, 2)
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            path = make_ab(root, cur_values=[10.5] * 8)
            with mock.patch('sys.stdout'):
                self.assertEqual(compare_perf.main(['--ab', str(path), '--json-out', str(root / 'report.json')]), 1)
            result = json.loads((root / 'report.json').read_text(encoding='utf-8'))
            self.assertEqual(result['verdict'], 'WARN')
            for delta in result['metrics'][0]['block_deltas_percent']:
                self.assertAlmostEqual(delta, 5)

    def test_ab_t_table_and_large_sample_approximation(self):
        self.assertEqual(compare_perf.t_critical(2), 12.706)
        self.assertEqual(compare_perf.t_critical(4), 3.182)
        self.assertEqual(compare_perf.t_critical(31), 2.042)
        self.assertEqual(compare_perf.t_critical(32), 1.96)


if __name__ == '__main__':
    unittest.main()
