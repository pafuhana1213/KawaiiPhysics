"""Run a KawaiiPhysics verification manifest headlessly and report the outcome."""

from __future__ import annotations

import argparse
import ctypes
import pathlib
import re
import subprocess
import sys
import time
from ctypes import wintypes

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
PROJECT = REPO_ROOT / 'KawaiiPhysicsSample.uproject'
DEFAULT_ENGINE = pathlib.Path('D:/EpicGames/UE_5.8')
DEFAULT_MAP = '/Engine/Maps/Entry'
SEVERE_LOG = re.compile(r'(Error:|Ensure condition failed|Fatal error|LogKawaiiPhysics: Warning)')
# UE 5.8 experimental toolset plugins fail their Python imports under -game; unrelated to the run.
IGNORED_LOG = re.compile(r'LogPython: Error:')


def _set_affinity(process, mask):
    kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel32.SetProcessAffinityMask.argtypes = [wintypes.HANDLE, ctypes.c_size_t]
    kernel32.SetProcessAffinityMask.restype = wintypes.BOOL
    if not kernel32.SetProcessAffinityMask(int(process._handle), mask):
        raise ctypes.WinError(ctypes.get_last_error())


def run(manifest, output, engine=DEFAULT_ENGINE, level=DEFAULT_MAP, fps=60, timeout=600,
        extra_arguments=(), affinity_mask=None, render=False, resolution=(320, 180),
        capture_directory=None):
    """Launch one process and return its exit code, seconds, and severe log lines.

    render keeps the renderer alive off screen. Wind sources live in the renderer scene, so
    anything using them needs it; everything else should stay on the cheaper -nullrhi.
    capture_directory saves every recorded frame there as a PNG and needs render.
    """
    manifest, output = pathlib.Path(manifest).resolve(), pathlib.Path(output).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.unlink(missing_ok=True)
    log_path = output.with_suffix('.log')
    command = [str(pathlib.Path(engine) / 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'),
               str(PROJECT), level, '-game',
               *(['-RenderOffscreen', f'-ResX={resolution[0]}', f'-ResY={resolution[1]}']
                 if render else ['-nullrhi']),
               *([f'-KPVerifyCapture={pathlib.Path(capture_directory).resolve()}']
                 if capture_directory else []),
               '-unattended', '-nosplash', '-nosound',
               '-nopause', '-deterministic', f'-FPS={fps}', f'-KPVerify={manifest}',
               f'-KPVerifyOut={output}', f'-abslog={log_path}', *extra_arguments]
    started = time.perf_counter()
    process = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        if affinity_mask is not None:
            _set_affinity(process, affinity_mask)
        exit_code = process.wait(timeout=timeout)
    except BaseException:
        process.kill()
        process.wait()
        raise
    seconds = time.perf_counter() - started
    log_text = log_path.read_text(encoding='utf-8', errors='replace') if log_path.exists() else ''
    severe = [line for line in log_text.splitlines()
              if SEVERE_LOG.search(line) and not IGNORED_LOG.search(line)]
    return {'exit_code': exit_code, 'seconds': seconds, 'output': str(output),
            'recorded': output.exists(), 'severe_log': severe}


def main():
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest')
    parser.add_argument('output', help='JSONL recording path; the log lands next to it.')
    parser.add_argument('--engine', default=str(DEFAULT_ENGINE))
    parser.add_argument('--level', default=DEFAULT_MAP)
    parser.add_argument('--fps', type=int, default=60)
    parser.add_argument('--timeout', type=int, default=600)
    parser.add_argument('--capture', help='Save every recorded frame to this directory.')
    parser.add_argument('--resolution', type=int, nargs=2, default=(320, 180), metavar=('X', 'Y'))
    arguments, extra = parser.parse_known_args()
    outcome = run(arguments.manifest, arguments.output, arguments.engine, arguments.level,
                  arguments.fps, arguments.timeout, extra, render=bool(arguments.capture),
                  resolution=arguments.resolution, capture_directory=arguments.capture)
    print(f"exit={outcome['exit_code']} seconds={outcome['seconds']:.1f} "
          f"recorded={outcome['recorded']} severe={len(outcome['severe_log'])}")
    for line in outcome['severe_log'][:40]:
        print(line)
    return 0 if outcome['exit_code'] == 0 and outcome['recorded'] else 1


if __name__ == '__main__':
    sys.exit(main())
