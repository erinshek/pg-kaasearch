"""Loading the lexicon and the lists (data for steps 2, 5 and 6).

All files are plain text: anything after `#` is a comment, columns are separated by TAB.
The lists can be edited without touching the code.
"""
import os
import re

DATA_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'data')

_H_RE = re.compile(r'(?<![sc])h')


def key(word):
    """Comparison key.

    The «Túsindirme sózlik» headword list writes x for h (xám, xár, xárip). So both the lexicon
    word and the word being checked are brought to one form: h → x (the h inside sh and ch is
    left alone). The spelling of the stem that is returned does not change.
    """
    return _H_RE.sub('x', word)


def _rows(path):
    with open(path, encoding='utf-8') as f:
        for line in f:
            line = line.split('#', 1)[0].rstrip()
            if line.strip():
                yield [c.strip() for c in line.split('\t')]


def _words(path):
    return [r[0] for r in _rows(path)]


class Data:
    """All the lists the stemmer needs."""

    def __init__(self, data_dir=None, extra_lexicons=()):
        d = data_dir or DATA_DIR
        self.dir = d
        self.nom = set()            # non-verb headwords
        self.verb = set()           # verb stems
        lex_files = [os.path.join(d, 'lexicon.tsv'), os.path.join(d, 'lexicon_extra.tsv'), *extra_lexicons]
        for path in lex_files:
            if os.path.exists(path):
                self.load_lexicon(path)
        # entries removed from the lexicon (inflected forms that get in the way of stripping)
        exclude = os.path.join(d, 'lexicon_exclude.tsv')
        if os.path.exists(exclude):
            for r in _rows(exclude):
                (self.verb if len(r) > 1 and r[1] == 'v' else self.nom).discard(key(r[0]))

        self.protected = {key(w) for w in _words(os.path.join(d, 'protected.txt'))}
        # function words: not stripped, and cannot be a stem either (ushın + da → ush + ı + nda)
        self.function = {key(w) for w in _words(os.path.join(d, 'function_words.txt'))}
        # ready forms: for the word itself (form → stem) and for a form with a continuation
        # (form → (stem, state)). A form may appear on two lines: "aldı → al" (the word itself)
        # and "aldı → aldı, PX" (aldında).
        self.ready, self.ready_prefix = {}, {}
        for r in _rows(os.path.join(d, 'ready_forms.tsv')):
            form, stem, state = key(r[0]), r[1], (r[2] if len(r) > 2 else '-')
            self.ready.setdefault(form, stem)
            if state != '-':
                self.ready_prefix[form] = (stem, state)
        self.overrides = {key(r[0]): r[1] for r in _rows(os.path.join(d, 'overrides.tsv'))}
        self.voice_stop = {key(w) for w in _words(os.path.join(d, 'voice_stop.txt'))}
        self.voice_explicit = {key(r[0]): r[1] for r in _rows(os.path.join(d, 'voice_explicit.tsv'))}
        self.gi_bases = {key(w) for w in _words(os.path.join(d, 'gi_bases.txt'))}
        self.kinship = {key(w) for w in _words(os.path.join(d, 'kinship.txt'))}
        self.numerals = {key(w) for w in _words(os.path.join(d, 'numerals.txt'))}
        self.bare = {key(w) for w in _words(os.path.join(d, 'bare_stems.txt'))}
        self.elision = {key(w) for w in _words(os.path.join(d, 'elision.txt'))}
        # pairs: ((first, kind), (second, kind)); kind: n or v.
        #   prefer    — when the two compete, the first one wins
        #   ambiguous — the two are equal (marked "="): the word is left unchanged
        self.prefer, self.ambiguous = [], []
        for r in _rows(os.path.join(d, 'prefer.tsv')):
            pair = ((key(r[0]), r[1]), (key(r[2]), r[3]))
            (self.ambiguous if len(r) > 4 and r[4] == '=' else self.prefer).append(pair)

        # Words in the lists are accepted as stems even if the lexicon lacks them.
        self.nom |= self.gi_bases | self.kinship | self.numerals | self.elision
        # edi, eken, emes are not verbs: the lexicon has them as n; atır, tur, júr ... are verbs
        self.verb |= self.voice_stop | set(self.overrides) | (self.bare - self.nom)
        self.verb |= {key(base) for base in self.voice_explicit.values()}
        self._drop_negative_stems()

    def _drop_negative_stems(self):
        """A negative form cannot be a verb stem: qoyma = qoy + ma, túsinbe = túsin + be.

        If the base verb is in the lexicon and -ma/-ba/-pa fits it phonologically, the entry is removed
        (real verbs such as terbe-, sıypa- stay: ter + be, sıy + pa do not fit).
        """
        from . import tables as T
        mbp = {'V': 'm', 'L': 'm', 'Z': 'b', 'N': 'b', 'T': 'p'}
        for stem in [s for s in self.verb if len(s) > 3 and s[-2:] in ('ma', 'me', 'ba', 'be', 'pa', 'pe')]:
            base = stem[:-2]
            if (base in self.verb and T.has_vowel(base) and stem[-2] == mbp[T.sound_class(base)]
                    and T.is_back(base) == (stem[-1] == 'a')):
                self.verb.discard(stem)

    def load_lexicon(self, path):
        """Lexicon file: word <TAB> n|v [<TAB> source]. n = non-verb, v = verb stem."""
        for r in _rows(path):
            if len(r) < 2:
                continue
            (self.verb if r[1] == 'v' else self.nom).add(key(r[0]))
