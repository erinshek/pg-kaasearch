"""Karakalpak stemmer: steps 2–6.

The order follows the project's six-step stemming procedure:
  1. spelling normalisation           normalize.py
  2. ready answers                    data/protected.txt, function_words.txt, ready_forms.tsv, overrides.tsv
  3. suffix stripping                 tables.py: SLOTS, NEXT, parse
  4. checks and stem restoration      tables.py: harmony_ok, class_ok; this file: _restorations
  5. selection                        this file: _candidates, _exceptions, _select, _light, _safe; data/prefer.tsv
  6. voice                            this file: _voice_base, _voice_reduce; data/voice_stop.txt
"""
from dataclasses import dataclass

from . import tables as T
from .lexicon import Data, key
from .normalize import normalize, tokenize

_DEVOICE = {'b': 'p', 'g': 'k', 'ǵ': 'q'}

# Particles written with a hyphen: sen-aq, kelseń-á
_PARTICLES = frozenset(['aq', 'ay', 'aw', 'áy', 'a', 'á', 'sa', 'se'])

# Voice suffixes in the order they are tried. After a vowel the one-letter variant is tried before the
# two-letter one: oqıt = oqı + t (not oq + ıt), tanıl = tanı + l.
_VOICE = ('dır', 'dir', 'tır', 'tir', 'ǵız', 'giz', 'qız', 'kiz',
          'l', 'n', 's', 't',
          'ıl', 'il', 'ın', 'in', 'ıs', 'is', 'ıt', 'it', 'ır', 'ir', 'ar', 'er')
_GAN = frozenset(['ǵan', 'gen', 'qan', 'ken'])

# Derivational suffixes, which are never stripped. A lexicon word plus one of these suffixes is also
# accepted as a stem (tazalıq = taza + lıq), but the suffix itself is not stripped.
_DERIV_HARMONIC = (('shılıq', 'shilik'), ('lıq', 'lik'), ('shı', 'shi'), ('lı', 'li'), ('sız', 'siz'))
_DERIV_FIXED = ('gershilik', 'xana', 'kesh', 'paz', 'stan', 'ker', 'ger', 'xor', 'dar', 'góy')
# from a verb stem: oqı-wshı, jaz-ıwshı, basla-wısh, arna-wlı, dúzil-is (True: the stem ends in a vowel)
_DERIV_VERBAL = (('wshı', 'wshi', True), ('ıwshı', 'iwshi', False), ('wısh', 'wish', True), ('wlı', 'wli', True),
                 ('ıs', 'is', False))                    # dúzil-is, irkil-is, qurıl-ıs: action noun
# particles that cannot be the stem of a nominal chain (malı = mal + ı, not ma + lı; pada = pada, not pa + da)
_NOT_BASE = frozenset(['ma', 'me', 'ba', 'be', 'pa', 'pe', 'da', 'de', 'ta', 'te', 'sa', 'se', 'so', 'she',
                       'ya', 'gá', 'ǵoy', 'al'])


@dataclass(frozen=True)
class Analysis:
    """Analysis of a single word."""
    word: str                 # input (normalised)
    stem: str                 # stem
    segments: tuple = ()      # surface segmentation: ('mekteb', 'imiz', 'de')
    classes: tuple = ()       # suffix classes: ('PX', 'LOC')
    notes: tuple = ()         # stem changes and the rules applied
    source: str = ''          # 'list' | 'lexicon' | 'safe' | 'unchanged'

    @property
    def segmentation(self):
        return '-'.join(self.segments) if self.segments else self.word

    def __str__(self):
        s = f'{self.word} → {self.stem}  [{self.segmentation}]'
        if self.notes:
            s += '  (' + '; '.join(self.notes) + ')'
        return s


@dataclass
class _Cand:
    cut: int                  # length of the surface stem
    stem: str                 # restored stem
    kind: str                 # 'N' nominal chain, 'V' verbal chain, 'B' atır/edi/eken, 'R' ready form
    path: list                # [(class, suffix), ...]
    note: str = ''
    weak: bool = False        # tentative analysis (derived stem, tentative restoration): used only if nothing else fits


_KIND_ORDER = {'R': 0, 'N': 1, 'B': 2, 'V': 3}


class Stemmer:
    """Light (mode A) stemmer for the Karakalpak language.

    >>> st = Stemmer()
    >>> st.stem('mektebimizde')
    'mektep'
    """

    def __init__(self, data_dir=None, extra_lexicons=()):
        self.d = Data(data_dir, extra_lexicons)
        self._nom_cache = {}
        self._voice_cache = {}
        self._cache = {}

    # ------------------------------------------------------------------ public API

    def stem(self, word):
        """Return the stem of a word."""
        return self.analyze(word).stem

    def analyze(self, word):
        """Analyse a word: stem, segmentation and the rules applied."""
        w = normalize(word)
        if '-' in w:
            return self._analyze_hyphen(w)
        return self._analyze_token(w)

    def stem_text(self, text):
        """Return a (word, stem) pair for every word in the text."""
        return [(tok, self.stem(tok)) for tok in tokenize(text)]

    # ------------------------------------------------------------------ the lexicon-dependent part of step 1

    def _analyze_token(self, w):
        a = self._analyze_simple(w)
        # 2009–2016 orthography: ye- → e-, wo- → o-, wó- → ó- (only if the result is in the lexicon)
        if a.source in ('safe', 'unchanged') and len(w) > 2 and w[:2] in ('ye', 'wo', 'wó'):
            b = self._analyze_simple(w[1:])
            if b.source in ('list', 'lexicon'):
                return Analysis(w, b.stem, b.segments, b.classes,
                                (f'old spelling: {w[:2]}- → {w[1]}-',) + b.notes, b.source)
        return a

    def _analyze_hyphen(self, w):
        parts = w.split('-')
        # a particle written with a hyphen is dropped: sen-aq → sen, kelseń-á → kel
        if len(parts) >= 2 and parts[-1] in _PARTICLES and parts[-2].isalpha():
            head = '-'.join(parts[:-1])
            a = self._analyze_hyphen(head) if '-' in head else self._analyze_token(head)
            if a.source in ('safe', 'unchanged') and head[-1] in _DEVOICE and '-' not in head:
                b = self._analyze_token(head[:-1] + _DEVOICE[head[-1]])       # joǵ-á → joq
                if b.source in ('list', 'lexicon'):
                    a = b
            return Analysis(w, a.stem, a.segments, a.classes, (f'particle: -{parts[-1]}',) + a.notes, a.source)
        if key(w) in self.d.nom or key(w) in self.d.protected or key(w) in self.d.function:   # ata-ana
            return Analysis(w, w, (w,), (), (), 'lexicon')
        # paired words and «5-mektepke»: each part is stemmed separately, the hyphen stays
        stems, notes, source = [], [], 'unchanged'
        for p in parts:
            if p.isalpha():
                a = self._analyze_token(p)
                stems.append(a.stem)
                notes.extend(a.notes)
                if a.source in ('list', 'lexicon') or (a.source == 'safe' and source == 'unchanged'):
                    source = a.source
            else:
                stems.append(p)
        return Analysis(w, '-'.join(stems), tuple(parts), (), tuple(notes), source)

    # ------------------------------------------------------------------ lexicon checks

    def _is_nom(self, w, depth=0):
        """Is it a non-verb stem: 2 = in the lexicon; 1 = lexicon word + derivational suffix; 0 = no."""
        k = key(w)
        if k in self.d.nom or k in self.d.protected:
            return 2
        if depth >= 2 or len(w) < 4:
            return 0
        if depth == 0 and k in self._nom_cache:
            return self._nom_cache[k]
        level = 1 if self._is_derived(w, depth) else 0
        if depth == 0:
            self._nom_cache[k] = level
        return level

    def _is_derived(self, w, depth):
        def base_ok(base):
            return (len(base) >= 2 and T.has_vowel(base) and key(base) not in _NOT_BASE
                    and self._is_nom(base, depth + 1))
        for back, front in _DERIV_HARMONIC:
            for suf in (back, front):
                if w.endswith(suf):
                    base = w[:-len(suf)]
                    if base and T.is_back(base) == (suf == back) and base_ok(base):
                        return True
        for suf in _DERIV_FIXED:
            if w.endswith(suf) and base_ok(w[:-len(suf)]):
                return True
        for back, front, after_vowel in _DERIV_VERBAL:
            for suf in (back, front):
                if w.endswith(suf):
                    base = w[:-len(suf)]
                    if len(base) < 2 or not T.has_vowel(base) or (base[-1] in T.VOWELS) != after_vowel \
                            or T.is_back(base) != (suf == back):
                        continue
                    if self._is_verb(base):
                        return True
                    if suf[0] in T.VOWELS and base[-1] in _DEVOICE and base[-2] in T.VOWELS \
                            and self._is_verb(base[:-1] + _DEVOICE[base[-1]]):
                        return True                               # shıǵıs = shıq + ıs
        return False

    def _is_verb(self, w):
        """Is it a verb stem: in the lexicon, or a lexicon verb + a voice suffix."""
        return key(w) in self.d.verb or self._voice_base(w) is not None

    # ------------------------------------------------------------------ step 6: voice

    @staticmethod
    def _voice_fit(rem, suf):
        if not T.harmony_ok(rem, suf, 'VOICE'):
            return False
        sc = T.sound_class(rem)
        if suf in ('l', 'n', 's'):                       # only after a vowel; the remainder has at least two syllables
            return sc == 'V' and T.vowel_count(rem) >= 2
        if suf == 't':                                   # after a vowel, r, y: oqıt, ózgert, kóbeyt
            return (sc == 'V' or rem[-1] in 'ry') and T.vowel_count(rem) >= 2
        if suf in ('dır', 'dir', 'ǵız', 'giz'):
            return sc != 'T'
        if suf in ('tır', 'tir', 'qız', 'kiz'):
            return sc == 'T'
        return sc != 'V'                                 # -ıl, -ın, -ıs, -ıt, -ır, -ar: after a consonant

    def _voice_base(self, w):
        """Strip one voice suffix: (base, suffix) or None."""
        k = key(w)
        if k in self._voice_cache:
            return self._voice_cache[k]
        res = None
        if k in self.d.voice_explicit:
            res = (self.d.voice_explicit[k], 'explicit form')
        else:
            options = []
            for suf in _VOICE:
                if not w.endswith(suf) or len(w) - len(suf) < 2:
                    continue
                rem = w[:-len(suf)]
                if not T.has_vowel(rem) or not self._voice_fit(rem, suf):
                    continue
                variants = [rem]
                if suf[0] in T.VOWELS and rem[-1] in _DEVOICE and rem[-2] in T.VOWELS:
                    variants.append(rem[:-1] + _DEVOICE[rem[-1]])           # shıǵ-ar → shıq
                for cand in variants:
                    if self._is_verb(cand):
                        options.append((cand, '-' + suf))
                        break
            # the preferred stem in pairs such as qur / qurı (prefer.tsv)
            losers = {lose for win, lose in self.d.prefer + self.d.ambiguous
                      if win[1] == lose[1] == 'v' and any(key(c) == win[0] for c, _ in options)}
            options = [o for o in options if (key(o[0]), 'v') not in losers] or options
            if options:
                res = options[0]
        self._voice_cache[k] = res
        return res

    def _voice_reduce(self, stem, notes):
        while True:
            k = key(stem)
            if k in self.d.overrides or k in self.d.voice_stop or k in self.d.protected or k in self.d.bare:
                break
            vb = self._voice_base(stem)
            if vb is None:
                break
            notes.append(f'voice {vb[1]}: {stem} → {vb[0]}')
            stem = vb[0]
        return stem

    # ------------------------------------------------------------------ step 4: stem restoration

    def _restorations(self, surf, path):
        """Lexicon forms of what remains after stripping: the unchanged form first, then the restored ones."""
        cls0, m0 = path[0]
        yield surf, '', False
        vowel0 = m0[0] in T.VOWELS
        if vowel0 and surf[-1] in _DEVOICE and surf[-2] in T.VOWELS:
            yield surf[:-1] + _DEVOICE[surf[-1]], f'{surf[-1]} → {_DEVOICE[surf[-1]]}', False
        if cls0 == 'CVBP' and vowel0 and surf[-1] == 'w':
            yield surf[:-1] + 'p', 'w → p', False
        if cls0 == 'PX' and vowel0 and len(surf) >= 3 and surf[-1] not in T.VOWELS and surf[-2] not in T.VOWELS:
            v = 'ı' if T.is_back(surf) else 'i'
            full = surf[:-1] + v + surf[-1]
            # listed words (xalıq, orın ...) are certain; the rest are tentative
            yield full, f'dropped {v} restored', key(full) not in self.d.elision
            if surf == 'dost':
                yield 'dos', 'dost → dos', False
        if cls0 == 'NUM' and m0 in ('aw', 'ew'):
            yield surf + ('ı' if T.is_back(surf) else 'i'), 'numeral base', False
        if cls0 not in T.VERB_FIRST and surf[-1] not in T.VOWELS:
            yield surf + surf[-1], 'double consonant', True

    # ------------------------------------------------------------------ steps 3–5: stripping and selection

    def _valid(self, stem, kind, path):
        cls0 = path[0][0]
        k = key(stem)
        if kind == 'V':
            return 2 if self._is_verb(stem) else 0
        if k in _NOT_BASE or k in self.d.function:       # function words do not inflect: pada ≠ pa + da
            return 0
        if cls0 == 'NUM':                                # only after numerals
            return 2 if k in self.d.numerals else 0
        if cls0 == 'JAN':                                # only after kinship terms
            return 2 if k in self.d.kinship else 0
        if cls0 == 'GI':                                 # direct -ǵı: only words of time and place
            return 2 if k in self.d.gi_bases else 0
        return self._is_nom(stem)                        # 2 = in the lexicon, 1 = derived (tentative)

    def _candidates(self, w):
        """All analyses of the word that agree with the lexicon."""
        out = []
        for cut in range(len(w) - 1, 1, -1):
            surf, rest = w[:cut], w[cut:]
            if not T.has_vowel(surf):
                continue
            ks = key(surf)
            ready = self.d.ready_prefix.get(ks)
            if ready:                                    # ready form + continuation: onda-ǵı, meniń-she, aldı-nda
                state = 'START' if ready[1] == 'BARE' else ready[1]
                for path in T.parse(surf, rest, state, bare=True):
                    out.append(_Cand(cut, ready[0], 'R', path, f'ready form: {surf} → {ready[0]}'))
            for path in T.parse(surf, rest, 'START', bare=ks in self.d.bare):
                cls0 = path[0][0]
                if cls0 in T.BARE_FIRST:
                    out.append(_Cand(cut, surf, 'B', path))
                    continue
                kind = 'V' if cls0 in T.VERB_FIRST else 'N'
                for stem, note, weak in self._restorations(surf, path):
                    level = self._valid(stem, kind, path)
                    if level:
                        out.append(_Cand(cut, stem, kind, path, note, weak or level == 1))
                        break
        return out

    def _exceptions(self, w, cands):
        """Step 5: exceptions to rule 4 (applied before the "longest stem wins" rule)."""
        if any(not c.weak for c in cands):                # a tentative analysis is used only if there is no other
            cands = [c for c in cands if not c.weak]
        if len({c.cut for c in cands}) < 2:
            return cands
        drop, keep = set(), set()
        # (0) pair list: when two stems compete, the one named in the list wins (prefer.tsv).
        #     A word that is itself in the lexicon (rule 2) is not overridden by this list.
        present = {}
        for c in cands:
            present.setdefault((key(c.stem), 'v' if c.kind == 'V' else 'n'), []).append(c)
        for win, lose in self.d.prefer:
            if win in present and lose in present:
                drop.update(id(c) for c in present[lose] if c.path or c.kind == 'V')
                keep.update(id(c) for c in present[win])
        nominal = [c for c in cands if c.kind in ('N', 'R') and c.path and id(c) not in drop]
        for cv in cands:
            if cv.kind != 'V' or id(cv) in keep:
                continue
            if not cv.path:
                # (1) the word itself is only a verb stem, but it can also be analysed as noun + suffix:
                #     jılı → jıl, suwı → suw
                if nominal:
                    drop.add(id(cv))
                continue
            cls0, m0 = cv.path[0]
            classes = [c for c, _ in cv.path]
            for cn in nominal:
                ncls, nm = cn.path[0]
                extra = w[cn.cut:cv.cut]
                # (2) -ın, -ına, -ınıń: possessive (jumısına → jumıs + ı + na, not jumısın + a)
                if extra in ('ın', 'in', 'sın', 'sin') and ncls == 'PX' and nm == extra[:-1] and len(cn.path) > 1:
                    drop.add(id(cv))
                # (3) -nıń, -niń: genitive (usınıń → usı + nıń, not usın + ıń)
                if extra == 'n' and ncls == 'GEN' and cls0 == 'IMP':
                    drop.add(id(cv))
                # (4) -lar, -ler: plural (baslar → bas + lar, not basla + r; ańlar → ań + lar)
                if cls0 == 'PTCP' and m0 in ('r', 'ar', 'er') and ncls == 'PL' \
                        and cv.cut + len(m0) == cn.cut + 3:
                    drop.add(id(cv))
        for cv in cands:
            if cv.kind != 'V' or not cv.path or id(cv) in drop:
                continue
            classes = [c for c, _ in cv.path]
            for cn in nominal:
                if id(cn) in keep or cn.cut <= cv.cut or len(cn.path) != 1:
                    continue
                ncls, nm = cn.path[0]
                # (5) -aman, -asań, -amız, -asız: present-future tense (alaman → al + a + man, not ala + man)
                if classes[:2] in (['PRES', 'P1'], ['PRES', 'Q2']) and len(classes) <= 3 \
                        and ncls in ('PRED', 'PX', 'Q2'):
                    drop.add(id(cn))
                # (6) -ydı, -ydi: present-future tense (aydaydı → ayda + y + dı, not ayday + dı)
                if classes == ['PRES', 'P3'] and cv.path[0][1] == 'y' and ncls == 'ACC':
                    drop.add(id(cn))
                # (7) -maqta, -mekte: progressive present (barmaqta → bar + maqta, not barmaq + ta)
                if classes[0] == 'PROG' and ncls == 'LOC':
                    drop.add(id(cn))
        # (8) possessive -ń after a vowel yields to any other analysis:
        #     jazıń → jaz + ıń (imperative), jazbań → jaz + ba + ń, kimseń → kim + seń, jaqtıń → jaq + tıń
        for cn in nominal:
            if len(cn.path) != 1 or cn.path[0][0] != 'PX' or id(cn) in keep:
                continue
            if cn.path[0][1][0] == 'ń' and any(c.cut < cn.cut and id(c) not in drop for c in cands):
                drop.add(id(cn))
            # (9) -nıń, -niń: the genitive beats the 2nd person possessive (dalanıń → dala + nıń, not dalan + ıń)
            if cn.path[0][1] in ('ıń', 'iń') and any(
                    c.cut == cn.cut - 1 and c.path and c.path[0][0] == 'GEN' and len(c.path) == 1 for c in nominal):
                drop.add(id(cn))
        return [c for c in cands if id(c) not in drop]

    @staticmethod
    def _select(cands):
        """Of several analyses the one with the longest stem wins; on a tie, the nominal chain."""
        if not cands:
            return None
        return min(cands, key=lambda c: (-c.cut, _KIND_ORDER[c.kind]))

    def _light(self, stem):
        """Rule 3: light suffixes, the verbal noun and -ǵan/-gen are stripped even from a word in the lexicon."""
        def pure(path):
            classes = [c for c, _ in path]
            i = 0
            while i < len(classes) and classes[i] == 'NEG':
                i += 1
            if i < len(classes) and (classes[i] in ('VN', 'NMSH') or (classes[i] == 'PTCP' and path[i][1] in _GAN)):
                i += 1                                                          # jazıw, quwanısh, kelisken, aynımaǵan
            elif i:
                return False
            return all(c in T.LIGHT or (c in ('LOC', 'LOCP') and classes[j + 1:j + 2] == ['GI'])
                       for j, c in enumerate(classes) if j >= i)                # búgingi, awıldaǵı, aytqanday
        cands = [c for c in self._candidates(stem) if pure(c.path)]
        return self._select(self._exceptions(stem, cands))

    def _safe(self, w):
        """Safe mode: from a word not in the lexicon only nominal suffixes of three or more letters are stripped."""
        for cut in range(3, len(w) - 2):
            surf, rest = w[:cut], w[cut:]
            if not T.has_vowel(surf):
                continue
            for path in T.parse(surf, rest, 'START'):
                if not all(cls in T.SAFE and len(m) >= 3 for cls, m in path):
                    continue
                if path[0][1] == 'tan' and surf.endswith('s'):      # Qazaqstan, Tatarstan: -stan
                    continue
                return Analysis(w, surf, (surf,) + tuple(m for _, m in path),
                                tuple(c for c, _ in path), ('not in lexicon: safe mode',), 'safe')
        return Analysis(w, w, (w,), (), ('not in lexicon',), 'unchanged')

    def _analyze_simple(self, w):
        """Steps 2–6 for one normalised word without a hyphen."""
        if w in self._cache:
            return self._cache[w]
        a = self._analyze_uncached(w)
        if len(self._cache) < 200000:
            self._cache[w] = a
        return a

    def _analyze_uncached(self, w):
        if len(w) < 2 or not w.isalpha() or not T.has_vowel(w):
            return Analysis(w, w, (w,), (), (), 'unchanged')
        k = key(w)
        # --- step 2: ready answers ---
        if k in self.d.protected or k in self.d.function:
            return Analysis(w, w, (w,), (), ('protected word',), 'list')
        if k in self.d.ready:
            stem = self.d.ready[k]
            return Analysis(w, self.d.overrides.get(key(stem), stem), (w,), (), ('ready form',), 'list')
        # --- steps 3–5 ---
        cands = self._candidates(w)
        level = self._is_nom(w)
        if level:                                         # rule 2: a word found in the lexicon is not stripped
            cands.append(_Cand(len(w), w, 'N', [], weak=level == 1))
        elif self._is_verb(w):                            # not in the lexicon, a verb only through voice: tentative
            cands.append(_Cand(len(w), w, 'V', [], weak=k not in self.d.verb))
        # ambiguous pair (prefer.tsv, "="): if both stems fit, the word is left unchanged (qurıp, dúzedi)
        strong = [c for c in cands if not c.weak] or cands
        present = {(key(c.stem), 'v' if c.kind == 'V' else 'n') for c in strong}
        for a, b in self.d.ambiguous:
            if a in present and b in present:
                return Analysis(w, w, (w,), (), (f'ambiguous: {a[0]} / {b[0]}',), 'list')
        best = self._select(self._exceptions(w, cands))
        if best is None:
            return self._safe(w)
        stem, kind = best.stem, best.kind
        segs = [w[:best.cut]] + [m for _, m in best.path]
        classes = [c for c, _ in best.path]
        notes = [best.note] if best.note else []
        while kind == 'N' and key(stem) not in self.d.protected:
            nxt = self._light(stem)
            if nxt is None:
                break
            segs[:1] = [stem[:nxt.cut]] + [m for _, m in nxt.path]
            classes[:0] = [c for c, _ in nxt.path]
            if nxt.note:
                notes.append(nxt.note)
            stem, kind = nxt.stem, nxt.kind
        # --- step 6: voice (only for a stem found through a verbal chain, or one that is only a verb) ---
        if kind == 'V':
            stem = self._voice_reduce(stem, notes)
        if key(stem) in self.d.overrides:
            new = self.d.overrides[key(stem)]
            notes.append(f'override: {stem} → {new}')
            stem = new
        return Analysis(w, stem, tuple(segs), tuple(classes), tuple(notes), 'lexicon')
