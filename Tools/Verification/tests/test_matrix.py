"""Synthetic specification expansion tests."""

import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import matrix  # noqa: E402


def make_spec():
    return {'level': '/Game/Test', 'frames': 3, 'record_nodes': True,
            'grid': {'origin': [0, 0, 500], 'spacing': [600, 600], 'columns': 2},
            'profiles': {'base': {'fps': 60, 'settings': {'fixed_substepping': True}}},
            'axes': {'fixture': {'chain': {'mesh': 'x', 'overrides': {'a': 1},
                                         'events': [1], 'location_offset': [1, 0, 0]}},
                     'space': {'cs': {'overrides': {'a': 2, 'b': 3},
                                      'events': [2], 'external_forces': [1]},
                               'bb': {'by_fixture': {'chain': {'overrides': {'b': 4},
                                                                'location_offset': [0, 2, 0]}}}},
                     'feature': {'none': {}, 'on': {'events': [3]}}},
            'matrix': [{'fixture': ['chain'], 'space': ['cs', 'bb'],
                        'feature': ['none', 'on']}],
            'extra_cells': [{'id': 'reg', 'tags': {'group': 'regression'},
                             'cell': {'location': [9, 8, 7]}}],
            'relations': [{'kind': 'equal', 'vary': {'axis': 'space', 'a': 'cs', 'b': 'bb'},
                           'reason': 'same space', 'max_cm': 1}]}


class MatrixTest(unittest.TestCase):
    def test_expansion_and_merging(self):
        spec = make_spec()
        result = matrix.expand_spec(spec)
        self.assertEqual(len(result['cells']), 5)
        self.assertEqual(result['cells'][0]['id'], 'chain.cs.none')
        self.assertEqual(result['cells'][0]['overrides'], {'a': 2, 'b': 3})
        self.assertEqual(result['cells'][0]['events'], [1, 2])
        self.assertEqual(result['cells'][0]['external_forces'], [1])
        self.assertEqual(result['cells'][0]['location'], [1, 0, 500])
        self.assertEqual(result['cells'][2]['location'], [1, 602, 500])
        self.assertEqual(result['cells'][2]['overrides']['b'], 4)
        self.assertEqual(result['cells'][-1]['location'], [9, 8, 7])
        self.assertEqual(len(result['relations']), 2)
        manifest = matrix.profile_manifest(spec, result, 'base')
        self.assertEqual(manifest['settings']['fixed_substepping'], True)

    def test_unknown_and_duplicate(self):
        spec = make_spec()
        spec['matrix'][0]['space'] = ['unknown']
        with self.assertRaises(ValueError):
            matrix.expand_spec(spec)
        spec = make_spec()
        spec['matrix'].append(spec['matrix'][0])
        with self.assertRaises(ValueError):
            matrix.expand_spec(spec)

    def test_except_removes_matching_pairs(self):
        spec = make_spec()
        spec['relations'][0]['except'] = {'feature': ['on']}
        relations = matrix.expand_spec(spec)['relations']
        self.assertEqual([relation['a'] for relation in relations], ['chain.cs.none'])

    def test_relation_empty_is_preserved(self):
        spec = make_spec()
        spec['relations'][0]['where'] = {'feature': ['missing']}
        self.assertIsNone(matrix.expand_spec(spec)['relations'][0]['a'])
