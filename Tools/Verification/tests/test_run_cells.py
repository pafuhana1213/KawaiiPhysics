"""Tests for editor and packaged verification commands."""

import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import run_cells


class CommandTests(unittest.TestCase):
    def test_editor_command_is_unchanged(self):
        manifest, output = pathlib.Path('manifest.json').resolve(), pathlib.Path('out.jsonl').resolve()
        engine = pathlib.Path('engine')
        command = run_cells.build_command(manifest, output, engine, '/Test', 30,
                                          extra_arguments=['-extra'], cvars=['a.Test=0'])
        self.assertEqual(command, [str(engine / 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'),
            str(run_cells.PROJECT), '/Test', '-game', '-nullrhi', '-unattended', '-nosplash',
            '-nosound', '-nopause', '-deterministic', '-FPS=30', f'-KPVerify={manifest}',
            f'-KPVerifyOut={output}', f'-abslog={output.with_suffix(".log")}',
            '-dpcvars=a.Test=0', '-extra'])

    def test_packaged_command_starts_with_exe_and_level(self):
        executable = pathlib.Path('package/Game-Win64-Test.exe')
        command = run_cells.build_command('manifest.json', 'out.jsonl', level='/Test',
                                          executable=executable)
        self.assertEqual(command[:3], [str(executable), '/Test', '-nullrhi'])
        self.assertNotIn('-game', command)
        self.assertNotIn(str(run_cells.PROJECT), command)
        self.assertTrue(pathlib.Path(next(arg.split('=', 1)[1] for arg in command
                                         if arg.startswith('-KPVerify='))).is_absolute())

    def test_render_capture_and_extra_argument_order(self):
        capture = pathlib.Path('frames').resolve()
        command = run_cells.build_command('manifest.json', 'out.jsonl', render=True,
            resolution=(640, 360), capture_directory=capture, cvars=['a.One=1', 'a.Two=0'],
            executable='game.exe', extra_arguments=['-extra'])
        self.assertEqual(command[2:6], ['-RenderOffscreen', '-ResX=640', '-ResY=360',
                                       f'-KPVerifyCapture={capture}'])
        self.assertEqual(command[-2:], ['-dpcvars=a.One=1,a.Two=0', '-extra'])


if __name__ == '__main__':
    unittest.main()
