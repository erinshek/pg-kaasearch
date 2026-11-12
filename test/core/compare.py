#!/usr/bin/env python3
"""Equivalence test: the C stemmer core must give the same analysis as kaastem for every word.

The word list is built here, reproducibly (fixed seed), from what is already in the repository:

  - every test word of kaastem (kaastem/tests/*.tsv);
  - every stem of the lexicon and the lists;
  - inflected forms: random stems followed by 1-4 random suffixes in an allowed order
    (both real word forms and impossible ones: the two programs must agree on both);
  - spelling variants of all of the above: upper case, title case, Cyrillic, apostrophe spelling,
    decomposed accents, hyphenated pairs, particles;
  - random strings of Latin, Cyrillic, combining marks, invisible characters and punctuation.

For every word both programs report: stem, segments, suffix classes and source. Any difference fails.
Words longer than KAA_MAX_WORD characters are skipped (the C core returns them unchanged).

    cc -std=c99 -O2 -fsanitize=address,undefined -Isrc test/core/kaacore_cli.c src/kaacore.c src/kaadata.c -o kaacore_cli
    python3 test/core/compare.py ./kaacore_cli [--words 150000] [--seed 1]
"""
import argparse
import csv
import io
import os
import random
import subprocess
import sys
import unicodedata

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, 'kaastem'))

from kaastem import Stemmer                      # noqa: E402
from kaastem import tables as T                  # noqa: E402
from kaastem.lexicon import Data                 # noqa: E402
from kaastem.normalize import _CYR, normalize    # noqa: E402

MAX_WORD = 64
LAT_TO_CYR = {}
for cyr, lat in _CYR.items():
    if lat and lat not in LAT_TO_CYR:
        LAT_TO_CYR[lat] = cyr


def to_cyrillic(w):
    out, i = [], 0
    while i < len(w):
        two = w[i:i + 2]
        if two in LAT_TO_CYR:
            out.append(LAT_TO_CYR[two])
            i += 2
        else:
            out.append(LAT_TO_CYR.get(w[i], w[i]))
            i += 1
    return ''.join(out)


def to_apostrophe(w, rnd):
    apo = rnd.choice("'’ʻ`")
    return (w.replace('á', 'a' + apo).replace('ó', 'o' + apo).replace('ú', 'u' + apo)
             .replace('ǵ', 'g' + apo).replace('ń', 'n' + apo).replace('ı', 'i' + apo))


def build_words(n_target, seed):
    rnd = random.Random(seed)
    d = Data()
    words = []
    for name in sorted(os.listdir(os.path.join(ROOT, 'kaastem', 'tests'))):
        if name.endswith('.tsv'):
            text = open(os.path.join(ROOT, 'kaastem', 'tests', name), encoding='utf-8').read()
            words += [r['word'] for r in csv.DictReader(io.StringIO(text), delimiter='\t', quoting=csv.QUOTE_NONE)]
    stems = sorted(d.nom | d.verb | d.protected | d.function | set(d.ready) | set(d.ready_prefix))
    words += stems
    # inflected forms
    morphs = {c: list(ms) for c, ms in T.SLOTS.items()}
    n_forms = n_target // 2
    for _ in range(n_forms):
        w, state = rnd.choice(stems), 'START'
        for _ in range(rnd.randint(1, 4)):
            nxt = [c for c in T.NEXT[state] if c != 'END']
            if not nxt:
                break
            state = rnd.choice(nxt)
            w += rnd.choice(morphs[state])
        words.append(w)
    # spelling variants
    base = list(words)
    for _ in range(n_target // 4):
        w = rnd.choice(base)
        k = rnd.randrange(7)
        if k == 0:
            words.append(w.upper())
        elif k == 1:
            words.append(w[:1].upper() + w[1:])
        elif k == 2:
            words.append(to_cyrillic(w))
        elif k == 3:
            words.append(to_apostrophe(w, rnd))
        elif k == 4:
            words.append(unicodedata.normalize('NFD', w))
        elif k == 5:
            words.append(w + '-' + rnd.choice(base))
        else:
            words.append(w + '-' + rnd.choice(['aq', 'á', 'a', 'sa', 'ay']))
    # random strings
    alphabet = (list("aábdefgǵhiıjklmnńoópqrstuúvwxyzAÁBÍIŃǴOÓUÚ'’ʻ`-–. 0123456789")
                + [chr(c) for c in range(0x400, 0x500, 2)] + list('ўғқңөүҳәЎҒҚҢӨҮҲӘ')
                + ['́', '̆', '̈', '­', '​'])
    for _ in range(n_target // 6):
        words.append(''.join(rnd.choice(alphabet) for _ in range(rnd.randint(0, 40))))
    return [w for w in words if '\n' not in w and '\r' not in w]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('cli', help='path to the compiled kaacore_cli')
    ap.add_argument('--words', type=int, default=150000, help='approximate number of generated words')
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--show', type=int, default=20, help='differences to print')
    args = ap.parse_args()

    words = build_words(args.words, args.seed)
    proc = subprocess.run([args.cli], input='\n'.join(words) + '\n', capture_output=True, text=True)
    if proc.returncode != 0 or proc.stderr:
        print(proc.stderr, file=sys.stderr)
        print(f'kaacore_cli failed with exit code {proc.returncode}', file=sys.stderr)
        return 1
    c_lines = proc.stdout.split('\n')[:-1]
    if len(c_lines) != len(words):
        print(f'kaacore_cli printed {len(c_lines)} lines for {len(words)} words', file=sys.stderr)
        return 1
    st = Stemmer()
    compared = skipped = 0
    diffs = []
    for w, line in zip(words, c_lines):
        if len(normalize(w)) > MAX_WORD:
            skipped += 1
            continue
        a = st.analyze(w)
        py = f"{w}\t{a.stem}\t{'|'.join(a.segments)}\t{' '.join(a.classes)}\t{a.source}"
        compared += 1
        if py != line:
            diffs.append((py, line))
    print(f'words compared: {compared}; skipped (longer than {MAX_WORD} characters): {skipped}; '
          f'differences: {len(diffs)}')
    for py, c in diffs[:args.show]:
        print('  kaastem:', py.replace('\t', ' | '))
        print('  C core: ', c.replace('\t', ' | '))
    return 1 if diffs else 0


if __name__ == '__main__':
    sys.exit(main())
