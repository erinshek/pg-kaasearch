#!/usr/bin/env python3
"""Finds stems missing from the lexicon in a list of word forms (candidates for the supplementary lexicon).

Method. For every word form that cannot be analysed with the lexicon, all possible splits are built from
the suffix table (without the lexicon check). A stem that occurs with several different suffixes becomes
a candidate:

  verb:  at least 3 different suffix classes and 4 different suffix chains; one of them must be an
         unmistakably verbal suffix that begins with a consonant (-dı, -ǵan, -ma, -sa), so that the
         stem boundary is certain;
  noun:  the stem also occurs on its own, with at least 2 different classes (plural, cases) and 3 chains.

Candidates are accepted from the shortest to the longest; after each round the forms are analysed again,
so once «al» is accepted, «alma», «alın», «alıw» are no longer candidates.
The result has to be reviewed by hand.

    python tools/induce_lexicon.py [--min-freq N] forms1.tsv [forms2.tsv ...] > candidates.tsv
"""
import argparse
import collections
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

from kaastem import Stemmer, tables as T      # noqa: E402
from kaastem.lexicon import key               # noqa: E402
from kaastem.normalize import normalize       # noqa: E402

DEVOICE = {'b': 'p', 'g': 'k', 'ǵ': 'q'}
N_STRONG = {'PL', 'GEN', 'DAT', 'ABL', 'LOC'}
V_STRONG = {'PAST', 'PTCP', 'NEG', 'COND', 'CVBP', 'PRES', 'INT', 'CVB', 'IMP', 'VN'}
GAN = ('ǵan', 'gen', 'qan', 'ken')
# fragments where a suffix is written as a word of its own («-lar», «-lıq» in exercises) are not candidates
NOT_STEM = {m for ms in T.SLOTS.values() for m in ms} | set(
    'lıq lik shı shi lı li sız siz la le lan len las les lar ler dıq dik tıq tik ıq ik ıs is ısh ish'.split())


def is_boundary(surf, path):
    """Does the split show the stem boundary for certain (a verb form no other stem can explain)."""
    cls = [c for c, _ in path]
    m0 = path[0][1]
    if cls[0] == 'PTCP' and (m0 in GAN or m0.endswith('tuǵın')):       # jasaǵan, basatuǵın
        return True
    if cls[0] == 'NEG' and len(cls) > 1:                               # jarmaǵan, janbay
        return True
    if cls[0] == 'PAST':
        if surf[-1] in 'ıi':                                           # sharpıdı (after a vowel the accusative is -nı)
            return True
        return len(cls) > 1 and path[1][1] in ('m', 'q', 'k', 'ńız', 'ńiz')     # qırdım (but not qoldıń)
    if cls[0] == 'PRES' and m0 == 'y' and len(cls) > 1:                # oqıydı, jasayman
        return True
    if cls[0] == 'COND' and len(cls) > 1:                              # barsam
        return True
    return cls[0] in ('INT', 'CVB')                                    # qırmaqshı, atamastan


def letters(w):
    return len(re.sub(r'[sc]h', 'S', w))


def read_forms(paths):
    freq = collections.Counter()
    for path in paths:
        for line in open(path, encoding='utf-8'):
            p = line.rstrip('\n').split('\t')
            if len(p) == 2 and p[1].isdigit():
                w = normalize(p[0])
                if w.isalpha() and len(w) >= 2:
                    freq[w] += int(p[1])
    return freq


def reset(st):
    st._cache.clear()
    st._nom_cache.clear()
    st._voice_cache.clear()


def known(st, w):
    return st.analyze(w).source in ('lexicon', 'list')


def hypotheses(st, oov, kind_wanted):
    """oov holds the forms not analysed yet; those that can now be analysed are removed from it."""
    hyp = collections.defaultdict(lambda: collections.defaultdict(set))    # stem -> cls0 -> {suffix chain}
    hfreq = collections.Counter()
    for w in [w for w in oov if known(st, w)]:
        del oov[w]
    for w, n in oov.items():
        if len(w) < 3:
            continue
        seen = set()
        for cut in range(2, len(w)):
            surf, rest = w[:cut], w[cut:]
            if not T.has_vowel(surf):
                continue
            for path in T.parse(surf, rest, 'START'):
                cls0, m0 = path[0]
                kind = 'v' if cls0 in T.VERB_FIRST else 'n'
                if kind != kind_wanted:
                    continue
                stem = surf
                if m0[0] in T.VOWELS and surf[-1] in DEVOICE and len(surf) > 2 and surf[-2] in T.VOWELS:
                    stem = surf[:-1] + DEVOICE[surf[-1]]
                hyp[stem][cls0].add(rest)
                if kind == 'v' and is_boundary(surf, path):
                    hyp[stem]['#boundary'].add(rest)
                if stem not in seen:
                    seen.add(stem)
                    hfreq[stem] += n
    return hyp, hfreq


def qualify(st, freq, kind, hyp, hfreq, min_freq, trusted=None, tier=1):
    out = []
    for stem, by_cls in hyp.items():
        if trusted is not None and stem not in trusted and not any(
                stem + s in trusted for c, sufs in by_cls.items() for s in sufs):
            continue                                      # never occurs in the trusted text
        strong_set = N_STRONG if kind == 'n' else V_STRONG
        strong = {c: s for c, s in by_cls.items() if c in strong_set}
        n_suf = len(set().union(*strong.values())) if strong else 0
        if hfreq[stem] < min_freq or stem in NOT_STEM:
            continue
        if kind == 'n':
            if stem not in freq or len(strong) < 2 or n_suf < (3 if tier == 1 else 2) or st._is_nom(stem):
                continue
            if tier == 2 and (trusted is None or stem not in trusted or stem.endswith(('lar', 'ler'))):
                continue                                  # tier 2: the stem must occur on its own in the trusted text
        elif tier == 1:
            if len(strong) < 3 or n_suf < 4 or len(by_cls.get('#boundary', ())) < 2 or st._is_verb(stem):
                continue
        else:                                             # tier 2: a verbal noun + at least one certain verb form
            if 'VN' not in by_cls or not by_cls.get('#boundary') or len(strong) < 2 or n_suf < 3 \
                    or st._is_verb(stem):
                continue
        ex = ' '.join(sorted(stem + s for c in strong for s in strong[c])[:5])
        out.append((stem, kind, hfreq[stem], len(strong), n_suf, freq.get(stem, 0), ex))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('forms', nargs='+')
    ap.add_argument('--trusted', action='append', default=[],
                    help='file of trusted forms: a candidate must occur in it at least once')
    ap.add_argument('--min-freq', type=int, default=3)
    ap.add_argument('--min-letters', type=int, default=3)
    ap.add_argument('--tier', type=int, default=1, choices=(1, 2),
                    help='1 = strict evidence (default); 2 = weaker evidence, the result must be reviewed')
    args = ap.parse_args()
    st = Stemmer()
    freq = read_forms(args.forms + args.trusted)
    trusted = set(read_forms(args.trusted)) if args.trusted else None
    total = sum(freq.values())

    oov = {w: n for w, n in freq.items() if not known(st, w)}

    def coverage():
        reset(st)
        for w in [w for w in oov if known(st, w)]:
            del oov[w]
        return 100 - 100 * sum(oov.values()) / total

    print(f'forms: {len(freq)}; initial coverage: {coverage():.1f}%', file=sys.stderr)
    accepted = []
    for kind in ('v', 'n'):
        length = args.min_letters
        while length <= 14:
            reset(st)
            hyp, hfreq = hypotheses(st, oov, kind)
            cands = qualify(st, freq, kind, hyp, hfreq, args.min_freq, trusted, args.tier)
            now = [c for c in cands if letters(c[0]) == length]
            for c in now:
                (st.d.verb if kind == 'v' else st.d.nom).add(key(c[0]))
                accepted.append(c + (length,))
            if not any(letters(c[0]) > length for c in cands):
                break
            length += 1
        print(f'{kind}: accepted {sum(1 for a in accepted if a[1] == kind)}; coverage: {coverage():.1f}%',
              file=sys.stderr)
    print('stem\tkind\tfreq\tclasses\tchains\tbare\texamples')
    for a in sorted(accepted, key=lambda a: (a[1], -a[2])):
        print('\t'.join(map(str, a[:7])))


if __name__ == '__main__':
    main()
