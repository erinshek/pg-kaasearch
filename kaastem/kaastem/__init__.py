"""kaastem: a rule-based stemmer for the Karakalpak language (light, mode A).

    >>> from kaastem import stem
    >>> stem('mektebimizde')
    'mektep'
"""
from .normalize import normalize, tokenize
from .stemmer import Analysis, Stemmer

__version__ = '0.2.2'
__all__ = ['Stemmer', 'Analysis', 'stem', 'analyze', 'normalize', 'tokenize']

_default = None


def _get():
    global _default
    if _default is None:
        _default = Stemmer()
    return _default


def stem(word):
    """Return the stem of a word."""
    return _get().stem(word)


def analyze(word):
    """Return the analysis of a word (stem, segmentation, rules applied)."""
    return _get().analyze(word)
