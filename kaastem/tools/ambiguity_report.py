#!/usr/bin/env python3
"""Lists the word forms that can be split into more than one stem (for filling the exception lists).

For every word all the analyses that agree with the lexicon are built. If they lead to different stems,
the word goes into the report: the stem the stemmer chose, the rejected alternatives, and how often each
stem occurs in the text in «unambiguous» forms (forms with no other analysis).

    python tools/ambiguity_report.py forms.tsv [...] > report.tsv
    python tools/ambiguity_report.py --pairs forms.tsv [...] > pairs.tsv     # candidates for prefer.tsv
"""
import argparse
import collections
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

from kaastem import Stemmer                   # noqa: E402
from kaastem.lexicon import key               # noqa: E402
from kaastem.normalize import normalize       # noqa: E402


def seg(w, c):
    return '-'.join([w[:c.cut]] + [m for _, m in c.path])


def alternatives(st, w):
    """All (stem, kind) alternatives of a word: {(key, kind): segmentation}."""
    cands = st._candidates(w)
    if any(not c.weak for c in cands):
        cands = [c for c in cands if not c.weak]
    alts = collections.OrderedDict()
    if st._is_nom(w):
        alts[(key(w), 'n')] = w
    elif st._is_verb(w):
        alts[(key(w), 'v')] = w
    for c in sorted(cands, key=lambda c: -c.cut):
        alts.setdefault((key(c.stem), 'v' if c.kind == 'V' else 'n'), seg(w, c))
    return alts


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('forms', nargs='+')
    ap.add_argument('--pairs', action='store_true', help='summary report by stem pair')
    args = ap.parse_args()
    st = Stemmer()
    freq = collections.Counter()
    for path in args.forms:
        for line in open(path, encoding='utf-8'):
            p = line.rstrip('\n').split('\t')
            if len(p) == 2 and p[1].isdigit():
                w = normalize(p[0])
                if w.isalpha() and len(w) >= 2:
                    freq[w] += int(p[1])
    sure = collections.Counter()              # (stem, kind) → count in unambiguous forms
    amb = []
    for w, n in freq.items():
        a = st.analyze(w)
        if a.source != 'lexicon':
            continue
        alts = alternatives(st, w)
        if len({s for s, _ in alts}) < 2:
            for k in alts:
                sure[k] += n
        else:
            amb.append((n, w, a, alts))
    amb.sort(key=lambda r: -r[0])
    if not args.pairs:
        print('freq\tword\tstem\tsegmentation\talternatives (stem:kind=count in unambiguous forms)')
        for n, w, a, alts in amb:
            desc = '; '.join(f'{s}:{k}={sure[(s, k)]} [{sg}]' for (s, k), sg in alts.items())
            print(f'{n}\t{w}\t{a.stem}\t{a.segmentation}\t{desc}')
        return
    pairs = collections.defaultdict(lambda: [0, []])
    for n, w, a, alts in amb:
        chosen = None
        for (s, k) in alts:
            if s == key(a.stem) or chosen is None and False:
                chosen = (s, k)
                break
        if chosen is None:                    # the stem differs after voice reduction or an override
            chosen = next(iter(alts))
        for other in alts:
            if other[0] != chosen[0]:
                p = pairs[(chosen, other)]
                p[0] += n
                p[1].append(w)
    print('chosen\tkind\tunambiguous\trejected\tkind\tunambiguous\ttokens\texamples')
    for (c, o), (n, ws) in sorted(pairs.items(), key=lambda x: -x[1][0]):
        print(f'{c[0]}\t{c[1]}\t{sure[c]}\t{o[0]}\t{o[1]}\t{sure[o]}\t{n}\t{" ".join(ws[:6])}')


if __name__ == '__main__':
    main()
