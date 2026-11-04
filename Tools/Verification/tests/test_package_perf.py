"""Tests for package identity, UAT commands, smoke checks, and cache publication."""

import copy
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import package_perf
import run_perf


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = pathlib.Path(self.temporary.name).resolve()
        self.source, self.engine, self.out = (self.root / name for name in ('source', 'engine', 'out'))
        self.suite = {'level': '/KawaiiPhysicsDev/Levels/L_KPV_Performance', 'fps': 60,
                      'settings': {'max_substeps': 4},
                      'grid': {'origin': [0, 0, 0], 'spacing': [100, 100, 0], 'columns': 3},
                      'fixtures': {'one': {'mesh': '/Game/Mesh/A.A',
                                          'anim_class': '/KawaiiPhysicsDev/ABP/B.B_C'},
                                   'two': {'mesh': '/Game/Mesh/A.A', 'anim_class': '/Game/ABP/C.C_C'}},
                      'groups': [{'fixture': 'one', 'count': 5}]}
        self.source.mkdir()
        self.suite_path = self.source / 'suite.json'
        self.suite_path.write_text(json.dumps(self.suite), encoding='utf-8')
        version = self.engine / 'Engine/Build/Build.version'
        version.parent.mkdir(parents=True)
        version.write_text(json.dumps({'MajorVersion': 5, 'MinorVersion': 8, 'PatchVersion': 4,
                                      'Changelist': 123, 'BranchName': 'branch', 'Ignored': 1}))
        self.signatures = {'runner_cpp_sha256': 'runner', 'level_sha256': 'level',
                           'assets_sha256': 'assets', 'asset_count': 3}

    def git_result(self, command, **kwargs):
        self.assertEqual(kwargs['cwd'], self.source)
        self.assertNotIn('shell', kwargs)
        if command[1:3] == ['rev-parse', 'HEAD']:
            stdout = b'a' * 40 + b'\n'
        elif command[1:3] == ['rev-parse', 'HEAD:Plugins/KawaiiPhysics']:
            stdout = b'b' * 40 + b'\n'
        else:
            self.assertEqual(command[-len(package_perf.TARGET_PATHS):], list(package_perf.TARGET_PATHS))
            stdout = b''
        return subprocess.CompletedProcess(command, 0, stdout=stdout)

    def identity(self):
        with mock.patch.object(package_perf.subprocess, 'run', side_effect=self.git_result):
            return package_perf.make_identity(self.source, self.engine, self.suite_path, self.suite)

    def test_cookdir_uses_absolute_mapped_sorted_unique_directories(self):
        directories = package_perf.cook_directories(self.suite, self.source)
        expected = {self.source / 'Content/Mesh', self.source / 'Content/ABP',
                    self.source / 'Plugins/KawaiiPhysicsDev/Content/ABP',
                    self.source / 'Plugins/KawaiiPhysicsDev/Content/Levels'}
        self.assertEqual(set(directories), expected)
        self.assertEqual(directories, sorted(expected, key=str))
        self.assertTrue(all(path.is_absolute() for path in directories))
        command = package_perf.build_command(self.source, self.engine, self.out, self.suite)
        self.assertIn('-cookdir=' + '+'.join(map(str, directories)), command)

    def test_buildcookrun_complete_argument_order(self):
        command = package_perf.build_command(self.source, self.engine, self.out, self.suite)
        self.assertEqual(command, [str(self.engine / 'Engine/Build/BatchFiles/RunUAT.bat'),
            'BuildCookRun', f'-project={self.source / "KawaiiPhysicsSample.uproject"}',
            '-noP4', '-platform=Win64', '-clientconfig=Development', '-build', '-cook', '-stage',
            '-pak', '-archive', f'-archivedirectory={self.out / "archive"}',
            '-map=/KawaiiPhysicsDev/Levels/L_KPV_Performance',
            '-cookdir=' + '+'.join(map(str, package_perf.cook_directories(self.suite, self.source))),
            '-unattended', '-utf8output'])

    def test_identity_variant_dirty_destination_and_opt_in(self):
        identity = self.identity()
        self.assertEqual(identity['commit'], 'a' * 40)
        self.assertFalse(identity['dirty'])
        self.assertIsNone(identity['dirty_sha256'])
        self.assertEqual(set(identity['engine_version']), set(package_perf.ENGINE_FIELDS))
        self.assertEqual(identity['build_arguments'], package_perf.build_arguments(self.suite))
        self.assertEqual(identity['suite_sha256'], run_perf.digest_bytes([self.suite_path.read_bytes()]))
        identifier = package_perf.package_id(identity)
        self.assertEqual(identifier, 'a' * 12 + '-' + run_perf.condition_key(identity)[:8])
        self.assertNotEqual(identifier, package_perf.package_id({**identity, 'variant': 'aa'}))
        self.assertEqual(package_perf.destination(self.out, identity), self.out / identifier)
        identity.update(dirty=True, dirty_sha256='changed')
        with self.assertRaisesRegex(ValueError, 'allow-dirty'):
            package_perf.destination(self.out, identity)
        self.assertEqual(package_perf.destination(self.out, identity, True).parent, self.out / 'dirty')

    def test_dirty_digest_tracks_binary_diff_untracked_names_and_contents(self):
        tracked = self.source / 'Content'
        tracked.mkdir()
        untracked = tracked / 'new.uasset'
        untracked.write_bytes(b'first')
        def identity(diff, name=b'Content/new.uasset\0'):
            with mock.patch.object(package_perf.subprocess, 'run', side_effect=[
                subprocess.CompletedProcess([], 0, stdout=b'a' * 40),
                subprocess.CompletedProcess([], 0, stdout=diff),
                subprocess.CompletedProcess([], 0, stdout=name)]):
                return package_perf.make_identity(self.source, self.engine, self.suite_path, self.suite)
        first = identity(b'\x00binary\n')
        self.assertTrue(first['dirty'])
        self.assertNotEqual(first['dirty_sha256'], identity(b'\x00binary')['dirty_sha256'])
        self.assertTrue(identity(b'')['dirty'])
        untracked.write_bytes(b'second')
        self.assertNotEqual(first['dirty_sha256'], identity(b'\x00binary\n')['dirty_sha256'])
        other = tracked / 'other.uasset'
        other.write_bytes(b'second')
        self.assertNotEqual(identity(b'')['dirty_sha256'],
                            identity(b'', b'Content/other.uasset\0')['dirty_sha256'])
        self.assertTrue(identity(b'only diff', b'')['dirty'])

    def test_executable_requires_exactly_one_match(self):
        archive = self.out / 'archive'
        with self.assertRaises(ValueError):
            package_perf.find_executable(archive)
        first = archive / 'Windows/KawaiiPhysicsSample/Binaries/Win64/KawaiiPhysicsSample.exe'
        first.parent.mkdir(parents=True)
        first.touch()
        self.assertEqual(package_perf.find_executable(archive), first)
        second = archive / 'Other/Binaries/Win64/KawaiiPhysicsSample.exe'
        second.parent.mkdir(parents=True)
        second.touch()
        with self.assertRaises(ValueError):
            package_perf.find_executable(archive)

    def test_build_arguments_and_executable_for_test_config(self):
        self.assertIn('-clientconfig=Test', package_perf.build_arguments(self.suite, 'Test'))
        with self.assertRaises(ValueError):
            package_perf.build_arguments(self.suite, 'Shipping')
        archive = self.out / 'archive'
        executable = archive / 'Windows/KawaiiPhysicsSample/Binaries/Win64/KawaiiPhysicsSample-Win64-Test.exe'
        executable.parent.mkdir(parents=True)
        executable.touch()
        self.assertEqual(package_perf.find_executable(archive, 'Test'), executable)

    def test_smoke_manifest_and_rejection_rules(self):
        manifest = package_perf.smoke_manifest(self.suite)
        small = copy.deepcopy(self.suite)
        small['groups'] = [{'fixture': name, 'count': 1} for name in small['fixtures']]
        self.assertEqual(manifest, {'frames': 30, 'record': False, 'settings': self.suite['settings'],
                                    'cells': run_perf.expand_suite(small)})
        outcome = {'exit_code': 0, 'recorded': True, 'severe_log': []}
        header = {'ok': True, 'build': 'game_development', 'kp_nodes': 2}
        package_perf.validate_smoke(outcome, header, 2)
        for change in ({'build': 'editor'}, {'kp_nodes': 1}, {'kp_nodes': True}, {'ok': False}):
            with self.subTest(change=change), self.assertRaises(ValueError):
                package_perf.validate_smoke(outcome, {**header, **change}, 2)
        for change in ({'exit_code': 1}, {'recorded': False}, {'severe_log': ['Error: bad']}):
            with self.subTest(change=change), self.assertRaises(ValueError):
                package_perf.validate_smoke({**outcome, **change}, header, 2)

    def fake_uat(self, command, **kwargs):
        archive = pathlib.Path(next(arg.split('=', 1)[1] for arg in command
                                     if arg.startswith('-archivedirectory=')))
        executable = archive / 'Windows/KawaiiPhysicsSample/Binaries/Win64/KawaiiPhysicsSample.exe'
        executable.parent.mkdir(parents=True)
        executable.touch()
        kwargs['stdout'].write(b'Using Visual Studio 2022 14.44.35999 toolchain (editor)\n')
        makefile = self.source / 'Intermediate/Build/Win64/x64/KawaiiPhysicsSample/Development/Makefile.bin'
        makefile.parent.mkdir(parents=True, exist_ok=True)
        makefile.write_bytes(b'\x00Using Visual Studio 14.44.35214 toolchain\x00')
        self.assertEqual(kwargs['cwd'], self.source)
        self.assertEqual(kwargs['stderr'], subprocess.STDOUT)
        self.assertNotIn('shell', kwargs)
        process = mock.Mock(pid=1234)
        def wait(timeout=None):
            self.assertEqual(timeout, 42)
            return 0
        process.wait.side_effect = wait
        return process

    def fake_smoke(self, manifest, output, **kwargs):
        self.assertTrue(kwargs['executable'].is_file())
        self.assertEqual(kwargs['level'], self.suite['level'])
        self.assertEqual(kwargs['fps'], 60)
        self.assertEqual(kwargs['timeout'], 42)
        self.assertNotIn('csv_capture', json.loads(manifest.read_text()))
        output.write_text(json.dumps({'ok': True, 'build': 'game_development', 'kp_nodes': 2}))
        return {'exit_code': 0, 'recorded': True, 'severe_log': []}

    def test_publish_metadata_cache_and_rebuild_preserves_previous(self):
        identity = self.identity()
        target = package_perf.destination(self.out, identity)
        target.mkdir(parents=True)
        old_package = json.dumps({'toolchain': 'Using Visual Studio 14.44.35214 toolchain'})
        (target / 'package.json').write_text(old_package)
        with mock.patch.object(package_perf.subprocess, 'run', side_effect=self.git_result) as process, \
                mock.patch.object(package_perf.subprocess, 'Popen', side_effect=self.fake_uat), \
                mock.patch.object(run_perf, 'source_signatures', return_value=self.signatures) as signatures, \
                mock.patch.object(package_perf.run_cells, 'run', side_effect=self.fake_smoke) as runner:
            self.assertEqual(package_perf.create_package(self.source, self.engine, self.suite_path,
                                                        self.out), target)
            signatures.assert_not_called()
            runner.assert_not_called()
            self.assertEqual(process.call_count, 3)
            actual = package_perf.create_package(self.source, self.engine, self.suite_path,
                                                   self.out, rebuild=True, timeout=42)
        self.assertEqual(actual, target)
        # ビルド前の取得と、公開前の再確認の 2 回。
        self.assertEqual(signatures.call_args_list, [mock.call(self.suite, self.source)] * 2)
        package = run_perf.load_package(target)
        self.assertEqual(set(package), {'schema', *run_perf.PACKAGE_FIELDS})
        self.assertEqual(package['identity'], identity)
        self.assertEqual(package['plugin_tree'], 'b' * 40)
        self.assertEqual(package['source'], str(self.source))
        self.assertEqual(package['executable'], 'Windows/KawaiiPhysicsSample/Binaries/Win64/KawaiiPhysicsSample.exe')
        self.assertIn('14.44.35214 toolchain', package['toolchain'])
        self.assertTrue((target / package['uat_log']).is_file())
        previous = list((self.out / '_work').glob('*/previous-package/package.json'))
        self.assertEqual(len(previous), 1)
        self.assertEqual(previous[0].read_text(), old_package)
        self.assertEqual(set(package['ddc']), {'UE-SharedDataCachePath', 'UE-LocalDataCachePath'})

    def test_cached_package_without_toolchain_requires_rebuild(self):
        target = package_perf.destination(self.out, self.identity())
        target.mkdir(parents=True)
        (target / 'package.json').write_text(json.dumps({'toolchain': None}))
        with mock.patch.object(package_perf.subprocess, 'run', side_effect=self.git_result), \
                mock.patch.object(package_perf.subprocess, 'Popen', side_effect=AssertionError('built')):
            with self.assertRaisesRegex(ValueError, '--rebuild'):
                package_perf.create_package(self.source, self.engine, self.suite_path, self.out)

    def test_toolchain_comes_only_from_game_makefile(self):
        with tempfile.TemporaryDirectory() as directory:
            source = pathlib.Path(directory)
            # Makefile が無ければ、ログに Editor の MSVC 行があっても使わない。
            with self.assertRaisesRegex(ValueError, 'MSVC'):
                package_perf.read_toolchain(source, 'Development')
            makefile = source / 'Intermediate/Build/Win64/x64/KawaiiPhysicsSample/Development/Makefile.bin'
            makefile.parent.mkdir(parents=True)
            makefile.write_bytes(b'\x00Using Clang 18.1.8 toolchain\x00')
            with self.assertRaisesRegex(ValueError, 'MSVC'):
                package_perf.read_toolchain(source, 'Development')
            makefile.write_bytes(b'\x00\x05Using Visual Studio 14.44.35222 toolchain\x00')
            self.assertEqual(package_perf.read_toolchain(source, 'Development'),
                             'Using Visual Studio 14.44.35222 toolchain')
            with self.assertRaisesRegex(ValueError, 'MSVC'):
                package_perf.read_toolchain(source, 'Test')

    def test_failed_uat_retains_work_and_never_runs_smoke(self):
        def fail(command, **kwargs):
            kwargs['stdout'].write(b'failure log')
            return mock.Mock(pid=1234, **{'wait.return_value': 7})
        with mock.patch.object(package_perf.subprocess, 'run', side_effect=self.git_result), \
                mock.patch.object(package_perf.subprocess, 'Popen', side_effect=fail), \
                mock.patch.object(run_perf, 'source_signatures', return_value=self.signatures), \
                mock.patch.object(package_perf.run_cells, 'run') as runner:
            with self.assertRaisesRegex(ValueError, '終了コード 7'):
                package_perf.create_package(self.source, self.engine, self.suite_path, self.out)
            runner.assert_not_called()
        logs = list((self.out / '_work').glob('*/uat.log'))
        self.assertEqual(len(logs), 1)
        self.assertEqual(logs[0].read_bytes(), b'failure log')
        self.assertEqual(list(self.out.glob('*/package.json')), [])

    def test_uat_timeout_kills_process_tree_and_raises(self):
        process = mock.Mock(pid=4321)
        process.wait.side_effect = [subprocess.TimeoutExpired('uat', 42), 0]
        commands = []
        def run(command, **kwargs):
            commands.append(command)
            return self.git_result(command, **kwargs) if command[0] == 'git' else \
                subprocess.CompletedProcess(command, 0)
        with mock.patch.object(package_perf.subprocess, 'run', side_effect=run), \
                mock.patch.object(package_perf.subprocess, 'Popen', return_value=process), \
                mock.patch.object(run_perf, 'source_signatures', return_value=self.signatures), \
                mock.patch.object(package_perf.run_cells, 'run') as runner:
            with self.assertRaisesRegex(ValueError, 'タイムアウト'):
                package_perf.create_package(self.source, self.engine, self.suite_path, self.out,
                                            timeout=42)
            runner.assert_not_called()
        self.assertIn(['taskkill', '/T', '/F', '/PID', '4321'], commands)
        self.assertEqual(process.wait.call_count, 2)

    def test_source_change_during_build_is_not_published(self):
        identity = self.identity()
        target = package_perf.destination(self.out, identity)
        changed = {**identity, 'dirty_sha256': 'changed'}
        with mock.patch.object(package_perf.subprocess, 'run', side_effect=self.git_result), \
                mock.patch.object(package_perf.subprocess, 'Popen', side_effect=self.fake_uat), \
                mock.patch.object(package_perf, 'make_identity', side_effect=[identity, changed]), \
                mock.patch.object(run_perf, 'source_signatures', return_value=self.signatures), \
                mock.patch.object(package_perf.run_cells, 'run', side_effect=self.fake_smoke):
            with self.assertRaisesRegex(ValueError, 'ビルド中にソースが変わりました'):
                package_perf.create_package(self.source, self.engine, self.suite_path, self.out,
                                            timeout=42)
        self.assertFalse(target.exists())
        self.assertEqual(len(list((self.out / '_work').glob('*/archive'))), 1)

    def test_cli_out_root_environment_required_and_error_codes(self):
        with mock.patch.dict(package_perf.os.environ, {}, clear=True), \
                mock.patch('sys.stdout'), mock.patch('sys.stderr'):
            with self.assertRaises(SystemExit) as error:
                package_perf.main([])
            self.assertEqual(error.exception.code, 2)
        with mock.patch.dict(package_perf.os.environ, {'KPV_PACKAGE_ROOT': str(self.out)}), \
                mock.patch.object(package_perf, 'create_package', return_value=self.out) as create, \
                mock.patch('sys.stdout'):
            self.assertEqual(package_perf.main([]), 0)
            self.assertEqual(create.call_args.args[3], self.out)
        with mock.patch.object(package_perf, 'create_package', side_effect=ValueError('失敗')), \
                mock.patch('sys.stdout'), mock.patch('sys.stderr'):
            self.assertEqual(package_perf.main(['--out-root', str(self.out)]), 1)


if __name__ == '__main__':
    unittest.main()
