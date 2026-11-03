"""Temporarily restore old bugs and prove that the feature matrix detects them."""

from __future__ import annotations

import argparse
import json
import pathlib
import subprocess
import sys

import run_cells
import verify_matrix


DEFAULT_MUTATIONS = pathlib.Path(__file__).with_name('mutations.json')
DEFAULT_SUITE = pathlib.Path(__file__).with_name('suites') / 'feature_matrix.json'


def _decode(data: bytes) -> tuple[bytes, str]:
    """Decode UTF-8 while retaining an optional byte order mark."""
    bom = b'\xef\xbb\xbf' if data.startswith(b'\xef\xbb\xbf') else b''
    return bom, data[len(bom):].decode('utf-8')


def _prepare(mutation: dict, root: pathlib.Path) -> tuple[dict[pathlib.Path, bytes],
                                                              dict[pathlib.Path, bytes]]:
    """Validate edits in order and return original and mutated byte streams."""
    originals = {}
    changed = {}
    for index, edit in enumerate(mutation['edits'], 1):
        relative = pathlib.Path(edit['file'])
        path = (root / relative).resolve()
        if not path.is_relative_to(root.resolve()):
            raise ValueError(f"{mutation['id']} edit {index}: path leaves repository")
        try:
            if path not in originals:
                originals[path] = path.read_bytes()
                changed[path] = originals[path]
            bom, source = _decode(changed[path])
            count = source.count(edit['old'])
            if not edit['old'] or count != 1:
                raise ValueError(f"{mutation['id']} edit {index}: old occurs {count} times")
            changed[path] = bom + source.replace(edit['old'], edit['new'], 1).encode('utf-8')
        except (OSError, UnicodeError) as error:
            raise ValueError(f"{mutation['id']} edit {index}: {error}") from error
    return originals, changed


def run_build(log_path: pathlib.Path) -> int:
    """Build the sample editor and save its combined output."""
    log_path.parent.mkdir(parents=True, exist_ok=True)
    command = [str(run_cells.DEFAULT_ENGINE / 'Engine/Build/BatchFiles/Build.bat'),
               'KawaiiPhysicsSampleEditor', 'Win64', 'Development',
               f'-Project={run_cells.PROJECT}', '-WaitMutex', '-NoHotReload']
    with log_path.open('wb') as log:
        return subprocess.run(command, stdout=log, stderr=subprocess.STDOUT,
                              check=False).returncode


def run_matrix(suite: pathlib.Path, directory: pathlib.Path,
               profiles: list[str], timeout: int) -> int:
    """Run the matrix for one mutation."""
    arguments = [str(suite), str(directory)]
    for profile in profiles:
        arguments.extend(['--profile', profile])
    return verify_matrix.main([*arguments, '--timeout', str(timeout)])


def _failures(report: dict) -> list[str]:
    """Collect the matrix and baseline failures used as detection evidence."""
    failures = []
    for profile, cells in report.get('cells', {}).items():
        for cell_id, cell in cells.items():
            failures.extend(f'A {profile}/{cell_id}:{item}'
                            for item in cell.get('violations', []))
    failures.extend(f"R {item['id']}:{item['failure']}" for item in report.get('relations', [])
                    if not item['ok'])
    failures.extend(f"RUN {item.get('profile', '')}:{item['violation']}"
                    for item in report.get('run_violations', []))
    for profile, comparison in report.get('baselines', {}).items():
        failures.extend(f'D {profile}/{cell_id}' for cell_id, cell in
                        comparison.get('cells', {}).items() if not cell['ok'])
        failures.extend(f'D {profile}/{cell_id}' for cell_id in comparison.get('missing', []))
        failures.extend(f'D {profile}/{cell_id}' for cell_id in comparison.get('extra', []))
    return failures


def _execute(mutation: dict, root: pathlib.Path, output: pathlib.Path,
             suite: pathlib.Path, profiles: list[str], timeout: int,
             originals: dict[pathlib.Path, bytes], changed: dict[pathlib.Path, bytes]) -> dict:
    """Back up, apply, evaluate, and always restore one mutation."""
    identifier = mutation['id']
    directory = output / identifier
    for path, data in originals.items():
        backup = output / 'backup' / identifier / path.relative_to(root.resolve())
        backup.parent.mkdir(parents=True, exist_ok=True)
        backup.write_bytes(data)
    status = 'build_failed'
    failures = []
    try:
        for path, data in changed.items():
            path.write_bytes(data)
        if run_build(directory / 'build.log') == 0:
            run_matrix(suite, directory, profiles, timeout)
            report = json.loads((directory / 'report.json').read_text(encoding='utf-8'))
            failures = _failures(report)
            status = 'detected' if failures else 'missed'
    finally:
        for path, data in originals.items():
            path.write_bytes(data)
        for path, data in originals.items():
            if path.read_bytes() != data:
                raise RuntimeError(f'{identifier}: restoration failed for {path}')
    return {'id': identifier, 'commit': mutation['commit'], 'summary': mutation['summary'],
            'status': status, 'failure_count': len(failures), 'failures': failures[:20]}


def main(argv: list[str] | None = None, *, root: pathlib.Path | None = None,
         mutations_path: pathlib.Path | None = None) -> int:
    """Validate all selected edits before running mutations one at a time."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=pathlib.Path)
    parser.add_argument('--suite', type=pathlib.Path, default=DEFAULT_SUITE)
    parser.add_argument('--mutation', action='append')
    parser.add_argument('--profile', action='append')
    parser.add_argument('--timeout', type=int, default=600)
    parser.add_argument('--dry-run', action='store_true')
    args = parser.parse_args(argv)
    root = (root or run_cells.REPO_ROOT).resolve()
    source = mutations_path or DEFAULT_MUTATIONS
    mutations = json.loads(source.read_text(encoding='utf-8'))['mutations']
    if args.mutation:
        unknown = set(args.mutation) - {item['id'] for item in mutations}
        if unknown:
            parser.error(f'Unknown mutation: {", ".join(sorted(unknown))}')
        mutations = [item for item in mutations if item['id'] in args.mutation]
    prepared = []
    for mutation in mutations:
        try:
            originals, changed = _prepare(mutation, root)
        except ValueError as error:
            print(error, file=sys.stderr)
            return 2
        prepared.append((mutation, originals, changed))
    if args.dry_run:
        return 0
    profiles = args.profile or ['base', 'fps45']
    results = [_execute(item, root, args.output, args.suite, profiles, args.timeout,
                        originals, changed) for item, originals, changed in prepared]
    restored_build_failed = run_build(args.output / 'restored_build.log') != 0
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / 'proof.json').write_text(
        json.dumps({'mutations': results}, ensure_ascii=False, indent=2), encoding='utf-8')
    for item in results:
        print(f"{item['id']}: {item['status']} ({item['failure_count']} failures)")
    missed = [item['id'] for item in results if item['status'] != 'detected']
    print(f'RESULT: missed ({", ".join(missed)})' if missed else
          f'RESULT: all detected ({len(results)})')
    if restored_build_failed:
        print('Restored source build failed; see restored_build.log', file=sys.stderr)
        return 3
    return 1 if missed else 0


if __name__ == '__main__':
    sys.exit(main())
