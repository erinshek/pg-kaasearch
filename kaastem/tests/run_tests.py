#!/usr/bin/env python3
"""Runs the stemmer against four tables:

    test_set.tsv    200 words, the test set verified by a native speaker
    exceptions.tsv  100 words, the exceptions list verified by a native speaker
    reviewed.tsv    118 words, entries added during implementation and reviewed by a native speaker (2026-11-12)
    extra.tsv       examples for spelling normalisation and for the rules added during implementation

    python tests/run_tests.py          # summary
    python tests/run_tests.py -v       # every word
"""
import csv
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))

from kaastem import Stemmer  # noqa: E402

TABLES = ('test_set.tsv', 'exceptions.tsv', 'reviewed.tsv', 'extra.tsv')


def run(st, name, verbose):
    rows = list(csv.DictReader(open(os.path.join(HERE, name), encoding='utf-8'), delimiter='\t',
                               quoting=csv.QUOTE_NONE))
    bad = []
    for r in rows:
        a = st.analyze(r['word'])
        ok = a.stem == r['stem']
        if not ok:
            bad.append((r, a))
        if verbose:
            print(('  ok  ' if ok else ' FAIL ') + f"{r['n']:>3} {str(a)}" + ('' if ok else f"   expected: {r['stem']}"))
    print(f"{name}: {len(rows) - len(bad)} / {len(rows)} passed")
    for r, a in bad:
        group = r.get('group') or r.get('list')
        print(f"   FAIL {r['n']:>3} [{group}] {a}   expected: {r['stem']}")
    return len(bad)


def main():
    verbose = '-v' in sys.argv
    st = Stemmer()
    errors = sum(run(st, name, verbose) for name in TABLES)
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main())
