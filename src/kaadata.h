/*
 * GENERATED FILE - DO NOT EDIT.
 * Source: kaastem 0.2.2 (kaastem/kaastem/tables.py, stemmer.py, data/).
 * Regenerate with: python3 tools/gen_tables.py
 */

#ifndef KAASEARCH_KAADATA_H
#define KAASEARCH_KAADATA_H

#include <stddef.h>
#include <stdint.h>

#define KAA_KAASTEM_VERSION "0.2.2"

/* Suffix classes, then the two pseudo-states START and END (bit positions in KaaMask). */
typedef enum
{
    KAA_PL = 0,
    KAA_PX = 1,
    KAA_GEN = 2,
    KAA_DAT = 3,
    KAA_DATP = 4,
    KAA_ACC = 5,
    KAA_ACCP = 6,
    KAA_ABL = 7,
    KAA_LOC = 8,
    KAA_LOCP = 9,
    KAA_PRED = 10,
    KAA_Q2 = 11,
    KAA_GI = 12,
    KAA_DAY = 13,
    KAA_SHA = 14,
    KAA_JAN = 15,
    KAA_NIKI = 16,
    KAA_DEG = 17,
    KAA_NUM = 18,
    KAA_NEG = 19,
    KAA_PAST = 20,
    KAA_PRES = 21,
    KAA_COND = 22,
    KAA_PTCP = 23,
    KAA_CVBP = 24,
    KAA_CVB = 25,
    KAA_IMP = 26,
    KAA_INT = 27,
    KAA_OPT = 28,
    KAA_PROG = 29,
    KAA_SUP = 30,
    KAA_VN = 31,
    KAA_NMSH = 32,
    KAA_NMLZ = 33,
    KAA_P1 = 34,
    KAA_P2 = 35,
    KAA_P3 = 36,
    KAA_PLV = 37,
    KAA_START = 38,
    KAA_END = 39,
    KAA_NSTATES
} KaaClass;

#define KAA_NCLASSES 38

typedef uint64_t KaaMask;
#define KAA_BIT(c) ((KaaMask) 1 << (c))

/* One suffix variant (morph) of a class. */
typedef struct
{
    KaaClass    cls;
    const char *morph;
} KaaMorph;

/* A sorted list of words (UTF-8 byte order): look up with bsearch + strcmp. */
typedef struct
{
    const char *const *words;
    size_t      n;
} KaaWordSet;

/* A sorted map from a word to a string. */
typedef struct
{
    const char *key;
    const char *value;
} KaaPair;

typedef struct
{
    const KaaPair *items;
    size_t      n;
} KaaPairMap;

/* A ready form a suffix chain may follow: form, its stem, the state the chain continues from
   (KAA_START for a stem that takes person suffixes directly). */
typedef struct
{
    const char *form;
    const char *stem;
    KaaClass    state;
} KaaReady;

/* Two competing stems; kind is 'n' (non-verb) or 'v' (verb). */
typedef struct
{
    const char *first;
    char        first_kind;
    const char *second;
    char        second_kind;
} KaaStemPair;

/* Derivational suffixes in back/front pairs; after_vowel: -1 any, 0 consonant, 1 vowel. */
typedef struct
{
    const char *back;
    const char *front;
    int         after_vowel;
} KaaDeriv;

/* --- suffix table (tables.py) --- */
extern const KaaMorph kaa_morphs[];
extern const size_t kaa_nmorphs;
extern const KaaMask kaa_next[KAA_NSTATES];       /* allowed classes after each state */
extern const KaaClass kaa_next_order[KAA_NSTATES][KAA_NSTATES + 1];  /* the same, in order; ends with KAA_NSTATES */
extern const KaaMask kaa_verb_first, kaa_bare_first, kaa_light, kaa_safe;
extern const KaaWordSet kaa_voiced_morphs, kaa_unvoiced_morphs, kaa_loan_clusters;
extern const char *const kaa_class_names[KAA_NSTATES];

/* --- word lists (data/, as loaded by kaastem.lexicon.Data; keys use x for h) --- */
extern const KaaWordSet kaa_nom;         /* non-verb stems (lexicon + lists) */
extern const KaaWordSet kaa_verb;        /* verb stems */
extern const KaaWordSet kaa_protected;   /* protected.txt */
extern const KaaWordSet kaa_function;    /* function_words.txt */
extern const KaaWordSet kaa_voice_stop;  /* voice_stop.txt */
extern const KaaWordSet kaa_gi_bases;    /* gi_bases.txt */
extern const KaaWordSet kaa_kinship;     /* kinship.txt */
extern const KaaWordSet kaa_numerals;    /* numerals.txt */
extern const KaaWordSet kaa_bare;        /* bare_stems.txt */
extern const KaaWordSet kaa_elision;     /* elision.txt */
extern const KaaPairMap kaa_ready;                /* ready_forms.tsv: the word itself */
extern const KaaReady kaa_ready_prefix[];         /* ready_forms.tsv: forms a suffix may follow */
extern const size_t kaa_nready_prefix;
extern const KaaPairMap kaa_overrides;            /* overrides.tsv */
extern const KaaPairMap kaa_voice_explicit;       /* voice_explicit.tsv */
extern const KaaStemPair kaa_prefer[];            /* prefer.tsv, in file order */
extern const size_t kaa_nprefer;
extern const KaaStemPair kaa_ambiguous[];         /* prefer.tsv, pairs marked "=" */
extern const size_t kaa_nambiguous;

/* --- stemmer constants (stemmer.py) --- */
extern const char *const kaa_voice_suffixes[];    /* in the order they are tried */
extern const size_t kaa_nvoice_suffixes;
extern const KaaWordSet kaa_particles, kaa_gan, kaa_not_base, kaa_deriv_fixed;
extern const KaaDeriv kaa_deriv_harmonic[];
extern const size_t kaa_nderiv_harmonic;
extern const KaaDeriv kaa_deriv_verbal[];
extern const size_t kaa_nderiv_verbal;

/* --- NFC composition of a letter and a following combining mark (normalize.py) --- */
typedef struct
{
    uint32_t    base;
    uint32_t    mark;
    uint32_t    composed;
} KaaCompose;
extern const KaaCompose kaa_compose[];            /* sorted by base, then mark */
extern const size_t kaa_ncompose;

#endif /* KAASEARCH_KAADATA_H */
