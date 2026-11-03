"""Compressed trajectory baseline tests."""

import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import baseline  # noqa: E402
from test_check_invariants import make_cell  # noqa: E402


class BaselineTest(unittest.TestCase):
    def test_rounding_and_comparison(self):
        recording = {'cells': {'a': make_cell([[0.0004, 0, -10],
                                                [1, 0, -10], [2.0004, 0, -10]])}}
        cells = baseline.make_baseline(recording, stride=2)
        self.assertEqual(cells['a']['bones'], ['root', 'tip'])
        self.assertEqual(cells['a']['locations'][3], 0)
        self.assertEqual(cells['a']['locations'][9], 2)
        document = {'meta': {'engine': 'test', 'frames': 3, 'stride': 2}, 'cells': cells}
        self.assertTrue(baseline.compare_baseline(document, recording)['cells']['a']['ok'])
        changed = {'cells': {'a': make_cell([[0, 0, -10], [1, 0, -10],
                                             [2.2, 0, -10]])}}
        failure = baseline.compare_baseline(document, changed)['cells']['a']
        self.assertFalse(failure['ok'])
        self.assertEqual(failure['worst'], {'bone': 'tip', 'frame': 2})
        self.assertEqual(baseline.compare_baseline(document, {'cells': {'b': changed['cells']['a']}})[
            'missing'], ['a'])
        self.assertEqual(baseline.compare_baseline(document, {'cells': {'b': changed['cells']['a']}})[
            'extra'], ['b'])

    def test_deterministic_gzip(self):
        cells = baseline.make_baseline({'cells': {'a': make_cell([[0, 0, -10]])}})
        meta = {'engine': 'test', 'frames': 1, 'stride': 30}
        with tempfile.TemporaryDirectory() as directory:
            first = pathlib.Path(directory) / 'first.gz'
            second = pathlib.Path(directory) / 'second.gz'
            baseline.save(first, cells, meta)
            baseline.save(second, cells, meta)
            self.assertEqual(first.read_bytes(), second.read_bytes())
            self.assertEqual(baseline.load(first)['cells'], cells)


if __name__ == '__main__':
    unittest.main()
