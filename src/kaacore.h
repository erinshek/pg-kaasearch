/*
 * kaacore: the Karakalpak stemmer core in C.
 *
 * A port of kaastem (the Python reference stemmer in kaastem/) that gives the same result for every
 * word. It does not depend on PostgreSQL: memory comes from malloc and is released by the caller with
 * kaa_analysis_free() / free(). The data (suffix table, lexicon, lists) is in kaadata.c, generated
 * from kaastem by tools/gen_tables.py.
 *
 * The six steps of the stemming procedure:
 *   1. spelling normalisation           kaa_normalize()
 *   2. ready answers                    protected words, function words, ready forms, overrides
 *   3. suffix stripping                 suffix table and allowed order (kaadata.c)
 *   4. checks and stem restoration      vowel harmony, allomorph choice, p/b, k/g, q/ǵ, dropped vowels
 *   5. selection                        longest stem, its exceptions, pair list, light suffixes, safe mode
 *   6. voice                            voice suffixes and the voice stop list
 */
#ifndef KAASEARCH_KAACORE_H
#define KAASEARCH_KAACORE_H

#include <stddef.h>

#include "kaadata.h"

/* Words longer than this (in characters, after normalisation) are returned unchanged. */
#define KAA_MAX_WORD 64

typedef enum
{
    KAA_SOURCE_LIST,            /* a ready answer from a list */
    KAA_SOURCE_LEXICON,         /* analysed with the lexicon */
    KAA_SOURCE_SAFE,            /* not in the lexicon: only long nominal suffixes stripped */
    KAA_SOURCE_UNCHANGED        /* not in the lexicon, nothing stripped */
} KaaSource;

typedef struct
{
    char       *word;           /* the normalised word */
    char       *stem;
    char      **segments;       /* surface segmentation: mekteb | imiz | de */
    int         nsegments;
    KaaClass   *classes;        /* suffix classes: PX LOC */
    int         nclasses;
    KaaSource   source;
} KaaAnalysis;

/* Normalise one word (step 1): lower case, Cyrillic → Latin, apostrophe spelling → 2016 Latin.
 * Returns a malloc'd, null-terminated UTF-8 string, or NULL when out of memory. */
char       *kaa_normalize(const char *word, size_t len);

/* Analyse one word. Returns 0 on success, -1 when out of memory (out is then empty). */
int         kaa_analyze(const char *word, size_t len, KaaAnalysis *out);

/* Release what kaa_analyze() allocated. */
void        kaa_analysis_free(KaaAnalysis *a);

/* The stem of one word as a malloc'd string, or NULL when out of memory. */
char       *kaa_stem_word(const char *word, size_t len);

/* Name of a source as in kaastem: "list", "lexicon", "safe", "unchanged". */
const char *kaa_source_name(KaaSource source);

#endif /* KAASEARCH_KAACORE_H */
