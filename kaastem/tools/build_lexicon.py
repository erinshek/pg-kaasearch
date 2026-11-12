#!/usr/bin/env python3
"""Builds the stemmer lexicon from the headwords of «Túsindirme sózlik».

Input:   lexicon.txt — headword <TAB> part-of-speech tag and gloss
                       (github.com/alibekembergenovv-byte/qaraqalpaq-tili, skills/qaraqalpaq-tili/data)
         optional: word-form frequency files (word <TAB> count). They serve only as evidence:
         to decide whether the stem of a verb ending in -ıw/-iw ends in a consonant (jaz-ıw)
         or in a vowel (oqı-w).
Output:  kaastem/data/lexicon.tsv — word <TAB> n|v <TAB> source

    n — a non-verb headword (noun, adjective, adverb, numeral, pronoun ...), as it is
    v — a verb stem: the headword without its -w / -ıw / -iw

Usage:
    python tools/build_lexicon.py lexicon.txt [words.txt ...] -o kaastem/data/lexicon.tsv

Entries that are left out of the lexicon:
    - verb entries that do not end in -w (alajaq, aldap ... — inflected forms);
    - negative verbal nouns (bolmaw, barmaw): the base verb exists and -ma/-ba/-pa fits it;
    - plural headwords (balalar, tatarlar): the singular is in the lexicon;
    - a variant that refers to another word, if it is an inflected form of that word
      (kewli → kewil, barlıǵı → barlıq) or another spelling of a verb (kewiw → kebiw);
    - forms with possessive and case suffixes (ústinde, aldına, negizinen): the base word is in the lexicon.
"""
import argparse
import collections
import os
import re
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

from kaastem import tables as T          # noqa: E402
from kaastem.lexicon import key          # noqa: E402

VERB_TAGS = {'f.', 'fey.'}
XREF_TAGS = {'q.', 'k.'}                 # «qarań» cross-references
DEVOICE = {'b': 'p', 'g': 'k', 'ǵ': 'q'}
MBP = {'V': 'm', 'L': 'm', 'Z': 'b', 'N': 'b', 'T': 'p'}
VN_RE = re.compile(r'(ıw|iw|aw|ew)$')
HEAD_RE = re.compile(r'[a-záǵıńóú\-]+')


def verbal_forms(stem):
    """Forms found only with verbs: past tense, -ǵan, present-future tense, negation."""
    b, c = T.is_back(stem), T.sound_class(stem)
    d = 't' if c == 'T' else 'd'
    g = ('q' if b else 'k') if c == 'T' else ('ǵ' if b else 'g')
    m = MBP[c]
    a, i = ('a', 'ı') if b else ('e', 'i')
    di = 'dı' if b else 'di'
    out = [stem + d + i + 'm', stem + d + i + 'ń', stem + d + i + ('q' if b else 'k'),
           stem + g + a + 'n', stem + g + a + 'n' + ('da' if b else 'de'), stem + g + a + 'n' + i]
    if c == 'V':
        out += [stem + 'y' + di, stem + 'ytuǵın', stem + 'y' + ('man' if b else 'men')]
    else:
        out += [stem + a + di, stem + a + 'tuǵın', stem + a + ('man' if b else 'men')]
    neg = stem + m + a
    out += [neg + 'y' + di, neg + 'y', neg + di, neg + ('ǵan' if b else 'gen'), neg + 'ytuǵın']
    return set(out)


def read_forms(paths):
    forms = collections.Counter()
    for path in paths:
        for line in open(path, encoding='utf-8'):
            p = line.rstrip('\n').split('\t')
            if len(p) == 2 and p[1].isdigit():
                forms[key(p[0].lower())] += int(p[1])
    return forms


def verb_stems(head, evidence):
    """Verb stem(s) of a headword given in the verbal-noun form."""
    if head[-2] not in 'ıi':                              # islew → isle, sóylew → sóyle
        stem = head[:-1]
        return [stem] if stem[-1] in T.VOWELS and len(stem) >= 2 else []
    a, b = head[:-2], head[:-1]                           # jaz-ıw or oqı-w ?
    if not a or not T.has_vowel(a):
        return [b]
    a_ok = a[-1] not in T.VOWELS
    if a[-1] in 'bgǵjlrmnńwy' and len(a) >= 2 and a[-2] not in T.VOWELS:
        a_ok = False                                      # órbi-, renji-, qalǵı-, ańlı-: a word cannot end like this
    if a_ok and a[-1] in DEVOICE:
        a = a[:-1] + DEVOICE[a[-1]]                       # tabıw → tap, shıǵıw → shıq
    if not a_ok:
        return [b]
    ea, eb = evidence(a), evidence(b)
    if eb and not ea:
        return [b]
    if ea >= 2 and eb >= 2:
        return [a, b]
    return [a]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('lexicon')
    ap.add_argument('forms', nargs='*')
    ap.add_argument('-o', '--output', default='-')
    args = ap.parse_args()

    forms = read_forms(args.forms)

    def evidence(stem):
        return sum(1 for f in verbal_forms(stem) if forms.get(key(f)))

    entries = []
    for line in open(args.lexicon, encoding='utf-8'):
        head, _, gloss = line.rstrip('\n').partition('\t')
        toks = gloss.split(' ')
        entries.append((head.strip().lower(), toks[0], toks[1:]))
    heads = {key(h) for h, _, _ in entries}

    stats = collections.Counter()
    nom, verb, xref = {}, {}, {}
    for head, tag, gloss in entries:
        if not HEAD_RE.fullmatch(head) or not T.has_vowel(head):
            stats['dropped: characters'] += 1
            continue
        target = gloss[0] if gloss else ''
        is_vn = bool(VN_RE.search(head)) and len(head) > 3 and '-' not in head
        if tag in VERB_TAGS or (tag == 'd.' and is_vn):
            if not is_vn:
                stats['dropped: verb entry not ending in -w'] += 1
                continue
            for stem in verb_stems(head, evidence):
                verb[stem] = tag.rstrip('.')
            continue
        if tag in XREF_TAGS and VN_RE.search(target):
            if is_vn:                                     # kewiw → kebiw: another spelling of a verb
                stats['dropped: verb variant'] += 1
                continue
            if key(target) in (key(head + 'w'), key(head + 'ıw'), key(head + 'iw')):
                verb[head] = tag.rstrip('.')              # kiyin → kiyiniw, ber → beriw
                continue
        nom[head] = tag.rstrip('.')
        if tag in XREF_TAGS and len(gloss) == 1:
            xref[head] = target

    # negative verbal nouns: bolmaw, barmaw, tınbaw (but terbe-, sıypa- stay)
    for stem in list(verb):
        if len(stem) > 3 and stem[-2:] in ('ma', 'me', 'ba', 'be', 'pa', 'pe'):
            base = stem[:-2]
            if (base in verb and T.has_vowel(base) and stem[-2] == MBP[T.sound_class(base)]
                    and T.is_back(base) == (stem[-1] == 'a')):
                del verb[stem]
                stats['dropped: negative verb'] += 1
    # plural headwords: balalar, tatarlar
    for head in list(nom):
        if re.search(r'l[ae]r$', head) and '-' not in head and key(head[:-3]) in heads and len(head) > 5:
            del nom[head]
            stats['dropped: plural'] += 1

    def write(out, nom, verb):
        out.write("# Source: headwords of «Qaraqalpaq tiliniń túsindirme sózligi»; built with tools/build_lexicon.py.\n")
        out.write("# The dictionary list writes x for h (xám, xár); the stemmer treats the two as equal when comparing.\n")
        out.write("# word\tn = non-verb, v = verb stem\tpart-of-speech tag in the dictionary\n")
        for w in sorted(nom):
            out.write(f'{w}\tn\t{nom[w]}\n')
        for w in sorted(verb):
            out.write(f'{w}\tv\t{verb[w]}\n')

    # Headwords that are inflected forms are left out of the lexicon. The stemmer itself is used to check this:
    # the word is temporarily removed from the lexicon; if it is still reduced to another word, it is an inflected form.
    #   a) a cross-referenced variant is reduced to the word it refers to, or to a verb: kewli → kewil, alıp → al
    #   b) possessive + case: ústinde = úst + i + nde, aldına = ald + ı + na, negizinen = negiz + i + nen
    from kaastem import Stemmer
    with tempfile.TemporaryDirectory() as tmp:
        data_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'kaastem', 'data')
        for name in os.listdir(data_dir):
            if name != 'lexicon.tsv':
                with open(os.path.join(data_dir, name), encoding='utf-8') as src, \
                        open(os.path.join(tmp, name), 'w', encoding='utf-8') as dst:
                    dst.write(src.read())
        with open(os.path.join(tmp, 'lexicon.tsv'), 'w', encoding='utf-8') as f:
            write(f, nom, verb)
        st = Stemmer(tmp)
        for head in list(nom):
            k = key(head)
            if '-' in head or k not in st.d.nom or k in st.d.protected:
                continue
            st.d.nom.discard(k)
            st._cache.clear()
            st._nom_cache.clear()
            a = st.analyze(head)
            st.d.nom.add(k)
            if a.source != 'lexicon' or a.stem == head:
                continue
            cls = a.classes
            if head in xref and (key(a.stem) == key(xref[head]) or cls[:1] and cls[0] in T.VERB_FIRST):
                del nom[head]
                stats['dropped: inflected variant'] += 1
            elif cls[:2] in (('PX', 'LOCP'), ('PX', 'DATP'), ('PX', 'ABL')) and a.segments[1] in ('ı', 'i', 'sı', 'si'):
                del nom[head]
                stats['dropped: possessive + case'] += 1

    out = sys.stdout if args.output == '-' else open(args.output, 'w', encoding='utf-8')
    write(out, nom, verb)
    stats['n'], stats['v'] = len(nom), len(verb)
    print(dict(stats), file=sys.stderr)


if __name__ == '__main__':
    main()
