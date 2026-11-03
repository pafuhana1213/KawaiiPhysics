"""Run and judge a feature matrix, or rejudge existing version-two recordings."""

from __future__ import annotations

import argparse
import json
import pathlib
import sys

import baseline
import compare_cells
import judge_matrix
import matrix
import run_cells


KNOWN_ISSUES = pathlib.Path(__file__).with_name('known_issues.json')


def _failure_lines(report: dict) -> list[str]:
    lines = []
    for profile, cells in report['cells'].items():
        for cell_id, cell in cells.items():
            lines.extend(f'A {profile}/{cell_id}: {v}' for v in cell['violations'])
    for relation in report['relations']:
        if not relation['ok']:
            lines.append(f"B/C {relation['id']}: {relation['failure']}")
    lines.extend(f"実行 {v.get('profile', '')}: {v['violation']}" for v in report['run_violations'])
    for profile, comparison in report.get('baselines', {}).items():
        lines.extend(f'D {profile}/{cell_id}: baseline difference'
                     for cell_id, cell in comparison.get('cells', {}).items() if not cell['ok'])
        lines.extend(f'D {profile}/{cell_id}: missing' for cell_id in comparison.get('missing', []))
        lines.extend(f'D {profile}/{cell_id}: extra' for cell_id in comparison.get('extra', []))
    lines.extend(f"既知課題 stale: {entry['check']}" for entry in report.get('stale', []))
    return lines


def _markdown(report: dict, lines: list[str]) -> str:
    cells = sum(len(group) for group in report['cells'].values())
    a = sum(len(cell['violations']) for group in report['cells'].values()
            for cell in group.values())
    b = sum(not item['ok'] for item in report['relations']
            if item['kind'] in ('equal', 'profile_equal'))
    c = sum(not item['ok'] for item in report['relations']
            if item['kind'] not in ('equal', 'profile_equal'))
    d = sum(not cell['ok'] for group in report.get('baselines', {}).values()
            for cell in group.get('cells', {}).values())
    d += sum(len(group.get('missing', [])) + len(group.get('extra', []))
             for group in report.get('baselines', {}).values())
    text = ['# 機能検証レポート', '', '## 判定件数', '',
            f'- セル: {cells}', f'- A 絶対不変条件: {a} 件失敗',
            f'- B セル間の等価: {b} 件失敗', f'- C 機能の感度: {c} 件失敗',
            f'- D 基準値との比較: {d} 件失敗', '', '## 失敗一覧', '']
    text.extend(f'- {line}' for line in lines[:200])
    if len(lines) > 200:
        text.append(f'- ほか {len(lines) - 200} 件')
    if not lines:
        text.append('失敗なし')
    text.extend(['', '## 軸ごとの偏り', '',
                 '| 軸 | 値 | セル | 失敗セル | 失敗関係 | 最大伸長比 | 最大角度ステップ | 最大貫通 cm | 偏り |',
                 '| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |'])
    for axis, values in report['axes'].items():
        for value, item in values.items():
            number = lambda name: '' if item[name] is None else f'{item[name]:.3f}'
            text.append(f"| {axis} | {value} | {item['cells']} | {item['failed_cells']} | "
                        f"{item['failed_relations']} | {number('max_stretch_ratio')} | "
                        f"{number('max_step_degrees')} | {number('max_penetration_cm')} | "
                        f"{'あり' if item['biased'] else ''} |")
    return '\n'.join(text) + '\n'


def main(argv=None) -> int:
    """Execute selected profiles, apply known issues, and write both reports."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('spec')
    parser.add_argument('output')
    parser.add_argument('--profile', action='append')
    parser.add_argument('--only', action='append')
    parser.add_argument('--no-run', action='store_true')
    parser.add_argument('--update-baseline', action='store_true')
    parser.add_argument('--timeout', type=int, default=600)
    parser.add_argument('--no-baseline', action='store_true')
    args = parser.parse_args(argv)
    spec_path = pathlib.Path(args.spec)
    spec = json.loads(spec_path.read_text(encoding='utf-8'))
    expanded = matrix.expand_spec(spec)
    profiles = args.profile or ['base']
    for profile in profiles:
        if profile not in spec['profiles']:
            parser.error(f'Unknown profile: {profile}')
    output = pathlib.Path(args.output)
    output.mkdir(parents=True, exist_ok=True)
    if args.only:
        expanded['cells'] = [cell for cell in expanded['cells']
                             if any(text in cell['id'] for text in args.only)]
        selected = {cell['id'] for cell in expanded['cells']}
        expanded['tags'] = {key: value for key, value in expanded['tags'].items()
                            if key in selected}
        expanded['relations'] = [relation for relation in expanded['relations']
                                 if relation['a'] in selected and relation['b'] in selected]
    expanded['relations'] = [relation for relation in expanded['relations']
                             if relation.get('a_profile', relation['profile']) in profiles and
                             relation.get('b_profile', relation['profile']) in profiles]
    entries = json.loads(KNOWN_ISSUES.read_text(encoding='utf-8'))['entries']
    recordings, run_violations = {}, []
    for profile in profiles:
        recording_path = output / f'{profile}.jsonl'
        if not args.no_run:
            manifest_path = output / f'{profile}.manifest.json'
            manifest_path.write_text(json.dumps(matrix.profile_manifest(spec, expanded, profile),
                                                ensure_ascii=False, indent=2), encoding='utf-8')
            selected_profile = spec['profiles'][profile]
            outcome = run_cells.run(manifest_path, recording_path, level=spec['level'],
                                    fps=selected_profile.get('fps', 60),
                                    cvars=selected_profile.get('cvars', ()),
                                    render=spec.get('render', False), timeout=args.timeout)
            if outcome['exit_code'] != 0 or not outcome['recorded']:
                run_violations.append({'profile': profile, 'violation': 'run_failed',
                                       'exit_code': outcome['exit_code']})
            unexplained = [line for line in outcome['severe_log']
                           if not any('log' in entry and entry['log'] in line for entry in entries)]
            if unexplained:
                run_violations.append({'profile': profile, 'violation': 'severe_log',
                                       'lines': unexplained})
        if recording_path.exists():
            try:
                recordings[profile] = compare_cells.load_recording(recording_path)
            except (ValueError, KeyError, json.JSONDecodeError) as error:
                run_violations.append({'profile': profile, 'violation': 'recording_invalid',
                                       'error': str(error)})
        else:
            if args.no_run:
                run_violations.append({'profile': profile, 'violation': 'recording_missing'})
    judged = judge_matrix.judge(expanded, recordings)
    judged['run_violations'].extend(run_violations)
    stale = judge_matrix.apply_known_issues(judged, entries)
    judged['stale'] = [] if args.only or set(profiles) != set(spec['profiles']) else stale
    judged['axes'] = judge_matrix.axis_report(judged, expanded['tags'])
    ac_failures = _failure_lines(judged)
    judged['baselines'] = {}
    if not args.no_baseline:
        for profile, full_recording in recordings.items():
            # 風は乱数で毎回変わるので、基準値では比較できないセルを仕様で除外する。
            recording = {**full_recording, 'cells': {
                cell_id: cell for cell_id, cell in full_recording['cells'].items()
                if not any(text in cell_id for text in spec.get('baseline_except', []))}}
            path = baseline.baseline_path(spec_path.stem, profile)
            old = baseline.load(path) if path.exists() else None
            if args.update_baseline:
                if not ac_failures:
                    stride = old['meta']['stride'] if old else 30
                    fresh = baseline.make_baseline(recording, stride)
                    comparison = (baseline.compare_baseline(old, recording)
                                  if old else {'cells': {}, 'missing': [], 'extra': []})
                    judged.setdefault('baseline_updates', {})[profile] = comparison
                    changed = sum(not item['ok'] for item in comparison['cells'].values())
                    changed += len(comparison['missing']) + len(comparison['extra'])
                    print(f'BASELINE UPDATE: {profile}: {changed} changed or missing cells; '
                          f'{len(fresh)} cells written')
                    meta = {'engine': recording['run'].get('engine'),
                            'frames': spec['frames'], 'stride': stride}
                    baseline.save(path, fresh, meta)
            elif old:
                comparison = baseline.compare_baseline(old, recording)
                if args.only:
                    comparison['missing'] = []
                    comparison['extra'] = []
                judged['baselines'][profile] = comparison
            elif not args.only:
                judged['baselines'][profile] = {'cells': {}, 'missing': ['baseline file'],
                                                 'extra': []}
    lines = _failure_lines(judged)
    (output / 'report.json').write_text(json.dumps(judged, indent=2, ensure_ascii=False),
                                        encoding='utf-8')
    (output / 'report.md').write_text(_markdown(judged, lines), encoding='utf-8')
    cells = sum(len(group) for group in judged['cells'].values())
    status = 'clean' if not lines else 'violations found'
    print(f'RESULT: {status} ({cells} cells, {len(judged["relations"])} relations, '
          f'{len(lines)} failing)')
    return 1 if lines else 0


if __name__ == '__main__':
    sys.exit(main())
