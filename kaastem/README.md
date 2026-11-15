# kaastem — a stemmer for the Karakalpak language

A rule-based light (mode A) stemmer. It strips inflectional suffixes from a word and returns the stem
in its dictionary form. Derivational suffixes (-shı, -lıq, -la, -lı, -sız ...) are not stripped.

```python
from kaastem import stem, analyze

stem('mektebimizde')      # 'mektep'
stem('xalqımızdıń')       # 'xalıq'
stem('jazıladı')          # 'jaz'
stem('Мектепке')          # 'mektep'   (Cyrillic, apostrophe orthography and upper case are normalised)
print(analyze('kelgenligin'))
# kelgenligin → kel  [kel-gen-lig-i-n]
```

Command line:

```
python -m kaastem mektebimizde xalqımızdıń
python -m kaastem -e jazıladı                      # with the segmentation and the rules applied
python -m kaastem -t "Bizler mektepke bardıq."
python -m kaastem -f text.txt
```

Python 3.10 or newer. No other libraries are needed. Install with `pip install .`

## Test results (0.2.4)

| Table | Result |
|---|---|
| `tests/test_set.tsv` — 200 words verified by a native speaker | 200 / 200 |
| `tests/exceptions.tsv` — 100 words verified by a native speaker | 100 / 100 |
| `tests/reviewed.tsv` — entries added during implementation and answers to open questions, 129 words reviewed by a native speaker | 129 / 129 |
| `tests/extra.tsv` — spelling normalisation and rules added during implementation (not independently verified) | 56 / 56 |
| `tests/news_sample.tsv` — 297 random words from news, health and parallel-corpus texts, verified by a native speaker | 297 / 297 |
| Idempotence: `stem(stem(w)) == stem(w)` for the words above, the lexicon headwords and `tests/idempotence_forms.txt`; known exceptions in `tests/idempotence_exceptions.tsv` (39 homographs and misspellings) | 25,693 / 25,693 |

Run them with `python tests/run_tests.py` (`-v` shows every word).

In the text of 11 school textbooks (261 thousand words) 95 percent of the words are analysed through
a list or the lexicon; the remaining 5 percent are not in the lexicon (mostly personal names, surnames
and word fragments from exercises). This is not a measure of accuracy: how many of the analysed words are
stemmed correctly was checked on the 478 test words (428 of them verified by a native speaker) and on 300
random words from news, health and parallel-corpus texts: 290 of the 300 were stemmed correctly (96.7%).

## Layout

| File | Purpose | Step |
|---|---|---|
| `kaastem/normalize.py` | spelling normalisation, splitting a text into words | 1 |
| `kaastem/tables.py` | suffix table, suffix order, vowel and consonant checks | 3, 4 |
| `kaastem/stemmer.py` | ready answers, stem restoration, selection, voice | 2, 4, 5, 6 |
| `kaastem/lexicon.py` | loading the lexicon and the lists | |
| `kaastem/data/` | the lexicon and the lists (plain text, edited by hand) | |
| `tools/build_lexicon.py` | builds `lexicon.tsv` from the «Túsindirme sózlik» headword list | |
| `tools/induce_lexicon.py` | finds stems missing from the lexicon in a text (candidates) | |
| `tools/ambiguity_report.py` | lists the words that can be split into more than one stem | |

The steps are the six steps of the stemming procedure: (1) spelling normalisation, (2) ready answers,
(3) suffix stripping, (4) checks and stem restoration, (5) selection, (6) voice.

## Lists (`kaastem/data/`)

In every file the text after `#` is a comment. A list can be changed without touching the code.

| File | Contents | Entries |
|---|---|---|
| `lexicon.tsv` | headwords of «Túsindirme sózlik»: non-verbs (n) and verb stems (v) | 16 829 + 5 898 |
| `lexicon_extra.tsv` | stems missing from the dictionary headword list: added by hand and induced from text | 1538 |
| `lexicon_exclude.tsv` | entries removed from the lexicon (headwords that are inflected forms) | 48 |
| `protected.txt` | protected words (soraw, belgi, qanday, olar ...) | 94 |
| `function_words.txt` | postpositions, conjunctions, particles, modal words | 73 |
| `ready_forms.tsv` | ready forms (maǵan → men, aldı → al, aldında → aldı ...) | 55 |
| `overrides.tsv` | stem overrides (ákel → alıp kel ...) | 5 |
| `voice_stop.txt` | verbs whose voice suffix is not stripped | 107 |
| `voice_explicit.tsv` | explicitly listed voice forms (delin → de ...) | 2 |
| `prefer.tsv` | what to do when two stems compete (al / ald, ata / at, qur / qurı ...) | 27 |
| `gi_bases.txt`, `kinship.txt`, `numerals.txt` | bases after which -ǵı, -jan and the numeral suffixes are stripped | 28, 7, 22 |
| `bare_stems.txt` | stems that take person suffixes directly (atır, edi, eken ...) | 9 |
| `elision.txt` | words that drop a vowel before a possessive suffix (xalıq – xalqı) | 21 |

Of the entries added during implementation, 118 were reviewed by a native speaker on 2026-11-12:
90 were confirmed and 28 corrected. Entries not reviewed yet (they have no effect on the textbook and
dictionary texts): 12 in `protected.txt` and 3 in `prefer.tsv`, each in a separate section of the file,
and 6 in `lexicon_exclude.tsv` (edip, ekesiz, keteresiz, sıypalamay, sungimey, bek).

## About the lexicon

The source of the lexicon is the headword list of «Qaraqalpaq tiliniń túsindirme sózligi»
(`lexicon.txt` in github.com/alibekembergenovv-byte/qaraqalpaq-tili, 23 315 entries). Permission to use
it has been obtained.

That list is incomplete: of 370 basic words that were checked, about 110 are missing from it
(*al, qol, úy, óz, oqı, shıq, qoy, gáp, biz*). For this reason `lexicon_extra.tsv` was added:

- `manual` — 225 stems added by hand: pronouns, two-letter stems, words frequent in school textbooks;
- `corpus` — 527 stems induced from the word forms in school textbooks and the dictionary text. Each of
  them occurs with several different suffixes; the list was looked through and the errors were removed.
- `news` — 786 stems induced the same way from news, health and parallel-corpus texts (kknews.uz, shipaker.uz,
  the Dilmash corpus; 12.5 million words) and reviewed by a native speaker: 1150 candidates; 355 rejected by the reviewer, 3 removed because they break verified test words (ústin, ustin, taw as a verb).

If the complete headword list of the dictionary becomes available, the lexicon can be rebuilt:

```
python tools/build_lexicon.py lexicon.txt [forms.tsv ...] -o kaastem/data/lexicon.tsv
```

The dictionary list writes x for h (*xám, xár*). The stemmer treats the two as equal when comparing;
the spelling of the stem it returns does not change.

## Rules added during implementation

The approved procedure had the rule "if several analyses remain, the one with the longest stem wins".
Testing on text showed that this rule gives wrong results for some forms, so exceptions were added to it
(`stemmer.py`, `_exceptions`):

| Form | Chosen | Rejected |
|---|---|---|
| -ıń, -ń (imperative) | jazıń → jaz + ıń | jazı + ń |
| -lar, -ler (plural) | baslar → bas + lar | basla + r |
| -aman, -asań, -amız | alasań → al + a + sań | alasa + ń |
| -ydı | aydaydı → ayda + y + dı | ayday + dı |
| -maqta | barmaqta → bar + maqta | barmaq + ta |
| -ına, -ın, -ınıń (possessive) | jumısına → jumıs + ı + na | jumısın + a |
| -nıń (genitive) | dalanıń → dala + nıń | dalan + ıń |
| the word itself is only a verb | jılı → jıl + ı | jılı (verb) |

Other changes:

- A participle that the lexicon lists as an adjective is stripped too (*kelisken → kelis, birlesken → birles*);
  independent nouns (*qorǵan, tuwısqan*) are in `protected.txt`.
- A derived stem (*taza + lıq*), restoring a vowel in a word that is not in the elision list, and a double
  consonant are tentative analyses: they are used only if no other analysis is found.
- A function word cannot be a stem: *ushında = ush + ı + nda*.
- Pairs of competing stems are in `prefer.tsv`: *al / ald, qal / qala, ata / at, qur / qurı*. Four reviewed
  forms of ambiguous pairs are kept unchanged as ready forms (*qurıp, dúzedi, egedi, tiyedi*); other forms
  follow the pair (*qurıw → qur, dúzer → dúz*).
- Possessed headwords: *aldı, astı, ústi, keyni, aqırı, túri, bári*. A case suffix is taken to attach to
  this form: *aldında → aldı, ústinde → ústi*.
- Forms added to the table: -maqta, -ǵay, -maǵa, -mısań (with the question particle), -ajaǵı, -raǵı.
- The action noun -ısh/-ish is stripped like the verbal noun (*quwanısh → quwan, ókinish → ókin*);
  nouns that only look like it (*qılısh, tınısh, burısh*) are in `protected.txt`.

## Limitations

- From a word that is not in the lexicon only nominal suffixes of three or more letters are stripped
  (*Lissabonnan → lissabon*, *Lissabonǵa* is left unchanged). This gets rarer as the lexicon grows.
- Words that are spelled the same get one answer (*alma* is always a noun, *aldı* is always a verb).
- Case forms that the dictionary lists as adverbs are not stripped (*tómende, birden, keshte*).
- Stripping a voice suffix can move the meaning away from the original; such verbs are added to `voice_stop.txt`.
- The desiderative form (*barǵım keledi*) and -maq/-mek are not stripped: they clash with nouns such as
  *sıpırǵı, barmaq*.
- The output is always in lower case.
