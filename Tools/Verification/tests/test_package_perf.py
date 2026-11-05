"""Tests for package metadata, UAT commands, and smoke checks."""

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
        self.status = b''
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
        if command[1:] == ['rev-parse', 'HEAD']:
            stdout = b'a' * 40 + b'\n'
        else:
            self.assertEqual(command[1:], ['status', '--porcelain', '--', *package_perf.TARGET_PATHS])
            stdout = self.status
        return subprocess.CompletedProcess(command, 0, stdout=stdout)

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
            '-pak', '-archive', f'-archivedirectory={self.out}',
            '-map=/KawaiiPhysicsDev/Levels/L_KPV_Performance',
            '-cookdir=' + '+'.join(map(str, package_perf.cook_directories(self.suite, self.source))),
            '-unattended', '-utf8output'])

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

    def test_package_metadata_and_dirty_status(self):
        fields = {'schema', 'created', 'source', 'commit', 'dirty', 'config', 'expected_build',
                  'executable', 'engine_version', 'suite_sha256', 'runner_cpp_sha256',
                  'level_sha256', 'assets_sha256', 'asset_count', 'toolchain', 'uat_log'}
        for dirty in (False, True):
            self.status = b' M Config/DefaultEngine.ini\n' if dirty else b''
            out = self.out / str(dirty)
            with self.subTest(dirty=dirty), \
                    mock.patch.object(package_perf.subprocess, 'run', side_effect=self.git_result) as git, \
                    mock.patch.object(package_perf.subprocess, 'Popen', side_effect=self.fake_uat), \
                    mock.patch.object(run_perf, 'source_signatures', return_value=self.signatures), \
                    mock.patch.object(package_perf.run_cells, 'run', side_effect=self.fake_smoke):
                actual = package_perf.create_package(self.source, self.engine, self.suite_path,
                                                    out, timeout=42)
            self.assertEqual(actual, out)
            self.assertEqual(git.call_count, 2)
            package = run_perf.load_package(out)
            self.assertEqual(set(package), fields)
            self.assertEqual(package['schema'], 'kpv-package/2')
            self.assertEqual(package['commit'], 'a' * 40)
            self.assertEqual(package['dirty'], dirty)
            self.assertEqual(package['source'], str(self.source))
            self.assertEqual(package['config'], 'Development')
            self.assertEqual(package['expected_build'], 'game_development')
            self.assertEqual(package['engine_version'], {
                'MajorVersion': 5, 'MinorVersion': 8, 'PatchVersion': 4,
                'Changelist': 123, 'BranchName': 'branch'})
            self.assertEqual(package['suite_sha256'], run_perf.digest_bytes([self.suite_path.read_bytes()]))
            self.assertTrue(package['created'])
            for key, value in self.signatures.items():
                self.assertEqual(package[key], value)
            self.assertEqual(package['executable'],
                             'Windows/KawaiiPhysicsSample/Binaries/Win64/KawaiiPhysicsSample.exe')
            self.assertIn('14.44.35214 toolchain', package['toolchain'])
            self.assertEqual(package['uat_log'], 'uat.log')
            self.assertIn(b'(editor)', (out / package['uat_log']).read_bytes())

    def test_package_accepts_missing_toolchain(self):
        with mock.patch.object(package_perf.subprocess, 'run', side_effect=self.git_result), \
                mock.patch.object(package_perf.subprocess, 'Popen', side_effect=self.fake_uat), \
                mock.patch.object(run_perf, 'source_signatures', return_value=self.signatures), \
                mock.patch.object(package_perf.run_cells, 'run', side_effect=self.fake_smoke), \
                mock.patch.object(package_perf, 'read_toolchain', return_value=None):
            package_perf.create_package(self.source, self.engine, self.suite_path, self.out, timeout=42)
        self.assertIsNone(run_perf.load_package(self.out)['toolchain'])

    def test_toolchain_comes_only_from_game_makefile(self):
        self.assertIsNone(package_perf.read_toolchain(self.source, 'Development'))
        makefile = self.source / 'Intermediate/Build/Win64/x64/KawaiiPhysicsSample/Development/Makefile.bin'
        makefile.parent.mkdir(parents=True)
        for data in (b'\x00Using Clang 18.1.8 toolchain\x00',
                     b'\x00Using Visual Studio toolchain\x00'):
            makefile.write_bytes(data)
            self.assertIsNone(package_perf.read_toolchain(self.source, 'Development'))
        makefile.write_bytes(b'\x00\x05Using Visual Studio 14.44.35222 toolchain\x00')
        self.assertEqual(package_perf.read_toolchain(self.source, 'Development'),
                         'Using Visual Studio 14.44.35222 toolchain')
        self.assertIsNone(package_perf.read_toolchain(self.source, 'Test'))
        with mock.patch.object(pathlib.Path, 'read_bytes', side_effect=PermissionError('busy')):
            self.assertIsNone(package_perf.read_toolchain(self.source, 'Development'))

    def test_failed_uat_retains_out_and_never_runs_smoke(self):
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
        self.assertEqual((self.out / 'uat.log').read_bytes(), b'failure log')
        self.assertFalse((self.out / 'package.json').exists())

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

    def test_existing_out_is_rejected_without_overwrite(self):
        for name in ('directory', 'file'):
            out = self.out / name
            out.parent.mkdir(parents=True, exist_ok=True)
            if name == 'directory':
                out.mkdir()
                marker = out / 'keep.txt'
            else:
                marker = out
            marker.write_text('keep', encoding='utf-8')
            with self.subTest(name=name), \
                    mock.patch.object(package_perf, 'run_uat') as uat, \
                    mock.patch('sys.stdout'), mock.patch('sys.stderr') as stderr:
                self.assertEqual(package_perf.main(['--out', str(out)]), 1)
                uat.assert_not_called()
                self.assertIn(str(out), ''.join(call.args[0] for call in stderr.write.call_args_list))
            self.assertEqual(marker.read_text(encoding='utf-8'), 'keep')

    def test_failed_smoke_keeps_archive_and_log(self):
        with mock.patch.object(package_perf.subprocess, 'run', side_effect=self.git_result), \
                mock.patch.object(package_perf.subprocess, 'Popen', side_effect=self.fake_uat), \
                mock.patch.object(run_perf, 'source_signatures', return_value=self.signatures), \
                mock.patch.object(package_perf, 'smoke', side_effect=ValueError('スモーク失敗')), \
                mock.patch('sys.stdout'), mock.patch('sys.stderr') as stderr:
            self.assertEqual(package_perf.main(['--out', str(self.out), '--source', str(self.source),
                '--engine', str(self.engine), '--suite', str(self.suite_path), '--timeout', '42']), 1)
            self.assertIn(str(self.out), ''.join(call.args[0] for call in stderr.write.call_args_list))
        self.assertTrue(package_perf.find_executable(self.out).is_file())
        self.assertTrue((self.out / 'uat.log').is_file())
        self.assertFalse((self.out / 'package.json').exists())

    def test_cli_out_required_and_options(self):
        with mock.patch('sys.stdout'), mock.patch('sys.stderr'):
            for options in ([], ['--out', str(self.out), '--timeout', '0']):
                with self.subTest(options=options), self.assertRaises(SystemExit) as error:
                    package_perf.main(options)
                self.assertEqual(error.exception.code, 2)
        with mock.patch.object(package_perf, 'create_package', return_value=self.out) as create, \
                mock.patch('sys.stdout'):
            self.assertEqual(package_perf.main(['--out', str(self.out), '--source', str(self.source),
                '--engine', str(self.engine), '--suite', str(self.suite_path), '--timeout', '42',
                '--config', 'Test']), 0)
            create.assert_called_once_with(self.source, self.engine, self.suite_path, self.out, 42, 'Test')


if __name__ == '__main__':
    unittest.main()
