"""Suffix table, allowed order and checks (steps 3 and 4).

Each class holds all variants of the suffixes that share one function.
NEXT says which class may follow which
(written left to right; this gives the same result as stripping the word from right to left).
"""

BACK = frozenset('aıou')            # back vowels (juwan)
FRONT = frozenset('áeióú')          # front vowels (jińishke)
VOWELS = BACK | FRONT


def _w(s):
    return tuple(s.split())


SLOTS = {
    # --- nominal chain ---
    'PL':   _w('lar ler'),
    'PX':   _w('ımız imiz ıńız ińiz mız miz ńız ńiz ım im ıń iń sı si m ń ı i'),
    'GEN':  _w('nıń niń dıń diń tıń tiń'),
    'DAT':  _w('ǵa ge qa ke'),
    'DATP': _w('na ne a e'),                      # dative after a possessive
    'ACC':  _w('nı ni dı di tı ti'),
    'ACCP': _w('n'),                              # accusative after the 3rd person possessive
    'ABL':  _w('dan den tan ten nan nen'),
    'LOC':  _w('da de ta te'),
    'LOCP': _w('nda nde'),                        # locative after the 3rd person possessive
    'PRED': _w('man men ban ben pan pen sań seń mız miz bız biz pız piz'),
    'Q2':   _w('mısań miseń bısań biseń pısań piseń mısız misiz bısız bisiz pısız pisiz'),
    # --- light suffixes ---
    'GI':   _w('ǵı gi qı ki'),
    'DAY':  _w('nday ndey day dey tay tey'),
    'SHA':  _w('nsha nshe sha she'),
    'JAN':  _w('jan'),
    'NIKI': _w('niki nıki diki dıki tiki tıki'),
    'DEG':  _w('ıraq irek ıraǵ ireg raq rek raǵ reg law lew'),
    'NUM':  _w('ınshı inshi nshı nshi laǵan legen lap lep aw ew'),
    # --- verbal chain ---
    'NEG':  _w('ma me ba be pa pe'),
    'PAST': _w('dı di tı ti'),
    'PRES': _w('a e y'),
    'COND': _w('sa se'),
    'PTCP': _w('atuǵın etuǵın ytuǵın ajaq ejaq yjaq ajaǵ ejaǵ yjaǵ ǵan gen qan ken '
               'mas mes bas bes pas pes ar er r'),
    'CVBP': _w('ıp ip p'),
    'CVB':  _w('ǵansha genshe qansha kenshe mastan mesten bastan besten pastan pesten ǵalı geli qalı keli'),
    'IMP':  _w('ıńız ińiz ayın eyin ayıq eyik ńız ńiz sın sin yın yin yıq yik ıń iń ń'),
    'INT':  _w('maqshı mekshi baqshı bekshi paqshı pekshi'),
    'OPT':  _w('ǵay gey qay key'),
    'PROG': _w('maqta mekte baqta bekte paqta pekte'),
    'SUP':  _w('maǵa mege baǵa bege paǵa pege'),
    'VN':   _w('ıw iw w'),
    'NMLZ': _w('lıq lik lıǵ lig'),              # -lıq/-lik after a participle
    'P1':   _w('man men ban ben pan pen sań seń mız miz bız biz pız piz sız siz'),   # full set
    'P2':   _w('ńız ńiz m ń q k'),                                                    # short set
    'P3':   _w('dı di tı ti'),
    'PLV':  _w('lar ler'),
}

_NOUN_CASES = ['GEN', 'DAT', 'ACC', 'ABL', 'LOC']

NEXT = {
    'START': ['NEG', 'PAST', 'PRES', 'COND', 'PTCP', 'CVBP', 'CVB', 'IMP', 'INT', 'OPT', 'PROG', 'SUP', 'VN',
              'PL', 'PX', *_NOUN_CASES, 'PRED', 'Q2', 'GI', 'DAY', 'SHA', 'JAN', 'NIKI', 'DEG', 'NUM',
              'P1', 'P2', 'NMLZ', 'END'],
    # verb
    'NEG':   ['PAST', 'PRES', 'COND', 'PTCP', 'CVBP', 'CVB', 'IMP', 'INT', 'OPT', 'VN', 'END'],
    'PAST':  ['P2', 'END'],
    'PRES':  ['P1', 'P3', 'Q2', 'END'],
    'COND':  ['P2', 'END'],
    'PTCP':  ['P1', 'NMLZ', 'PL', 'PX', *_NOUN_CASES, 'DAY', 'SHA', 'END'],
    'CVBP':  ['P1', 'P3', 'END'],
    'CVB':   ['END'],
    'IMP':   ['PLV', 'END'],
    'INT':   ['P1', 'END'],
    'OPT':   ['P1', 'END'],
    'PROG':  ['P1', 'END'],
    'SUP':   ['END'],
    'VN':    ['PL', 'PX', *_NOUN_CASES, 'END'],
    'NMLZ':  ['PX', *_NOUN_CASES, 'END'],
    'P1':    ['PLV', 'END'],
    'P2':    ['PLV', 'END'],
    'P3':    ['END'],
    'PLV':   ['END'],
    # noun
    'PL':    ['PX', *_NOUN_CASES, 'PRED', 'DAY', 'SHA', 'NIKI', 'END'],
    'PX':    ['GEN', 'DAT', 'DATP', 'ACC', 'ACCP', 'ABL', 'LOC', 'LOCP', 'PRED', 'DAY', 'SHA', 'END'],
    'GEN':   ['SHA', 'END'],
    'DAT':   ['END'],
    'DATP':  ['END'],
    'ACC':   ['END'],
    'ACCP':  ['END'],
    'ABL':   ['END'],
    'LOC':   ['GI', 'PRED', 'END'],
    'LOCP':  ['GI', 'END'],
    'PRED':  ['END'],
    'Q2':    ['END'],
    # after a light suffix the nominal chain starts again
    'GI':    ['PL', 'PX', *_NOUN_CASES, 'DAY', 'SHA', 'END'],
    'DAY':   ['END'],
    'SHA':   ['PL', 'PX', *_NOUN_CASES, 'END'],
    'JAN':   ['PL', 'PX', *_NOUN_CASES, 'END'],
    'NIKI':  ['DATP', 'ACCP', 'LOCP', 'ABL', 'GEN', 'PL', 'END'],
    'DEG':   ['PL', 'PX', 'END'],
    'NUM':   ['PL', 'PX', *_NOUN_CASES, 'PRED', 'END'],
}

VERB_FIRST = frozenset(['NEG', 'PAST', 'PRES', 'COND', 'PTCP', 'CVBP', 'CVB', 'IMP', 'INT', 'OPT', 'PROG',
                        'SUP', 'VN'])
BARE_FIRST = frozenset(['P1', 'P2', 'NMLZ'])       # only after atır, edi, eken ...
LIGHT = frozenset(['GI', 'DAY', 'SHA', 'JAN', 'NIKI', 'DEG', 'NUM', 'VN'])
SAFE = frozenset(['PL', 'PX', 'GEN', 'ABL'])       # classes stripped from words not in the lexicon (≥ 3 letters)

# A suffix-final q/k becomes ǵ/g before a vowel: -lıq → -lıǵ-ı, -raq → -raǵ-ı, -ajaq → -ajaǵ-ı
VOICED = frozenset(['lıǵ', 'lig', 'ıraǵ', 'ireg', 'raǵ', 'reg', 'ajaǵ', 'ejaǵ', 'yjaǵ'])
UNVOICED = frozenset(['lıq', 'lik', 'ıraq', 'irek', 'raq', 'rek', 'ajaq', 'ejaq', 'yjaq'])

# A loanword ending in nk, ks, kt, rk, st takes front-vowel suffixes: bank-ke, tekst-ti
LOAN_CLUSTERS = ('nk', 'ks', 'kt', 'rk', 'st')

_MBP = {'V': 'm', 'L': 'm', 'Z': 'b', 'N': 'b', 'T': 'p'}
_NDT_GEN = {'V': 'n', 'N': 'n', 'L': 'd', 'Z': 'd', 'T': 't'}
_NDT_ACC = {'V': 'n', 'N': 'd', 'L': 'd', 'Z': 'd', 'T': 't'}
_NDT_ABL = {'V': 'd', 'L': 'd', 'Z': 'd', 'N': 'n', 'T': 't'}
_MAN = {'V': 'm', 'L': 'm', 'N': 'm', 'Z': 'b', 'T': 'p'}
_MIZ = {'V': 'm', 'L': 'm', 'N': 'b', 'Z': 'b', 'T': 'p'}


def has_vowel(s):
    return any(c in VOWELS for c in s)


def vowel_count(s):
    return sum(1 for c in s if c in VOWELS)


def is_back(s):
    """Is the last syllable back (a part without vowels counts as back)."""
    for c in reversed(s):
        if c in VOWELS:
            return c in BACK
    return True


def sound_class(prev):
    """Class of the final sound: V (vowel), L (l, r, w, y), Z (z, j), N (m, n, ń), T (the rest)."""
    c = prev[-1]
    if c in VOWELS:
        return 'V'
    if c in 'lrwy':
        return 'L'
    if c in 'zj':
        return 'Z'
    if c in 'mnń':
        return 'N'
    return 'T'


def harmony_ok(prev, morph, cls):
    """Vowel harmony: do the vowels of the suffix agree with the last syllable of the preceding part."""
    mv = [c for c in morph if c in VOWELS]
    if not mv or cls in ('JAN', 'NIKI'):
        return True
    if morph.endswith('tuǵın') or morph[-3:] in ('jaq', 'jaǵ'):
        mv = mv[:1] if morph[0] in VOWELS else []        # -tuǵın, -jaq do not change
        if not mv:
            return True
    if not has_vowel(prev):
        return True
    back = is_back(prev)
    if all((v in BACK) == back for v in mv):
        return True
    if prev.endswith(LOAN_CLUSTERS) and all(v in FRONT for v in mv):
        return True
    if prev[-1] in 'xqǵ' and all(v in BACK for v in mv):          # tariyx-ı, tariyx-tan
        return True
    return False


def class_ok(prev, morph, cls, prevcls):
    """Allomorph choice: does this variant of the suffix fit the final sound of the preceding part."""
    sc = sound_class(prev)
    f = morph[0]
    last = prev[-1]
    # baǵ-da, baǵ-dan (but baǵ-qa); poezd-ǵa, poezd-dan
    scd = 'Z' if (last == 'ǵ' or prev.endswith('zd')) else sc
    scg = 'Z' if prev.endswith('zd') else sc
    after_px3 = prevcls in ('PX', 'NIKI') and last in 'ıi'

    if cls in ('PL', 'PLV', 'JAN', 'COND', 'NMLZ'):
        return True
    if cls == 'PX':
        short = morph in ('m', 'ń', 'sı', 'si', 'mız', 'miz', 'ńız', 'ńiz')
        return short == (sc == 'V')
    if cls == 'GEN':
        return f == _NDT_GEN[scd]
    if cls == 'ACC':
        return not after_px3 and f == _NDT_ACC[scd]
    if cls == 'DAT':
        if prevcls == 'PX' and last != 'z':               # -a/-e after -m, -ń; -na/-ne after the 3rd person
            return False
        if after_px3:
            return False
        return f in ('qk' if scg == 'T' else 'ǵg')
    if cls == 'LOC':
        return not after_px3 and f == ('t' if scd == 'T' else 'd')
    if cls == 'ABL':
        if after_px3:
            return f == 'n'
        return f == _NDT_ABL[scd]
    if cls == 'DATP':
        if morph in ('na', 'ne'):
            return after_px3
        return prevcls == 'PX' and last in 'mń'
    if cls in ('ACCP', 'LOCP'):
        return after_px3
    if cls in ('PRED', 'P1'):
        head = morph[:3]
        if head in ('man', 'men', 'ban', 'ben', 'pan', 'pen'):
            return f == _MAN[sc]
        if head in ('mız', 'miz', 'bız', 'biz', 'pız', 'piz'):
            return f == _MIZ[sc]
        return True
    if cls == 'Q2':
        return f == _MBP[sc]
    if cls == 'GI':
        return f in ('qk' if scg == 'T' else 'ǵg')
    if cls == 'DAY':
        if f == 'n':
            return after_px3
        return not after_px3 and f == ('t' if scd == 'T' else 'd')
    if cls == 'SHA':
        return (f == 'n') == after_px3
    if cls == 'NIKI':
        return f == _NDT_GEN[scd]
    if cls == 'DEG':
        if morph[0] == 'r':
            return sc == 'V'
        if morph[0] in 'ıi':
            return sc != 'V'
        return True
    if cls == 'NUM':
        if morph in ('nshı', 'nshi'):
            return sc == 'V'
        if morph in ('ınshı', 'inshi', 'aw', 'ew'):
            return sc != 'V'
        return True
    # --- verb ---
    if cls in ('NEG', 'INT', 'PROG', 'SUP'):
        return f == _MBP[sc]
    if cls == 'PAST':
        return f == ('t' if scd == 'T' else 'd')
    if cls == 'PRES':
        return (morph == 'y') == (sc == 'V')
    if cls == 'PTCP':
        if f in 'ǵgqk':
            return f in ('qk' if sc == 'T' else 'ǵg')
        if f in 'mbp':
            return f == _MBP[sc]
        if f in 'ry':                                     # -r, -ytuǵın, -yjaq
            return sc == 'V'
        return sc != 'V'                                  # -ar, -atuǵın, -ajaq
    if cls == 'CVBP':
        return (morph == 'p') == (sc == 'V')
    if cls == 'CVB':
        if f in 'mbp':
            return f == _MBP[sc]
        return f in ('qk' if sc == 'T' else 'ǵg')
    if cls == 'OPT':
        return f in ('qk' if sc == 'T' else 'ǵg')
    if cls == 'IMP':
        if morph in ('ń', 'ńız', 'ńiz', 'yın', 'yin', 'yıq', 'yik'):
            return sc == 'V'
        if morph in ('sın', 'sin'):
            return True
        return sc != 'V'
    if cls == 'VN':
        return (morph == 'w') == (sc == 'V')
    if cls == 'P2':
        if morph in ('q', 'k'):
            return (morph == 'q') == is_back(prev)
        return True
    if cls == 'P3':
        return (prevcls == 'CVBP') == (f == 't')
    return True


def parse(prev, rest, state, bare=False):
    """Split `rest` into suffixes, starting from the state `state`.

    Each result is a list [(class, suffix), ...]. Nothing is yielded if nothing fits.
    `bare`: does the stem take person suffixes directly (atır, edi, eken ...).
    """
    if not rest:
        if 'END' in NEXT[state]:
            yield []
        return
    for cls in NEXT[state]:
        if cls == 'END':
            continue
        if state == 'START' and cls in BARE_FIRST and not bare:
            continue
        for morph in SLOTS[cls]:
            if not rest.startswith(morph):
                continue
            after = rest[len(morph):]
            vowel_next = bool(after) and after[0] in VOWELS
            if morph in VOICED and not vowel_next:
                continue
            if morph in UNVOICED and vowel_next:
                continue
            if not harmony_ok(prev, morph, cls):
                continue
            if not class_ok(prev, morph, cls, state):
                continue
            for tail in parse(prev + morph, after, cls):
                yield [(cls, morph)] + tail
