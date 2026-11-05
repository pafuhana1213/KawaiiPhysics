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
import compare_perf


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

    def test_validate_disable_rejects_request_mismatch(self):
        for state, requested in (('on', True), ('off', False)):
            with self.subTest(state=state, requested=requested):
                with self.assertRaisesRegex(ValueError, 'kp_disabled_requested'):
                    run_perf.validate_disable({'kp_disabled_requested': requested,
                                               'kp_nodes': 3, 'kp_disabled_nodes': 0}, state)

    def test_validate_disable_rejects_nonpositive_nodes(self):
        for nodes in (0, -1):
            with self.subTest(nodes=nodes), self.assertRaisesRegex(ValueError, 'kp_nodes'):
                run_perf.validate_disable({'kp_disabled_requested': False,
                                           'kp_nodes': nodes, 'kp_disabled_nodes': 0}, 'on')

    def test_validate_disable_rejects_incomplete_off_and_disabled_on(self):
        for state, disabled in (('off', 2), ('on', 1)):
            with self.subTest(state=state), self.assertRaisesRegex(ValueError, 'kp_disabled_nodes'):
                run_perf.validate_disable({'kp_disabled_requested': state == 'off',
                                           'kp_nodes': 3, 'kp_disabled_nodes': disabled}, state)


class SuiteTests(unittest.TestCase):
    def setUp(self):
        self.suite = json.loads(run_perf.DEFAULT_SUITE.read_text(encoding='utf-8'))

    def test_expansion_and_expected_amounts(self):
        cells = run_perf.expand_suite(self.suite)
        self.assertEqual(len(cells), sum(group['count'] for group in self.suite['groups']))
        self.assertEqual(len({tuple(cell['location']) for cell in cells}), len(cells))
        self.assertEqual(run_perf.expected_amounts(self.suite)['NumNodes'], 55)
        self.assertNotIn('TargetFramerate', cells[0].get('overrides', {}))

    def test_off_manifest_flag(self):
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
        actual = {'a.ParallelAnimEvaluation': '1.0', 'a.ParallelAnimUpdate': '1'}
        run_perf.validate_effective_cvars(actual, parallel)
        run_perf.validate_effective_cvars({**actual, 'a.ParallelAnimEvaluation': 'true'}, parallel)
        for changed, requested in ((actual, serial),
                                   ({**actual, 'a.ParallelAnimEvaluation': 'false'}, parallel),
                                   ({**actual, 'a.ParallelAnimUpdate': '0'}, parallel),
                                   ({**actual, 'a.ParallelAnimUpdate': None}, parallel),
                                   ({'a.ParallelAnimEvaluation': '1'}, parallel)):
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
        return {'schema': 'kpv-package/2', 'commit': '1234567890abcdef' + '0' * 24, 'dirty': True,
                'created': 'now', 'source': 'old-source', 'executable': 'game.exe',
                'config': 'Development', 'expected_build': 'game_development',
                'engine_version': {'MajorVersion': 5, 'MinorVersion': 8, 'PatchVersion': 4,
                                   'Changelist': 123, 'BranchName': 'branch'},
                'suite_sha256': 'suite', 'runner_cpp_sha256': 'packaged-runner',
                'level_sha256': 'packaged-level', 'assets_sha256': 'packaged-assets',
                'asset_count': 123, 'toolchain': None, 'uat_log': 'uat.log'}

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
            self.assertEqual(conditions['toolchain'], run_perf.toolchain_version(toolchain))
            keys.append(run_perf.condition_key(conditions))
        self.assertEqual(len(set(keys)), 3)

    def test_provenance_comes_from_package(self):
        directory = pathlib.Path('package').resolve()
        with mock.patch.object(run_perf, 'provenance', side_effect=AssertionError('git read')):
            self.assertEqual(run_perf.package_provenance(self.package(), directory),
                {'commit': '12345678', 'dirty': True, 'plugin_diff_sha256': None, 'package_dir': str(directory)})

    def test_runner_build_must_match_conditions_for_both_modes(self):
        for expected, actual in (('editor', 'game_test'), ('game_test', 'editor')):
            with self.subTest(expected=expected), self.assertRaisesRegex(ValueError, 'build が条件と違います'):
                run_perf.record_runner_build({'runner_build': None}, {'build': actual}, expected)
            engine = {'runner_build': None}
            run_perf.record_runner_build(engine, {'build': expected}, expected)
            self.assertEqual(engine['runner_build'], expected)

    def test_package_schema_and_executable_exists(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            (root / 'game.exe').touch()
            path = root / 'package.json'
            package = self.package()
            path.write_text(json.dumps(package), encoding='utf-8')
            self.assertEqual(run_perf.load_package(root), package)
            for change in ({'schema': 'kpv-package/1'}, {'executable': 'missing.exe'}):
                path.write_text(json.dumps({**package, **change}), encoding='utf-8')
                with self.subTest(change=change), self.assertRaises(ValueError):
                    run_perf.load_package(root)

    def test_main_passes_packaged_executable_to_audit_and_measurements(self):
        suite = json.loads(run_perf.DEFAULT_SUITE.read_text(encoding='utf-8'))
        amounts = run_perf.expected_amounts(suite)
        def launch(manifest_path, output, **kwargs):
            self.assertEqual(kwargs['cvars'], suite['profiles']['serial']['cvars'])
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
            package['suite_sha256'] = run_perf.digest_bytes([run_perf.DEFAULT_SUITE.read_bytes()])
            (package_dir / 'package.json').write_text(json.dumps(package), encoding='utf-8')
            with mock.patch.object(run_perf.run_cells, 'run', side_effect=launch) as runner, \
                    mock.patch.object(run_perf, 'machine_info', return_value={}), \
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
            self.assertIsNone(result['conditions']['toolchain'])

    def test_main_rejects_package_built_from_another_suite(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            (root / 'game.exe').touch()
            (root / 'package.json').write_text(json.dumps(self.package()), encoding='utf-8')
            with mock.patch.object(run_perf.run_cells, 'run', side_effect=AssertionError('launched')), \
                    mock.patch('sys.stdout'), mock.patch('sys.stderr'):
                self.assertEqual(run_perf.main([str(root / 'result'), '--package', str(root)]), 1)
            self.assertFalse((root / 'result').exists())


class AbRunPerfTests(unittest.TestCase):
    def test_abba_baab_order_and_mean_positions(self):
        order = [arm for b in range(4) for arm in run_perf.ab_arms(b)]
        self.assertEqual(order, ['ref', 'cur', 'cur', 'ref', 'cur', 'ref', 'ref', 'cur'] * 2)
        self.assertEqual(sum(i for i, arm in enumerate(order) if arm == 'ref'),
                         sum(i for i, arm in enumerate(order) if arm == 'cur'))

    def test_ab_arguments(self):
        cases = [('--reference-package', 'ref'), ('--blocks', '4'),
                 ('--package', 'cur', '--reference-package', 'ref', '--runs', '2')]
        cases += [('--package', 'cur', '--reference-package', 'ref', '--blocks', value)
                  for value in ('0', '1', '3', '-2')]
        with mock.patch('sys.stdout'), mock.patch('sys.stderr'), \
                mock.patch.object(run_perf.run_cells, 'run') as launch:
            for options in cases:
                with self.subTest(options=options), self.assertRaises(SystemExit) as error:
                    run_perf.main(['out', *options])
                self.assertEqual(error.exception.code, 2)
            launch.assert_not_called()

    def execute(self, root, *, blocks=2, fail_calls=(), positive=False, audit=True,
                invalid_call=None, same_package=False, toolchain='MSVC 14.44.35222'):
        suite = json.loads(run_perf.DEFAULT_SUITE.read_text(encoding='utf-8'))
        amounts = run_perf.expected_amounts(suite)
        for arm in ('ref', 'cur'):
            target = root / arm
            target.mkdir()
            package = PackagePerfTests().package()
            package['toolchain'] = toolchain
            package['suite_sha256'] = run_perf.digest_bytes([run_perf.DEFAULT_SUITE.read_bytes()])
            (target / 'game.exe').touch()
            (target / 'package.json').write_text(json.dumps(package), encoding='utf-8')
        manifests, executables = [], []
        def launch(manifest_path, output, **kwargs):
            self.assertEqual(kwargs['cvars'], suite['profiles']['serial']['cvars'])
            manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
            manifests.append((kwargs['executable'].parent.name, manifest))
            executables.append(kwargs['executable'])
            call = len(manifests)
            if call in fail_calls:
                return {'exit_code': 1, 'recorded': False, 'severe_log': [], 'seconds': 1}
            count = manifest['csv_capture']['frames']
            off = manifest.get('disable_kawaii_physics', False)
            output.with_suffix('.csv').write_text('mock CSV', encoding='utf-8')
            output.write_text(json.dumps({'ok': True, 'build': 'game_development',
                'settings': {**suite['settings'], 'parallel_anim_evaluation': 0, 'parallel_anim_update': 0},
                'effective_cvars': {'a.ParallelAnimEvaluation': '0', 'a.ParallelAnimUpdate': '0'},
                'kp_disabled_requested': off, 'kp_nodes': 1,
                'kp_disabled_nodes': 0 if call == invalid_call else int(off),
                'csv': {'frames': count, 'path': str(output.with_suffix('.csv')), 'start_runner_frame': 0},
                'frame_ms': [9 if off else 10] * count,
                'calib_start_ms': [1], 'calib_end_ms': [1]}), encoding='utf-8')
            return {'exit_code': 0, 'recorded': True, 'severe_log': [], 'seconds': 1}
        def csv_capture(path):
            count = suite['audit']['capture']['frames'] if path.name.startswith('audit_') else suite['capture']['frames']
            return [{}] * count, {'engineversion': '5.8', 'config': 'Development'}
        def aggregate(rows, profile, state):
            return ({name: int(state == 'on') for name in ('kp_runtime_ms', *run_perf.METRIC_STATS)},
                    amounts if state == 'on' else {name: 0 for name in amounts})
        options = [str(root / 'out'), '--package', str(root / 'cur'),
                   '--reference-package', str(root / ('cur' if same_package else 'ref'))]
        if blocks is not None:
            options += ['--blocks', str(blocks)]
        if positive:
            options += ['--positive-control']
        if not audit:
            options += ['--no-audit']
        with mock.patch.object(run_perf.run_cells, 'run', side_effect=launch), \
                mock.patch.object(run_perf, 'machine_info', return_value={
                    'machine_id': 'test', 'cpu': 'test', 'logical_cpus': 8, 'power_plan': 'test'}), \
                mock.patch.object(run_perf, 'read_csv_capture', side_effect=csv_capture), \
                mock.patch.object(run_perf, 'aggregate_csv', side_effect=aggregate), \
                mock.patch('sys.stdout'), mock.patch('sys.stderr'):
            code = run_perf.main(options)
        ab = json.loads((root / 'out/ab.json').read_text(encoding='utf-8'))
        results = {arm: json.loads((root / f'out/{arm}/result.json').read_text(encoding='utf-8'))
                   for arm in ('ref', 'cur')}
        return code, ab, results, manifests, executables

    def test_ab_main_default_blocks_outputs_order_and_audits(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            code, ab, results, manifests, executables = self.execute(root, blocks=None)
            self.assertEqual(code, 0)
            self.assertEqual(ab['blocks'], 4)
            self.assertEqual(len(ab['order']), 32)
            audit_frames = json.loads(run_perf.DEFAULT_SUITE.read_text(encoding='utf-8'))['audit']['capture']['frames']
            measurement_exes = [exe.parent.name for exe, (_, manifest) in zip(executables, manifests)
                                if manifest['csv_capture']['frames'] != audit_frames]
            self.assertEqual(measurement_exes,
                             [arm for b in range(4) for arm in run_perf.ab_arms(b) for _ in range(2)])
            for arm, result in results.items():
                self.assertEqual(result['schema'], 'kpv-perf-result/1')
                self.assertEqual(result['conditions']['runs_per_state'], 8)
                self.assertEqual([run['index'] for run in result['runs']], list(range(16)))
                self.assertTrue(result['audit']['ok'])
                self.assertEqual(result['provenance']['package_dir'], str(root / arm))
                self.assertEqual(len(list((root / 'out' / arm).glob('serial_*.json'))), 16)
                self.assertEqual(len(list((root / 'out' / arm).glob('serial_*.jsonl'))), 16)
                self.assertEqual(len(list((root / 'out' / arm).glob('serial_*.csv'))), 16)
                for run in result['runs']:
                    self.assertEqual(run['sequence'], ab['order'][run['sequence']]['sequence'])
                    self.assertIn('started', run)
                # 追加フィールドを含めて既存の result 検証を通る。
                self.assertEqual(compare_perf.validate(result, arm), [])
            self.assertEqual(compare_perf.compare_ab(root / 'out/ab.json')['verdict'], 'OK')

    def test_ab_same_package_is_allowed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            code, ab, results, _, executables = self.execute(root, same_package=True)
            self.assertEqual(code, 0)
            self.assertTrue(all(path == root / 'cur/game.exe' for path in executables))
            self.assertEqual(ab['arms']['ref']['package_dir'], ab['arms']['cur']['package_dir'])
            self.assertEqual(compare_perf.compare_ab(root / 'out/ab.json')['verdict'], 'OK')

    def test_ab_preflight_mismatch_does_not_launch(self):
        for field, value in (('expected_build', 'game_test'), ('runner_cpp_sha256', 'other'),
                             ('suite_sha256', 'other')):
            for arm in (0, 1):
                packages = [PackagePerfTests().package(), PackagePerfTests().package()]
                for package in packages:
                    package['suite_sha256'] = run_perf.digest_bytes([run_perf.DEFAULT_SUITE.read_bytes()])
                packages[arm][field] = value
                with self.subTest(field=field, arm=arm), \
                        mock.patch.object(run_perf, 'load_package', side_effect=packages), \
                        mock.patch.object(run_perf.run_cells, 'run') as launch, \
                        mock.patch('sys.stdout'), mock.patch('sys.stderr'):
                    self.assertEqual(run_perf.main(['out', '--package', 'cur', '--reference-package', 'ref']), 1)
                    launch.assert_not_called()

    def test_ab_accepts_missing_toolchain(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            code, _, results, _, _ = self.execute(root, toolchain=None)
            self.assertEqual(code, 0)
            self.assertTrue(all(result['conditions']['toolchain'] is None for result in results.values()))
            self.assertEqual(compare_perf.compare_ab(root / 'out/ab.json')['verdict'], 'OK')

    def test_ab_first_failure_stops_and_writes_failed_results(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            code, ab, results, manifests, _ = self.execute(root, fail_calls=(3,), audit=False)
            self.assertEqual(code, 1)
            self.assertEqual(len(manifests), 3)
            self.assertEqual([row['ok'] for row in ab['order']], [True, True, False])
            self.assertEqual(len(results['ref']['runs']), 2)
            self.assertEqual(results['cur']['runs'], [])
            for result in results.values():
                self.assertEqual(result['summary']['metrics'], {})
                self.assertFalse(result['summary']['controls']['negative_ok'])
                self.assertTrue(result['summary']['controls']['problems'])
            self.assertEqual(compare_perf.compare_ab(root / 'out/ab.json')['verdict'], 'INVALID')

    def test_ab_failure_in_later_block_preserves_completed_runs(self):
        suite = json.loads(run_perf.DEFAULT_SUITE.read_text(encoding='utf-8'))
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            failed_call = len(suite['groups']) * 2 + 12
            code, ab, results, manifests, _ = self.execute(root, fail_calls=(failed_call,))
            self.assertEqual(code, 1)
            self.assertEqual(len(manifests), failed_call)
            self.assertEqual(len(ab['order']), 12)
            self.assertFalse(ab['order'][-1]['ok'])
            for result in results.values():
                self.assertEqual([run['block'] for run in result['runs'][:4]], [0] * 4)
                self.assertFalse(result['summary']['controls']['negative_ok'])
                self.assertTrue(result['summary']['controls']['problems'])

    def test_ab_validation_failure_stops(self):
        with tempfile.TemporaryDirectory() as directory:
            code, ab, results, manifests, _ = self.execute(pathlib.Path(directory), audit=False, invalid_call=2)
            self.assertEqual(code, 1)
            self.assertEqual(len(ab['order']), 2)
            self.assertEqual(len(manifests), 2)
            self.assertFalse(ab['order'][-1]['ok'])
            self.assertTrue(all(not result['summary']['controls']['negative_ok']
                                for result in results.values()))

    def test_ab_positive_control_only_changes_cur(self):
        with tempfile.TemporaryDirectory() as directory:
            code, ab, results, manifests, _ = self.execute(pathlib.Path(directory), positive=True)
            self.assertEqual(code, 0)
            self.assertIsNone(results['ref']['positive_control'])
            self.assertEqual(results['cur']['positive_control'], ab['positive_control'])
            for arm, manifest in manifests:
                for cell in manifest['cells']:
                    self.assertEqual(cell.get('overrides', {}).get('TargetFramerate'),
                                     '120' if arm == 'cur' else None)

    def test_editor_provenance_and_missing_git(self):
        with mock.patch.object(run_perf.subprocess, 'run', side_effect=[
                mock.Mock(stdout=b'12345678\n'), mock.Mock(stdout=b' M file\n'),
                mock.Mock(stdout=b'diff\n')]) as git:
            self.assertEqual(run_perf.provenance(), {
                'commit': '12345678', 'dirty': True,
                'plugin_diff_sha256': run_perf.digest_bytes([b'diff'])})
            self.assertEqual(git.call_count, 3)
        with mock.patch.object(run_perf.subprocess, 'run', side_effect=OSError('git')):
            self.assertEqual(run_perf.provenance(), {
                'commit': None, 'dirty': None, 'plugin_diff_sha256': None})


if __name__ == '__main__':
    unittest.main()
