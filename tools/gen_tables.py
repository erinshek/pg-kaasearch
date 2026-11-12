#!/usr/bin/env python3
"""Generate the C data tables of the stemmer core from kaastem.

kaastem (the Python reference stemmer in kaastem/) is the single source of the rules and the lists.
This script loads them exactly as kaastem does at run time (same normalisation of keys, same
exclusions and derived sets) and writes them out as C:

    src/kaadata.h   types, suffix-class ids, declarations
    src/kaadata.c   the data: suffix table, allowed suffix order, word lists, pairs, constants

The generated files are committed, so building the extension does not need Python. After editing
anything in kaastem/kaastem/data/ or kaastem/kaastem/tables.py, regenerate them:

    python3 tools/gen_tables.py            # write src/kaadata.[ch]
    python3 tools/gen_tables.py --check    # exit 1 if the committed files are out of date (used in CI)

Every word list is sorted by its UTF-8 bytes, so the C code can look words up with bsearch + strcmp.
"""
import argparse
import os
import sys
import unicodedata

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'kaastem'))

from kaastem import __version__ as KAASTEM_VERSION     # noqa: E402
from kaastem import stemmer as S                         # noqa: E402
from kaastem import tables as T                          # noqa: E402
from kaastem.lexicon import Data                         # noqa: E402

OUT_H = os.path.join(ROOT, 'src', 'kaadata.h')
OUT_C = os.path.join(ROOT, 'src', 'kaadata.c')

HEADER = ('/*\n * GENERATED FILE - DO NOT EDIT.\n'
          ' * Source: kaastem {ver} (kaastem/kaastem/tables.py, stemmer.py, data/).\n'
          ' * Regenerate with: python3 tools/gen_tables.py\n */\n')


def c_str(s):
    """A C string literal: printable ASCII as is, every other byte as an octal escape."""
    out = []
    for b in s.encode('utf-8'):
        c = chr(b)
        if c in '\\"':
            out.append('\\' + c)
        elif 0x20 <= b < 0x7F:
            out.append(c)
        else:
            out.append('\\%03o' % b)
    return '"' + ''.join(out) + '"'


def comment_safe(s):
    return s.replace('*/', '* /')


def byte_sorted(items):
    return sorted(items, key=lambda s: s.encode('utf-8'))


def ident(cls):
    return 'KAA_' + cls


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--check', action='store_true', help='only check that the committed files are up to date')
    args = ap.parse_args()

    d = Data()
    classes = list(T.SLOTS)                          # suffix classes in table order
    assert len(classes) + 2 <= 64, 'state bitmask is 64 bits wide'
    states = classes + ['START', 'END']
    sid = {c: i for i, c in enumerate(states)}

    def mask(names):
        m = 0
        for n in names:
            m |= 1 << sid[n]
        return m

    hdr = HEADER.format(ver=KAASTEM_VERSION)

    # ------------------------------------------------------------------ header
    h = [hdr, '#ifndef KAASEARCH_KAADATA_H', '#define KAASEARCH_KAADATA_H', '',
         '#include <stddef.h>', '#include <stdint.h>', '',
         '#define KAA_KAASTEM_VERSION ' + c_str(KAASTEM_VERSION), '',
         '/* Suffix classes, then the two pseudo-states START and END (bit positions in KaaMask). */',
         'typedef enum', '{']
    for i, s in enumerate(states):
        h.append(f'    {ident(s)} = {i},')
    h += ['    KAA_NSTATES', '} KaaClass;', '',
          '#define KAA_NCLASSES ' + str(len(classes)), '',
          'typedef uint64_t KaaMask;',
          '#define KAA_BIT(c) ((KaaMask) 1 << (c))', '',
          '/* One suffix variant (morph) of a class. */',
          'typedef struct', '{', '    KaaClass    cls;', '    const char *morph;', '} KaaMorph;', '',
          '/* A sorted list of words (UTF-8 byte order): look up with bsearch + strcmp. */',
          'typedef struct', '{', '    const char *const *words;', '    size_t      n;', '} KaaWordSet;', '',
          '/* A sorted map from a word to a string. */',
          'typedef struct', '{', '    const char *key;', '    const char *value;', '} KaaPair;', '',
          'typedef struct', '{', '    const KaaPair *items;', '    size_t      n;', '} KaaPairMap;', '',
          '/* A ready form a suffix chain may follow: form, its stem, the state the chain continues from',
          '   (KAA_START for a stem that takes person suffixes directly). */',
          'typedef struct', '{', '    const char *form;', '    const char *stem;', '    KaaClass    state;', '} KaaReady;', '',
          '/* Two competing stems; kind is \'n\' (non-verb) or \'v\' (verb). */',
          'typedef struct', '{', '    const char *first;', '    char        first_kind;',
          '    const char *second;', '    char        second_kind;', '} KaaStemPair;', '',
          '/* Derivational suffixes in back/front pairs; after_vowel: -1 any, 0 consonant, 1 vowel. */',
          'typedef struct', '{', '    const char *back;', '    const char *front;', '    int         after_vowel;', '} KaaDeriv;', '',
          '/* --- suffix table (tables.py) --- */',
          'extern const KaaMorph kaa_morphs[];', 'extern const size_t kaa_nmorphs;',
          'extern const KaaMask kaa_next[KAA_NSTATES];       /* allowed classes after each state */',
          'extern const KaaClass kaa_next_order[KAA_NSTATES][KAA_NSTATES + 1];  /* the same, in order; ends with KAA_NSTATES */',
          'extern const KaaMask kaa_verb_first, kaa_bare_first, kaa_light, kaa_safe;',
          'extern const KaaWordSet kaa_voiced_morphs, kaa_unvoiced_morphs, kaa_loan_clusters;',
          'extern const char *const kaa_class_names[KAA_NSTATES];', '',
          '/* --- word lists (data/, as loaded by kaastem.lexicon.Data; keys use x for h) --- */']
    sets = [('nom', d.nom, 'non-verb stems (lexicon + lists)'),
            ('verb', d.verb, 'verb stems'),
            ('protected', d.protected, 'protected.txt'),
            ('function', d.function, 'function_words.txt'),
            ('voice_stop', d.voice_stop, 'voice_stop.txt'),
            ('gi_bases', d.gi_bases, 'gi_bases.txt'),
            ('kinship', d.kinship, 'kinship.txt'),
            ('numerals', d.numerals, 'numerals.txt'),
            ('bare', d.bare, 'bare_stems.txt'),
            ('elision', d.elision, 'elision.txt')]
    for name, _, what in sets:
        h.append(f'extern const KaaWordSet kaa_{name};{" " * (12 - len(name))}/* {what} */')
    h += ['extern const KaaPairMap kaa_ready;                /* ready_forms.tsv: the word itself */',
          'extern const KaaReady kaa_ready_prefix[];         /* ready_forms.tsv: forms a suffix may follow */',
          'extern const size_t kaa_nready_prefix;',
          'extern const KaaPairMap kaa_overrides;            /* overrides.tsv */',
          'extern const KaaPairMap kaa_voice_explicit;       /* voice_explicit.tsv */',
          'extern const KaaStemPair kaa_prefer[];            /* prefer.tsv, in file order */',
          'extern const size_t kaa_nprefer;',
          'extern const KaaStemPair kaa_ambiguous[];         /* prefer.tsv, pairs marked "=" */',
          'extern const size_t kaa_nambiguous;', '',
          '/* --- stemmer constants (stemmer.py) --- */',
          'extern const char *const kaa_voice_suffixes[];    /* in the order they are tried */',
          'extern const size_t kaa_nvoice_suffixes;',
          'extern const KaaWordSet kaa_particles, kaa_gan, kaa_not_base, kaa_deriv_fixed;',
          'extern const KaaDeriv kaa_deriv_harmonic[];', 'extern const size_t kaa_nderiv_harmonic;',
          'extern const KaaDeriv kaa_deriv_verbal[];', 'extern const size_t kaa_nderiv_verbal;', '',
          '/* --- NFC composition of a letter and a following combining mark (normalize.py) --- */',
          'typedef struct', '{', '    uint32_t    base;', '    uint32_t    mark;', '    uint32_t    composed;', '} KaaCompose;',
          'extern const KaaCompose kaa_compose[];            /* sorted by base, then mark */',
          'extern const size_t kaa_ncompose;', '',
          '#endif /* KAASEARCH_KAADATA_H */', '']

    # ------------------------------------------------------------------ data
    c = [hdr, '#include "kaadata.h"', '']

    def wordset(name, words, static_name=None):
        words = byte_sorted(set(words))
        arr = static_name or f'{name}_words'
        c.append(f'static const char *const {arr}[] = {{')
        for w in words:
            c.append(f'    {c_str(w)},')
        if not words:
            c.append('    NULL')
        c.append('};')
        c.append(f'const KaaWordSet kaa_{name} = {{{arr}, {len(words)}}};')
        c.append('')

    def pairmap(name, mapping):
        keys = byte_sorted(mapping)
        c.append(f'static const KaaPair {name}_items[] = {{')
        for k in keys:
            c.append(f'    {{{c_str(k)}, {c_str(mapping[k])}}},')
        if not keys:
            c.append('    {NULL, NULL}')
        c.append('};')
        c.append(f'const KaaPairMap kaa_{name} = {{{name}_items, {len(keys)}}};')
        c.append('')

    c.append('/* ---------------------------------------------------------------- suffix table */')
    c.append('')
    c.append('const KaaMorph kaa_morphs[] = {')
    n = 0
    for cls in classes:
        for m in T.SLOTS[cls]:
            lit = f'{{{ident(cls)}, {c_str(m)}}},'
            c.append(f'    {lit:<40}/* {comment_safe(m)} */')
            n += 1
    c += ['};', f'const size_t kaa_nmorphs = {n};', '']
    c.append('const KaaMask kaa_next[KAA_NSTATES] = {')
    for s in states:
        nxt = T.NEXT.get(s, [])
        names = ' | '.join(f'KAA_BIT({ident(x)})' for x in nxt) or '0'
        c.append(f'    [{ident(s)}] = {names},')
    c += ['};', '']
    # The same lists in their original order: kaastem tries the classes in this order, and the
    # order decides between analyses that tie, so the C code must follow it too.
    c.append('const KaaClass kaa_next_order[KAA_NSTATES][KAA_NSTATES + 1] = {')
    for s in states:
        items = ', '.join(ident(x) for x in T.NEXT.get(s, []))
        c.append(f'    [{ident(s)}] = {{{items + ", " if items else ""}KAA_NSTATES}},')
    c += ['};', '']
    for name, group in (('verb_first', T.VERB_FIRST), ('bare_first', T.BARE_FIRST),
                        ('light', T.LIGHT), ('safe', T.SAFE)):
        names = ' | '.join(f'KAA_BIT({ident(x)})' for x in sorted(group, key=sid.get)) or '0'
        c.append(f'const KaaMask kaa_{name} = {names};')
    c.append('')
    c.append('const char *const kaa_class_names[KAA_NSTATES] = {')
    for s in states:
        c.append(f'    [{ident(s)}] = {c_str(s)},')
    c += ['};', '']
    wordset('voiced_morphs', T.VOICED)
    wordset('unvoiced_morphs', T.UNVOICED)
    wordset('loan_clusters', T.LOAN_CLUSTERS)

    c.append('/* ---------------------------------------------------------------- word lists */')
    c.append('')
    for name, words, _ in sets:
        wordset(name, words)
    pairmap('ready', d.ready)
    c.append('const KaaReady kaa_ready_prefix[] = {')
    for form in byte_sorted(d.ready_prefix):
        stem, state = d.ready_prefix[form]
        st = 'KAA_START' if state == 'BARE' else ident(state)
        c.append(f'    {{{c_str(form)}, {c_str(stem)}, {st}}},')
    c += ['};', f'const size_t kaa_nready_prefix = {len(d.ready_prefix)};', '']
    pairmap('overrides', d.overrides)
    pairmap('voice_explicit', d.voice_explicit)
    for name, pairs in (('prefer', d.prefer), ('ambiguous', d.ambiguous)):
        c.append(f'const KaaStemPair kaa_{name}[] = {{')
        for (a, ak), (b, bk) in pairs:
            c.append(f"    {{{c_str(a)}, '{ak}', {c_str(b)}, '{bk}'}},")
        if not pairs:
            c.append("    {NULL, 0, NULL, 0}")
        c += ['};', f'const size_t kaa_n{name} = {len(pairs)};', '']

    c.append('/* ---------------------------------------------------------------- stemmer constants */')
    c.append('')
    c.append('const char *const kaa_voice_suffixes[] = {')
    for v in S._VOICE:
        c.append(f'    {c_str(v)},')
    c += ['};', f'const size_t kaa_nvoice_suffixes = {len(S._VOICE)};', '']
    wordset('particles', S._PARTICLES)
    wordset('gan', S._GAN)
    wordset('not_base', S._NOT_BASE)
    wordset('deriv_fixed', S._DERIV_FIXED)
    c.append('const KaaDeriv kaa_deriv_harmonic[] = {')
    for back, front in S._DERIV_HARMONIC:
        c.append(f'    {{{c_str(back)}, {c_str(front)}, -1}},')
    c += ['};', f'const size_t kaa_nderiv_harmonic = {len(S._DERIV_HARMONIC)};', '']
    c.append('const KaaDeriv kaa_deriv_verbal[] = {')
    for back, front, after_vowel in S._DERIV_VERBAL:
        c.append(f'    {{{c_str(back)}, {c_str(front)}, {int(after_vowel)}}},')
    c += ['};', f'const size_t kaa_nderiv_verbal = {len(S._DERIV_VERBAL)};', '']

    # NFC pairs for the letters that occur in Karakalpak text (Latin, Latin-1, Cyrillic) and the
    # combining marks that may follow them in decomposed input
    bases = [chr(x) for x in list(range(0x41, 0x5B)) + list(range(0x61, 0x7B)) + list(range(0xC0, 0x250))
             + list(range(0x400, 0x530))]
    marks = [chr(x) for x in range(0x300, 0x370)]
    comp = []
    for b in bases:
        for m in marks:
            n = unicodedata.normalize('NFC', b + m)
            if len(n) == 1:
                comp.append((ord(b), ord(m), ord(n)))
    c.append('/* ---------------------------------------------------------------- NFC composition */')
    c.append('')
    c.append('const KaaCompose kaa_compose[] = {')
    for b, m, n in comp:
        c.append(f'    {{0x{b:04X}, 0x{m:04X}, 0x{n:04X}}},')
    c += ['};', f'const size_t kaa_ncompose = {len(comp)};', '']

    files = {OUT_H: '\n'.join(h), OUT_C: '\n'.join(c)}
    stale = []
    for path, text in files.items():
        old = open(path, encoding='utf-8').read() if os.path.exists(path) else None
        if old != text:
            stale.append(os.path.relpath(path, ROOT))
            if not args.check:
                with open(path, 'w', encoding='utf-8') as f:
                    f.write(text)
    if args.check:
        if stale:
            print('out of date: ' + ', '.join(stale) + ' (run: python3 tools/gen_tables.py)', file=sys.stderr)
            return 1
        print('C tables are up to date')
        return 0
    print('written: ' + (', '.join(stale) if stale else 'nothing (already up to date)'))
    return 0


if __name__ == '__main__':
    sys.exit(main())
