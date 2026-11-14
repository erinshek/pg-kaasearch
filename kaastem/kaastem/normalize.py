"""Step 1: spelling normalisation.

The result is always in the 2016 Latin alphabet and in lower case.
The Cyrillic → Latin table follows the table of the `kaalin` library.
"""
import re
import unicodedata

_CYR = {
    'а': 'a', 'ә': 'á', 'б': 'b', 'в': 'v', 'г': 'g', 'ғ': 'ǵ', 'д': 'd', 'е': 'e', 'ё': 'yo', 'ж': 'j',
    'з': 'z', 'и': 'i', 'й': 'y', 'к': 'k', 'қ': 'q', 'л': 'l', 'м': 'm', 'н': 'n', 'ң': 'ń', 'о': 'o',
    'ө': 'ó', 'п': 'p', 'р': 'r', 'с': 's', 'т': 't', 'у': 'u', 'ү': 'ú', 'ў': 'w', 'ф': 'f', 'х': 'x',
    'ҳ': 'h', 'ц': 'c', 'ч': 'ch', 'ш': 'sh', 'щ': 'sh', 'ъ': '', 'ы': 'ı', 'ь': '', 'э': 'e', 'ю': 'yu',
    'я': 'ya',
}
# a vowel after ъ, ь: объект → obyekt, павильон → pavilyon, Ильин → ilyin
_CYR_AFTER_SIGN = {'е': 'ye', 'о': 'yo', 'и': 'yi'}
# Cyrillic look-alike letters found inside Latin words (they look the same)
_HOMOGLYPH = str.maketrans({
    'а': 'a', 'е': 'e', 'о': 'o', 'р': 'p', 'с': 'c', 'х': 'x', 'у': 'y', 'і': 'i',
    'А': 'A', 'В': 'B', 'Е': 'E', 'К': 'K', 'М': 'M', 'Н': 'H', 'О': 'O', 'Р': 'P', 'С': 'C', 'Т': 'T',
    'Х': 'X', 'У': 'Y', 'І': 'I',
})
# letters that old fonts put in place of Karakalpak letters: ѳ → ө, ӊ → ң, ӯ ӳ ӱ → ў, ӽ → ҳ
_CYR_VARIANT = str.maketrans({
    'ѳ': 'ө', 'Ѳ': 'Ө', 'ӊ': 'ң', 'Ӊ': 'Ң', 'ӯ': 'ў', 'Ӯ': 'Ў', 'ӳ': 'ў', 'Ӳ': 'Ў', 'ӱ': 'ў', 'Ӱ': 'Ў',
    'ӽ': 'ҳ', 'Ӽ': 'Ҳ',
})
APOSTROPHES = "'’‘ʻʼ`′´"
_APO_LETTERS = {'a': 'á', 'g': 'ǵ', 'i': 'ı', 'n': 'ń', 'o': 'ó', 'u': 'ú'}
_INVISIBLE = dict.fromkeys(map(ord, '­​‌‍⁠﻿'))
_DASHES = dict.fromkeys(map(ord, '‐‑‒–'), '-')
_LOWER = {ord('I'): 'i', ord('Í'): 'ı', ord('İ'): 'i', ord('í'): 'ı'}

_CYR_RE = re.compile('[а-яёәғқңөүўҳіА-ЯЁӘҒҚҢӨҮЎҲІ]')
_LAT_RE = re.compile('[a-zA-ZáǵıńóúÁǴÍŃÓÚ]')
_APO_RE = re.compile('([aginou])[' + re.escape(APOSTROPHES) + ']')


def lower(text):
    """Lower-case the text: Í → ı, I → i (the standard .lower() gets these wrong)."""
    return text.translate(_LOWER).lower()


def cyr_to_lat(word):
    """Convert a (lower-case) Cyrillic word to Latin."""
    out = []
    prev = ''
    for ch in word:
        if prev in ('ъ', 'ь') and ch in _CYR_AFTER_SIGN:
            out.append(_CYR_AFTER_SIGN[ch])
        else:
            out.append(_CYR.get(ch, ch))
        prev = ch
    return ''.join(out)


def normalize(word):
    """Bring one word (token) to the uniform spelling."""
    w = unicodedata.normalize('NFC', word).translate(_INVISIBLE).translate(_DASHES).translate(_CYR_VARIANT).strip()
    if _CYR_RE.search(w) and _LAT_RE.search(w):
        w = w.translate(_HOMOGLYPH)                              # shаqаp (Cyrillic а) → shaqap
    w = lower(w)
    if _CYR_RE.search(w):
        w = cyr_to_lat(w)                                        # мектепке → mektepke
    w = _APO_RE.sub(lambda m: _APO_LETTERS[m.group(1)], w)      # so'z → sóz, qi'z → qız
    return unicodedata.normalize('NFC', w)


_A = re.escape(APOSTROPHES)
_TOKEN_RE = re.compile(r"[^\W_]+(?:[-­‐‑" + _A + r"][^\W_]+)*[" + _A + r"]?", re.UNICODE)


def tokenize(text):
    """Split a text into words. A hyphenated pair (ata-ana) and «5-mektepke» are single tokens."""
    return _TOKEN_RE.findall(text)
