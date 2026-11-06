"""Tests for how scan_samples applies known_issues.json."""

import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

import scan_samples  # noqa: E402


def make_report(violations=(), anim_class='ABP_Example_C', severe_log=()):
    return {'level': '/Game/L_Test', 'severe_log': list(severe_log), 'violations': [],
            'cells': {'cell': {'anim_class': anim_class, 'violations': list(violations)}}}


class ApplyKnownIssuesTest(unittest.TestCase):
    def test_listed_violation_moves_out_of_the_failures(self):
        report = make_report(['jump'])
        stale = scan_samples.apply_known_issues(
            [report], [{'anim_class': 'ABP_Example_C', 'violation': 'jump', 'reason': 'x'}])
        self.assertEqual(report['cells']['cell']['violations'], [])
        self.assertEqual(report['cells']['cell']['known_violations'], ['jump'])
        self.assertEqual(stale, [])

    def test_entry_only_covers_its_own_violation_and_class(self):
        report = make_report(['jump', 'still'])
        scan_samples.apply_known_issues(
            [report], [{'anim_class': 'ABP_Example_C', 'violation': 'jump', 'reason': 'x'},
                       {'anim_class': 'ABP_Other_C', 'violation': 'still', 'reason': 'x'}])
        self.assertEqual(report['cells']['cell']['violations'], ['still'])

    def test_unmatched_entries_are_returned_as_stale(self):
        entry = {'anim_class': 'ABP_Gone_C', 'violation': 'jump', 'reason': 'x'}
        self.assertEqual(scan_samples.apply_known_issues([make_report()], [entry]), [entry])

    def test_unlisted_log_line_stays_a_violation(self):
        report = make_report(severe_log=['LogKawaiiPhysics: Warning: new problem'])
        scan_samples.apply_known_issues([report], [{'log': 'old problem', 'reason': 'x'}])
        self.assertEqual(report['violations'], ['severe_log'])

    def test_listed_log_line_is_explained(self):
        report = make_report(severe_log=['LogKawaiiPhysics: Warning: old problem here'])
        stale = scan_samples.apply_known_issues([report], [{'log': 'old problem', 'reason': 'x'}])
        self.assertEqual(report['violations'], [])
        self.assertEqual(len(report['known_log']), 1)
        self.assertEqual(stale, [])


class FindLevelsTest(unittest.TestCase):
    def test_only_numbered_example_levels_are_found(self):
        levels = scan_samples.find_levels()
        self.assertTrue(any(level.endswith('L_KPS_01_BoneChain') for level in levels))
        self.assertFalse(any('/Common/' in level or 'AllExamples' in level for level in levels))


if __name__ == '__main__':
    unittest.main()
