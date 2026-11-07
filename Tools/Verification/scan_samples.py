"""Scan every example level and judge what is placed there with check_invariants.

Levels run with the renderer alive off screen by default: wind sources live in the renderer scene,
so under -nullrhi WindDirectionalSource wind silently does nothing and its cells stop moving.
A cell that never moves is therefore a violation unless known_issues.json explains it.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys
from collections import Counter

import check_invariants
import compare_cells
import run_cells

CONTENT = run_cells.REPO_ROOT / 'Content'
SAMPLE_ROOT = CONTENT / 'KawaiiPhysicsSample' / 'Examples'
KNOWN_ISSUES = pathlib.Path(__file__).with_name('known_issues.json')
# Common holds the shared stage and lighting.
SKIPPED_FOLDERS = {'Common'}
# A topic level streams its _Exhibits level, so scanning that alone would repeat its cells without a stage.
SKIPPED_SUFFIX = '_Exhibits'


def find_levels() -> list[str]:
    """Return the package paths of the sample levels, so new levels join the scan by themselves."""
    levels = []
    for path in sorted(SAMPLE_ROOT.rglob('L_*.umap')):
        if SKIPPED_FOLDERS & set(path.parts) or path.stem.endswith(SKIPPED_SUFFIX):
            continue
        levels.append('/Game/' + path.relative_to(CONTENT).with_suffix('').as_posix())
    return levels


def scan_level(level: str, manifest, directory: pathlib.Path, timeout: int,
               render: bool = True) -> dict:
    name = level.rsplit('/', 1)[-1]
    output = directory / f'{name}.jsonl'
    outcome = run_cells.run(manifest, output, level=level, timeout=timeout, render=render)
    report = {'level': level, 'exit_code': outcome['exit_code'], 'seconds': outcome['seconds'],
              'severe_log': outcome['severe_log'], 'cells': {}, 'violations': []}
    if outcome['exit_code'] != 0 or not outcome['recorded']:
        report['violations'].append('run_failed')
        return report
    report['cells'] = check_invariants.check_recording(compare_cells.load_recording(output))
    for cell in report['cells'].values():
        if cell.get('still'):
            cell['violations'].append('still')
    if not report['cells']:
        report['violations'].append('no_cells')
    return report


def apply_known_issues(reports: list[dict], entries: list[dict]) -> list[dict]:
    """Move explained findings out of the violations and return the entries nothing matched."""
    used = set()
    for report in reports:
        unexplained, explained = [], []
        for line in report['severe_log']:
            matches = [index for index, entry in enumerate(entries)
                       if 'check' not in entry and 'log' in entry and entry['log'] in line]
            used.update(matches)
            (explained if matches else unexplained).append(line)
        report['severe_log'], report['known_log'] = unexplained, explained
        if unexplained:
            report['violations'].append('severe_log')
        for cell in report['cells'].values():
            cell['known_violations'] = []
            for violation in list(cell['violations']):
                matches = [index for index, entry in enumerate(entries)
                           if 'check' not in entry and
                           entry.get('anim_class') == cell['anim_class'] and
                           entry.get('violation') == violation]
                if matches:
                    used.update(matches)
                    cell['violations'].remove(violation)
                    cell['known_violations'].append(violation)
    return [entry for index, entry in enumerate(entries)
            if index not in used and 'check' not in entry]


def main():
    sys.stdout.reconfigure(encoding='utf-8', errors='replace', line_buffering=True)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output_directory')
    parser.add_argument('--frames', type=int, default=300)
    parser.add_argument('--after-frames', type=int, default=60)
    parser.add_argument('--timeout', type=int, default=300)
    parser.add_argument('--level', action='append', help='Limit the scan to levels containing this text.')
    parser.add_argument('--nullrhi', action='store_true',
                        help='Skip the renderer. Wind sources stop working, so still cells appear.')
    arguments = parser.parse_args()

    directory = pathlib.Path(arguments.output_directory).resolve()
    directory.mkdir(parents=True, exist_ok=True)
    manifest = directory / 'scan_manifest.json'
    manifest.write_text(json.dumps({'frames': arguments.frames,
                                    'scan': {'after_frames': arguments.after_frames}}),
                        encoding='utf-8')

    levels = [level for level in find_levels()
              if not arguments.level or any(text in level for text in arguments.level)]
    reports = [scan_level(level, manifest, directory, arguments.timeout, not arguments.nullrhi)
               for level in levels]
    entries = json.loads(KNOWN_ISSUES.read_text(encoding='utf-8'))['entries']
    stale = apply_known_issues(reports, entries)
    # 一部のレベルだけの実行では、対象外の登録が使われないのは当然なので判定しない。
    if arguments.level or arguments.nullrhi:
        stale = []

    failed_cells = 0
    for report in reports:
        counts = Counter(violation for cell in report['cells'].values()
                         for violation in cell['violations'])
        known = sum(len(cell['known_violations']) for cell in report['cells'].values())
        failed_cells += sum(bool(cell['violations']) for cell in report['cells'].values())
        print(f"{report['level'].rsplit('/', 1)[-1]}: cells={len(report['cells'])} "
              f"level={report['violations']} violations={dict(counts)} known={known} "
              f"known_log={len(report['known_log'])} severe={len(report['severe_log'])} "
              f"({report['seconds']:.0f}s)")

    (directory / 'report.json').write_text(
        json.dumps({'levels': reports, 'stale_known_issues': stale}, indent=2, ensure_ascii=False),
        encoding='utf-8')
    for entry in stale:
        print('STALE known issue:', entry)
    failed = bool(stale or failed_cells or any(report['violations'] for report in reports))
    cells = sum(len(report['cells']) for report in reports)
    print('RESULT:', 'violations found' if failed else 'clean',
          f'({len(reports)} levels, {cells} cells, {failed_cells} failing)')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
