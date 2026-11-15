#!/usr/bin/env python3
"""Runs the stemmer against five tables:

    test_set.tsv    200 words, the test set verified by a native speaker
    exceptions.tsv  100 words, the exceptions list verified by a native speaker
    reviewed.tsv    118 words, entries added during implementation and reviewed by a native speaker (2026-11-12)
    extra.tsv       examples for spelling normalisation and for the rules added during implementation
    news_sample.tsv random words from news, health and parallel-corpus texts, verified by a native speaker (2026-11-15)

and checks that every stem is stable: stem(stem(w)) == stem(w) for the words of these tables, the lexicon
headwords and idempotence_forms.txt. The known exceptions are listed in idempotence_exceptions.tsv.

    python tests/run_tests.py          # summary
    python tests/run_tests.py -v       # every word
"""
import csv
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))

from kaastem import Stemmer  # noqa: E402

TABLES = ('test_set.tsv', 'exceptions.tsv', 'reviewed.tsv', 'extra.tsv', 'news_sample.tsv')


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


def read_words(name, column=0):
    out = []
    for line in open(os.path.join(HERE, name), encoding='utf-8'):
        if line.strip() and not line.startswith('#'):
            out.append(line.rstrip('\n').split('\t')[column])
    return out


def idempotence(st):
    """stem(stem(w)) == stem(w), except for the stems listed in idempotence_exceptions.tsv."""
    exc = {r['stem']: r['stem_of_stem'] for r in csv.DictReader(
        (line for line in open(os.path.join(HERE, 'idempotence_exceptions.tsv'), encoding='utf-8')
         if not line.startswith('#')), delimiter='\t', quoting=csv.QUOTE_NONE)}
    words = read_words('idempotence_forms.txt')
    for name in TABLES:
        words += [r['word'] for r in csv.DictReader(open(os.path.join(HERE, name), encoding='utf-8'),
                                                     delimiter='\t', quoting=csv.QUOTE_NONE)]
    for name in ('lexicon.tsv', 'lexicon_extra.tsv'):
        words += read_words(os.path.join('..', 'kaastem', 'data', name))
    bad = {}
    for w in words:
        s1 = st.stem(w)
        s2 = st.stem(s1)
        if s2 != s1 and exc.get(s1) != s2:
            bad.setdefault((s1, s2), w)
    # an exception that no longer happens is removed from the list
    stale = [s for s, s2 in exc.items() if st.stem(s) != s2]
    print(f"idempotence: {len(words) - len(bad)} / {len(words)} words stable, {len(exc)} known exceptions")
    for (s1, s2), w in sorted(bad.items()):
        print(f"   FAIL {w} → {s1} → {s2}")
    for s in stale:
        print(f"   STALE exception {s}: stem is now {st.stem(s)}")
    return len(bad) + len(stale)


def main():
    verbose = '-v' in sys.argv
    st = Stemmer()
    errors = sum(run(st, name, verbose) for name in TABLES)
    errors += idempotence(st)
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main())
