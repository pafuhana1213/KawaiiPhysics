"""Tests for reversible mutation detection proofs."""

import json
import pathlib
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import prove_detection  # noqa: E402


class ProofTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = pathlib.Path(self.temporary.name)
        self.source = self.root / 'Plugins' / 'sample.cpp'
        self.source.parent.mkdir(parents=True)
        self.original = b'\xef\xbb\xbfalpha\r\n\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e\r\n'
        self.source.write_bytes(self.original)
        self.mutations = self.root / 'mutations.json'
        self.output = self.root / 'proof'

    def mutation(self, edits, identifier='old_bug'):
        return {'id': identifier, 'commit': 'abc', 'summary': 'old behavior',
                'edits': [{'file': 'Plugins/sample.cpp', 'old': old, 'new': new}
                          for old, new in edits]}

    def save(self, *mutations):
        self.mutations.write_text(json.dumps({'mutations': mutations}), encoding='utf-8')

    def run_proof(self, *arguments):
        return prove_detection.main([str(self.output), *arguments], root=self.root,
                                    mutations_path=self.mutations)

    def test_preflight_rejects_zero_or_two_matches_without_writing(self):
        for old in ('missing', '\r\n'):
            with self.subTest(old=old):
                self.save(self.mutation([(old, 'changed')]))
                with mock.patch.object(prove_detection, 'run_build') as build:
                    self.assertEqual(self.run_proof(), 2)
                build.assert_not_called()
                self.assertEqual(self.source.read_bytes(), self.original)
                self.assertFalse(self.output.exists())

    def test_edits_apply_in_order_and_restore_bom_crlf(self):
        self.save(self.mutation([('alpha', 'beta'), ('beta', 'gamma')]))
        seen = []

        def build(log_path):
            seen.append(self.source.read_bytes())
            return 0

        def matrix(suite, directory, profiles, timeout):
            self.assertEqual(profiles, ['base', 'fps45'])
            self.assertEqual(self.source.read_bytes(),
                             self.original.replace(b'alpha', b'gamma'))
            directory.mkdir(parents=True)
            (directory / 'report.json').write_text(json.dumps({'cells': {}}), encoding='utf-8')
            return 1

        with mock.patch.object(prove_detection, 'run_build', side_effect=build), \
             mock.patch.object(prove_detection, 'run_matrix', side_effect=matrix):
            self.assertEqual(self.run_proof(), 1)
        self.assertEqual(seen, [self.original.replace(b'alpha', b'gamma'), self.original])
        self.assertEqual(self.source.read_bytes(), self.original)
        self.assertEqual((self.output / 'backup/old_bug/Plugins/sample.cpp').read_bytes(),
                         self.original)
        self.assertEqual(self.proof()[0]['status'], 'missed')

    def test_matrix_exception_restores_source(self):
        self.save(self.mutation([('alpha', 'beta')]))
        with mock.patch.object(prove_detection, 'run_build', return_value=0), \
             mock.patch.object(prove_detection, 'run_matrix', side_effect=RuntimeError('failed')):
            with self.assertRaisesRegex(RuntimeError, 'failed'):
                self.run_proof()
        self.assertEqual(self.source.read_bytes(), self.original)

    def test_failure_is_detected(self):
        self.save(self.mutation([('alpha', 'beta')]))

        def matrix(suite, directory, profiles, timeout):
            directory.mkdir(parents=True)
            (directory / 'report.json').write_text(json.dumps({
                'cells': {'base': {'cell': {'violations': ['bad']}}},
                'relations': [{'id': 'relation', 'ok': False, 'failure': 'bad'}],
                'run_violations': [{'profile': 'base', 'violation': 'run_failed'}],
                'baselines': {'base': {'cells': {'cell': {'ok': False}}}}
            }), encoding='utf-8')
            return 1

        with mock.patch.object(prove_detection, 'run_build', return_value=0), \
             mock.patch.object(prove_detection, 'run_matrix', side_effect=matrix):
            self.assertEqual(self.run_proof(), 0)
        self.assertEqual(self.proof()[0]['status'], 'detected')
        self.assertEqual(self.proof()[0]['failure_count'], 4)

    def test_build_failure_skips_matrix_and_rebuilds_original(self):
        self.save(self.mutation([('alpha', 'beta')]))
        seen = []

        def build(log_path):
            seen.append(self.source.read_bytes())
            return 1 if len(seen) == 1 else 0

        with mock.patch.object(prove_detection, 'run_build', side_effect=build), \
             mock.patch.object(prove_detection, 'run_matrix') as matrix:
            self.assertEqual(self.run_proof(), 1)
        matrix.assert_not_called()
        self.assertEqual(seen, [self.original.replace(b'alpha', b'beta'), self.original])
        self.assertEqual(self.proof()[0]['status'], 'build_failed')

    def test_rebuild_once_after_multiple_mutations(self):
        self.save(self.mutation([('alpha', 'beta')], 'first'),
                  self.mutation([('日本語', '英語')], 'second'))
        seen = []

        def build(log_path):
            seen.append(self.source.read_bytes())
            return 1 if len(seen) < 3 else 0

        with mock.patch.object(prove_detection, 'run_build', side_effect=build):
            self.assertEqual(self.run_proof(), 1)
        self.assertEqual(len(seen), 3)
        self.assertEqual(seen[-1], self.original)
        self.assertEqual(self.source.read_bytes(), self.original)

    def proof(self):
        return json.loads((self.output / 'proof.json').read_text(encoding='utf-8'))['mutations']


if __name__ == '__main__':
    unittest.main()
