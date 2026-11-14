#!/usr/bin/env python3
"""Lexicon coverage of the stemmer on text corpora (plan item 4.1).

For every corpus file it counts how the words are analysed:

    lexicon    the stem was found in the lexicon (the normal case)
    list       a ready answer from a list (pronouns, function words, ready forms)
    safe       not in the lexicon: only a safe nominal suffix of three or more letters was stripped
    unchanged  not in the lexicon and returned unchanged

"safe" and "unchanged" together are the words the lexicon does not know. This is not a measure of
accuracy: many of those words are names or foreign words that are correct unchanged.

    make coverage                                   # all files in corpus/
    python3 tools/coverage.py CLI FILE...           # CLI = test/core/kaacore_cli
    python3 tools/coverage.py CLI FILE... --unknown unknown.tsv

--unknown writes the words the lexicon does not know, most frequent first (word, frequency, number
of corpora it occurs in, example forms): the candidates for the lexicon (plan item 4.2).
"""
import argparse
import collections
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'kaastem'))
from kaastem.normalize import tokenize  # noqa: E402

SOURCES = ('lexicon', 'list', 'safe', 'unchanged')


def count_tokens(path):
    """Word forms of one corpus file with their frequency; tokens with digits are left out."""
    cnt = collections.Counter()
    with open(path, encoding='utf-8') as f:
        for line in f:
            cnt.update(t for t in tokenize(line) if not any(c.isdigit() for c in t))
    return cnt


def analyse(cli, words):
    """Run the C core on the distinct word forms: {form: (stem, source)}.

    kaacore_cli prints: word <TAB> stem <TAB> segments <TAB> classes <TAB> source."""
    out = subprocess.run([cli], input='\n'.join(words) + '\n', capture_output=True, text=True,
                         check=True).stdout.splitlines()
    if len(out) != len(words):
        sys.exit(f'{cli}: {len(words)} words in, {len(out)} lines out')
    res = {}
    for w, line in zip(words, out):
        cols = line.split('\t')
        res[w] = (cols[1], cols[-1])
    return res


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('cli', help='the kaacore_cli program (make check-core builds it)')
    ap.add_argument('files', nargs='+', help='corpus files: plain UTF-8 text')
    ap.add_argument('--unknown', metavar='TSV', help='write the words the lexicon does not know')
    args = ap.parse_args()

    corpora = [(Path(f).name, count_tokens(f)) for f in args.files]
    forms = sorted(set().union(*(c for _, c in corpora)))
    res = analyse(args.cli, forms)

    head = '| Corpus | Tokens | Distinct | ' + ' | '.join(SOURCES) + ' | Not in lexicon |'
    print(head)
    print('|' + '---|' * (head.count('|') - 1))
    unknown = collections.defaultdict(lambda: [0, set(), collections.Counter()])
    for name, cnt in corpora:
        by = collections.Counter()
        for w, n in cnt.items():
            stem, src = res[w]
            by[src] += n
            if src in ('safe', 'unchanged'):
                u = unknown[stem]
                u[0] += n
                u[1].add(name)
                u[2][w] += n
        tot = sum(cnt.values())
        pct = {s: 100 * by[s] / tot for s in SOURCES}
        print(f'| {name} | {tot:,} | {len(cnt):,} | '
              + ' | '.join(f'{pct[s]:.1f}%' for s in SOURCES)
              + f' | {pct["safe"] + pct["unchanged"]:.1f}% |')
    other = {res[w][1] for w in forms} - set(SOURCES)
    if other:
        print(f'\nother sources: {sorted(other)}')

    if args.unknown:
        rows = sorted(unknown.items(), key=lambda kv: (-kv[1][0], kv[0]))
        with open(args.unknown, 'w', encoding='utf-8') as f:
            f.write('stem\tfreq\tcorpora\tforms\n')
            for stem, (n, names, fc) in rows:
                f.write(f'{stem}\t{n}\t{len(names)}\t{" ".join(w for w, _ in fc.most_common(5))}\n')
        print(f'\n{len(rows):,} stems not in the lexicon written to {args.unknown}')


if __name__ == '__main__':
    main()
