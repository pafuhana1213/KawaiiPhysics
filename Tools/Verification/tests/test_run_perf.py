"""Unit tests for the CSV reader and performance suite expansion."""

from __future__ import annotations

import json
import pathlib
import sys
import tempfile
import unittest
from unittest import mock

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


class DisableTests(unittest.TestCase):
    def test_validate_disable_on_and_off(self):
        for state in ('on', 'off'):
            with self.subTest(state=state):
                run_perf.validate_disable({'kp_disabled_requested': state == 'off',
                                           'kp_nodes': 3,
                                           'kp_disabled_nodes': 3 if state == 'off' else 0}, state)

    def test_validate_disable_rejects_request_mismatch_and_non_bool(self):
        for state, requested in (('on', True), ('off', False), ('on', 0),
                                  ('off', 1), ('on', None), ('on', 'false')):
            with self.subTest(state=state, requested=requested):
                with self.assertRaisesRegex(ValueError, 'kp_disabled_requested'):
                    run_perf.validate_disable({'kp_disabled_requested': requested,
                                               'kp_nodes': 3, 'kp_disabled_nodes': 0}, state)

    def test_validate_disable_rejects_zero_and_non_integer_nodes(self):
        for nodes in (0, -1, True, False, 1.0):
            with self.subTest(nodes=nodes), self.assertRaisesRegex(ValueError, 'kp_nodes'):
                run_perf.validate_disable({'kp_disabled_requested': False,
                                           'kp_nodes': nodes, 'kp_disabled_nodes': 0}, 'on')

    def test_validate_disable_rejects_incomplete_off_and_disabled_on(self):
        for state, disabled in (('off', 2), ('on', 1)):
            with self.subTest(state=state), self.assertRaisesRegex(ValueError, 'kp_disabled_nodes'):
                run_perf.validate_disable({'kp_disabled_requested': state == 'off',
                                           'kp_nodes': 3, 'kp_disabled_nodes': disabled}, state)

    def test_validate_disable_rejects_bool_and_non_integer_disabled_count(self):
        for state, disabled in (('off', True), ('on', False), ('off', 1.0), ('on', 0.0)):
            with self.subTest(state=state, disabled=disabled):
                with self.assertRaisesRegex(ValueError, 'kp_disabled_nodes'):
                    run_perf.validate_disable({'kp_disabled_requested': state == 'off',
                                               'kp_nodes': 1, 'kp_disabled_nodes': disabled}, state)

    def test_validate_disable_reports_old_runner_for_missing_fields(self):
        runner = {'kp_disabled_requested': False, 'kp_nodes': 3, 'kp_disabled_nodes': 0}
        for field in runner:
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, 'ランナーが古い'):
                run_perf.validate_disable({key: value for key, value in runner.items()
                                           if key != field}, 'on')

    def test_runner_build_is_recorded_once_and_must_stay_the_same(self):
        engine = {'runner_build': None}
        run_perf.record_runner_build(engine, {'build': 'game_test'})
        run_perf.record_runner_build(engine, {'build': 'game_test'})
        self.assertEqual(engine['runner_build'], 'game_test')
        with self.assertRaisesRegex(ValueError, 'build が変わりました'):
            run_perf.record_runner_build(engine, {'build': 'editor'})
        for runner in ({}, {'build': None}, {'build': ''}, {'build': True}):
            with self.subTest(runner=runner), self.assertRaisesRegex(ValueError, 'build'):
                run_perf.record_runner_build(engine, runner)


class SuiteTests(unittest.TestCase):
    def setUp(self):
        self.suite = json.loads(run_perf.DEFAULT_SUITE.read_text(encoding='utf-8'))

    def test_expansion_and_expected_amounts(self):
        cells = run_perf.expand_suite(self.suite)
        self.assertEqual(len(cells), sum(group['count'] for group in self.suite['groups']))
        self.assertEqual(len({tuple(cell['location']) for cell in cells}), len(cells))
        self.assertEqual(run_perf.expected_amounts(self.suite)['NumNodes'], 55)
        self.assertNotIn('TargetFramerate', cells[0].get('overrides', {}))

    def test_off_manifest_flag_and_cvars_without_enable(self):
        cells = run_perf.expand_suite(self.suite)
        csv_path = pathlib.Path('captures') / 'capture.csv'
        for state in ('on', 'off'):
            manifest = run_perf.make_manifest(self.suite, cells, csv_path, state)
            if state == 'off':
                self.assertIs(manifest['disable_kawaii_physics'], True)
            else:
                self.assertNotIn('disable_kawaii_physics', manifest)
            self.assertEqual(manifest['cells'], cells)
            self.assertEqual(manifest['csv_capture']['filename'], csv_path.name)
            for profile in ('serial', 'parallel'):
                original = self.suite['profiles'][profile]['cvars']
                cvars = run_perf.make_cvars(original, state)
                self.assertEqual(cvars, original)
                self.assertIsNot(cvars, original)
                self.assertFalse(any(item.partition('=')[0] == 'a.AnimNode.KawaiiPhysics.Enable'
                                     for item in cvars))
        audit = run_perf.make_manifest(self.suite, cells, csv_path, 'on', audit=True)
        self.assertNotIn('disable_kawaii_physics', audit)
        self.assertEqual(audit['frames'], self.suite['audit']['frames'])
        self.assertEqual(audit['csv_capture']['frames'], self.suite['audit']['capture']['frames'])

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


class PackagePerfTests(unittest.TestCase):
    def package(self):
        return {'schema': run_perf.PACKAGE_SCHEMA, 'id': 'package-id',
                'identity': {'commit': '1234567890abcdef', 'dirty': True, 'dirty_sha256': 'dirty',
                             'engine_version': {}, 'build_arguments': [], 'suite_sha256': 'suite',
                             'variant': ''},
                'created': 'now', 'source': 'old-source', 'executable': 'game.exe',
                'runner_cpp_sha256': 'packaged-runner', 'level_sha256': 'packaged-level',
                'assets_sha256': 'packaged-assets', 'asset_count': 123,
                'plugin_tree': 'tree', 'toolchain': None, 'ddc': {}, 'uat_log': 'uat.log',
                'expected_build': 'game_development'}

    def test_package_conditions_use_stored_signatures_and_current_scripts(self):
        suite = json.loads(run_perf.DEFAULT_SUITE.read_text(encoding='utf-8'))
        package = self.package()
        with mock.patch.object(run_perf, 'source_signatures', side_effect=AssertionError('source read')):
            conditions = run_perf.conditions_for(suite, run_perf.DEFAULT_SUITE, 'serial', 2, package)
        for field in ('level_sha256', 'assets_sha256', 'asset_count'):
            self.assertEqual(conditions[field], package[field])
        self.assertEqual(conditions['runner_sha256'], run_perf.digest_bytes([
            package['runner_cpp_sha256'].encode('utf-8'), pathlib.Path(run_perf.__file__).read_bytes(),
            pathlib.Path(run_perf.run_cells.__file__).read_bytes()]))
        self.assertEqual(conditions['build'], 'game_development')
        self.assertNotIn('-game', conditions['extra_arguments'])
        editor = run_perf.conditions_for(suite, run_perf.DEFAULT_SUITE, 'serial', 2)
        self.assertEqual(editor['build'], 'editor')
        self.assertIn('-game', editor['extra_arguments'])
        self.assertIsNone(editor['toolchain'])

    def test_toolchain_version_extracts_first_dotted_version(self):
        self.assertEqual(run_perf.toolchain_version(
            'Using Visual Studio 2022 14.44.35222 toolchain (C:\\VS)'), '14.44.35222')
        self.assertIsNone(run_perf.toolchain_version(None))
        self.assertIsNone(run_perf.toolchain_version('Using Visual Studio toolchain'))

    def test_package_toolchain_changes_condition_key(self):
        suite = json.loads(run_perf.DEFAULT_SUITE.read_text(encoding='utf-8'))
        keys = []
        for toolchain in ('Using Visual Studio 2022 14.44.35214 toolchain',
                          'Using Visual Studio 2022 14.44.35222 toolchain', None):
            package = {**self.package(), 'toolchain': toolchain}
            conditions = run_perf.conditions_for(suite, run_perf.DEFAULT_SUITE, 'serial', 2, package)
            keys.append(run_perf.condition_key(conditions))
        self.assertEqual(len(set(keys)), 3)

    def test_provenance_comes_from_package(self):
        directory = pathlib.Path('package').resolve()
        with mock.patch.object(run_perf, 'provenance', side_effect=AssertionError('git read')):
            self.assertEqual(run_perf.package_provenance(self.package(), directory),
                {'commit': '12345678', 'dirty': True, 'plugin_diff_sha256': 'dirty',
                 'package_id': 'package-id', 'package_dir': str(directory)})

    def test_runner_build_must_match_conditions_for_both_modes(self):
        for expected, actual in (('editor', 'game_test'), ('game_test', 'editor')):
            with self.subTest(expected=expected), self.assertRaisesRegex(ValueError, 'build が条件と違います'):
                run_perf.record_runner_build({'runner_build': None}, {'build': actual}, expected)
            engine = {'runner_build': None}
            run_perf.record_runner_build(engine, {'build': expected}, expected)
            self.assertEqual(engine['runner_build'], expected)

    def test_package_schema_required_keys_and_relative_executable(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            (root / 'game.exe').touch()
            path = root / 'package.json'
            package = self.package()
            path.write_text(json.dumps(package), encoding='utf-8')
            self.assertEqual(run_perf.load_package(root), package)
            for field in run_perf.PACKAGE_FIELDS:
                invalid = {key: value for key, value in package.items() if key != field}
                path.write_text(json.dumps(invalid), encoding='utf-8')
                with self.subTest(field=field), self.assertRaises(ValueError):
                    run_perf.load_package(root)
            for change in ({'schema': 'other'}, {'identity': {}}, {'asset_count': True},
                           {'executable': '../game.exe'}, {'executable': str(root / 'game.exe')},
                           {'executable': 'missing.exe'}):
                path.write_text(json.dumps({**package, **change}), encoding='utf-8')
                with self.subTest(change=change), self.assertRaises(ValueError):
                    run_perf.load_package(root)

    def test_main_passes_packaged_executable_to_audit_and_measurements(self):
        suite = json.loads(run_perf.DEFAULT_SUITE.read_text(encoding='utf-8'))
        amounts = run_perf.expected_amounts(suite)
        def launch(manifest_path, output, **kwargs):
            manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
            count = manifest['csv_capture']['frames']
            nodes = len(manifest['cells'])
            off = manifest.get('disable_kawaii_physics', False)
            output.write_text(json.dumps({'ok': True, 'build': 'game_development',
                'settings': {**suite['settings'], 'parallel_anim_evaluation': 0, 'parallel_anim_update': 0},
                'kp_disabled_requested': off, 'kp_nodes': nodes, 'kp_disabled_nodes': nodes if off else 0,
                'effective_cvars': {'a.ParallelAnimEvaluation': '0', 'a.ParallelAnimUpdate': '0'},
                'csv': {'frames': count, 'path': str(output.with_suffix('.csv')), 'start_runner_frame': 0},
                'frame_ms': [10] * count, 'calib_start_ms': [1], 'calib_end_ms': [1]}), encoding='utf-8')
            return {'exit_code': 0, 'recorded': True, 'severe_log': [], 'seconds': 1}
        def csv_capture(path):
            count = suite['audit']['capture']['frames'] if path.name.startswith('audit_') else suite['capture']['frames']
            return [{}] * count, {'engineversion': '5.8', 'config': 'Test'}
        def aggregate(rows, profile, state):
            return ({name: 1 if state == 'on' else 0 for name in ('kp_runtime_ms', *run_perf.METRIC_STATS)},
                    amounts if state == 'on' else {name: 0 for name in amounts})
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            package_dir = root / 'package'
            package_dir.mkdir()
            (package_dir / 'game.exe').touch()
            package = self.package()
            package['identity']['suite_sha256'] = run_perf.digest_bytes([run_perf.DEFAULT_SUITE.read_bytes()])
            package['toolchain'] = 'Using Visual Studio 14.44.35222 toolchain'
            (package_dir / 'package.json').write_text(json.dumps(package), encoding='utf-8')
            with mock.patch.object(run_perf.run_cells, 'run', side_effect=launch) as runner, \
                    mock.patch.object(run_perf, 'machine_info', return_value={}), \
                    mock.patch.object(run_perf, 'on_ac_power', return_value=True), \
                    mock.patch.object(run_perf, 'read_csv_capture', side_effect=csv_capture), \
                    mock.patch.object(run_perf, 'aggregate_csv', side_effect=aggregate), \
                    mock.patch.object(run_perf, 'provenance', side_effect=AssertionError('git read')), \
                    mock.patch.object(run_perf, 'source_signatures', side_effect=AssertionError('source read')), \
                    mock.patch('sys.stdout'):
                self.assertEqual(run_perf.main([str(root / 'result'), '--package', str(package_dir), '--runs', '1']), 0)
            self.assertEqual(runner.call_count, len(suite['groups']) + 2)
            for call in runner.call_args_list:
                self.assertEqual(call.kwargs['executable'], package_dir / 'game.exe')
            result = json.loads((root / 'result/result.json').read_text(encoding='utf-8'))
            self.assertEqual(result['provenance'], run_perf.package_provenance(package, package_dir))
            self.assertEqual(result['engine']['root'], str(package_dir).replace('\\', '/'))
            self.assertEqual(result['engine']['runner_build'], 'game_development')

    def test_main_rejects_package_built_from_another_suite(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            (root / 'game.exe').touch()
            (root / 'package.json').write_text(json.dumps(self.package()), encoding='utf-8')
            with mock.patch.object(run_perf.run_cells, 'run', side_effect=AssertionError('launched')), \
                    mock.patch('sys.stdout'), mock.patch('sys.stderr'):
                self.assertEqual(run_perf.main([str(root / 'result'), '--package', str(root)]), 1)
            self.assertFalse((root / 'result').exists())

    def test_main_rejects_package_without_msvc_version(self):
        suite_sha256 = run_perf.digest_bytes([run_perf.DEFAULT_SUITE.read_bytes()])
        for toolchain in (None, 'Using Clang toolchain'):
            with self.subTest(toolchain=toolchain), tempfile.TemporaryDirectory() as directory:
                root = pathlib.Path(directory)
                (root / 'game.exe').touch()
                package = self.package()
                package['identity']['suite_sha256'] = suite_sha256
                package['toolchain'] = toolchain
                (root / 'package.json').write_text(json.dumps(package), encoding='utf-8')
                with mock.patch.object(run_perf.run_cells, 'run', side_effect=AssertionError('launched')), \
                        mock.patch('sys.stdout'), mock.patch('sys.stderr'):
                    self.assertEqual(run_perf.main([str(root / 'result'), '--package', str(root)]), 1)
                self.assertFalse((root / 'result').exists())


if __name__ == '__main__':
    unittest.main()
