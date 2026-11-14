/*
 * kaacore: the Karakalpak stemmer core in C. See kaacore.h.
 *
 * Every function here mirrors a function of kaastem (kaastem/kaastem/ *.py) with the same name, and
 * the comments name it. The order of loops and the order of the tables follow kaastem exactly,
 * because the order decides between analyses that tie.
 *
 * Words are handled as arrays of Unicode code points (KaaWord), so lengths and positions are counted
 * in characters, as in Python. A word is encoded back to UTF-8 only to look it up in a list.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "kaacore.h"

typedef uint32_t cp_t;

#define WORD_CAP    (KAA_MAX_WORD + 16)
#define MAX_DEPTH   24
#define KEY_BYTES   (WORD_CAP * 4 + 1)

typedef struct
{
    cp_t        c[WORD_CAP];
    int         n;
} KaaWord;

/* A suffix chain: indexes into kaa_morphs. */
typedef struct
{
    int         n;
    short       m[MAX_DEPTH];
} Path;

typedef struct
{
    int         cut;            /* length of the surface stem */
    KaaWord     stem;           /* restored stem */
    char        kind;           /* 'N' nominal chain, 'V' verbal chain, 'B' atır/edi/eken, 'R' ready form */
    Path        path;
    bool        weak;           /* tentative analysis */
} Cand;

typedef struct
{
    Cand       *items;
    int         n;
    int         cap;
} CandList;

typedef struct
{
    Path       *items;
    int         n;
    int         cap;
} PathList;

typedef struct
{
    bool        oom;
} Ctx;

/* ======================================================================== constants and init */

#define CP_I_DOTLESS 0x0131     /* ı */
#define CP_A_ACUTE   0x00E1     /* á */
#define CP_O_ACUTE   0x00F3     /* ó */
#define CP_U_ACUTE   0x00FA     /* ú */
#define CP_G_ACUTE   0x01F5     /* ǵ */
#define CP_N_ACUTE   0x0144     /* ń */

/* A constant string as code points. */
typedef struct
{
    cp_t        c[16];
    int         n;
} KStr;

static bool inited = false;
static KStr *morph_cp;                      /* kaa_morphs[i] as code points */
static int  class_first[KAA_NCLASSES];      /* morph index range of each class */
static int  class_last[KAA_NCLASSES];

/*
 * Morphs of each class grouped by their first letter, for parse(): only the morphs that start with
 * the next letter of the word are tried. Within a group the table order is kept, so the analyses come
 * out in the same order as in kaastem.
 */
typedef struct
{
    cp_t        first;
    short       start;          /* into group_morphs */
    short       count;
} MorphGroup;

/* Per-morph flags for class_ok(), precomputed from the literal lists of tables.class_ok. */
enum
{
    MF_PX_SHORT = 1 << 0,       /* PX: m ń sı si mız miz ńız ńiz */
    MF_MAN_HEAD = 1 << 1,       /* PRED/P1: starts with man men ban ben pan pen */
    MF_MIZ_HEAD = 1 << 2,       /* PRED/P1: starts with mız miz bız biz pız piz */
    MF_NA_NE = 1 << 3,          /* DATP: na ne */
    MF_NSHI = 1 << 4,           /* NUM: nshı nshi */
    MF_NUM_C = 1 << 5,          /* NUM: ınshı inshi aw ew */
    MF_IMP_V = 1 << 6,          /* IMP: ń ńız ńiz yın yin yıq yik */
    MF_SIN = 1 << 7             /* IMP: sın sin */
};
static unsigned short *morph_flags;

/* Constant suffixes of stemmer.py as code points. */
static KStr deriv_harm[16][2];
static KStr deriv_fixed[16];
static KStr deriv_verbal[16][2];
static int  voice_class[32];    /* 1: dır dir ǵız giz, 2: tır tir qız kiz, 0: other */

#define MAX_GROUPS 16
static MorphGroup class_groups[KAA_NCLASSES][MAX_GROUPS];
static int  class_ngroups[KAA_NCLASSES];
static short *group_morphs;
static bool *morph_voiced, *morph_unvoiced, *morph_gan;
static KStr voice_cp[32];
static KStr loan_cp[8];
static int  nloan;

/* ------------------------------------------------------------------------ UTF-8 */

static int
utf8_decode(const unsigned char *p, const unsigned char *end, cp_t *cp)
{
    size_t      avail = end - p;

    if (p[0] < 0x80)
    {
        *cp = p[0];
        return 1;
    }
    if ((p[0] & 0xE0) == 0xC0 && avail >= 2 && (p[1] & 0xC0) == 0x80)
    {
        *cp = ((cp_t) (p[0] & 0x1F) << 6) | (p[1] & 0x3F);
        return 2;
    }
    if ((p[0] & 0xF0) == 0xE0 && avail >= 3 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80)
    {
        *cp = ((cp_t) (p[0] & 0x0F) << 12) | ((cp_t) (p[1] & 0x3F) << 6) | (p[2] & 0x3F);
        return 3;
    }
    if ((p[0] & 0xF8) == 0xF0 && avail >= 4 && (p[1] & 0xC0) == 0x80 &&
        (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80)
    {
        *cp = ((cp_t) (p[0] & 0x07) << 18) | ((cp_t) (p[1] & 0x3F) << 12) |
            ((cp_t) (p[2] & 0x3F) << 6) | (p[3] & 0x3F);
        return 4;
    }
    *cp = 0xFFFD;                           /* malformed byte */
    return 1;
}

static int
utf8_encode(cp_t cp, char *out)
{
    if (cp < 0x80)
    {
        out[0] = (char) cp;
        return 1;
    }
    if (cp < 0x800)
    {
        out[0] = (char) (0xC0 | (cp >> 6));
        out[1] = (char) (0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000)
    {
        out[0] = (char) (0xE0 | (cp >> 12));
        out[1] = (char) (0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char) (0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char) (0xF0 | (cp >> 18));
    out[1] = (char) (0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char) (0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char) (0x80 | (cp & 0x3F));
    return 4;
}

static void
kstr_set(KStr *k, const char *s)
{
    const unsigned char *p = (const unsigned char *) s;
    const unsigned char *end = p + strlen(s);

    k->n = 0;
    while (p < end && k->n < 16)
        p += utf8_decode(p, end, &k->c[k->n++]);
}

static bool
kstr_eq(const KStr *a, const char *s)
{
    KStr        b;

    kstr_set(&b, s);
    return a->n == b.n && memcmp(a->c, b.c, a->n * sizeof(cp_t)) == 0;
}

static bool
in_list(const KStr *m, const char *const *lits)
{
    for (; *lits; lits++)
        if (kstr_eq(m, *lits))
            return true;
    return false;
}

static void
kaa_init(void)
{
    size_t      i;

    if (inited)
        return;
    morph_cp = calloc(kaa_nmorphs, sizeof(KStr));
    morph_voiced = calloc(kaa_nmorphs, sizeof(bool));
    morph_unvoiced = calloc(kaa_nmorphs, sizeof(bool));
    morph_gan = calloc(kaa_nmorphs, sizeof(bool));
    if (!morph_cp || !morph_voiced || !morph_unvoiced || !morph_gan)
        abort();                            /* a few kilobytes, once per process */
    for (i = 0; i < KAA_NCLASSES; i++)
        class_first[i] = class_last[i] = -1;
    for (i = 0; i < kaa_nmorphs; i++)
    {
        int         c = kaa_morphs[i].cls;
        kstr_set(&morph_cp[i], kaa_morphs[i].morph);
        if (class_first[c] < 0)
            class_first[c] = (int) i;
        class_last[c] = (int) i;
    }
    for (i = 0; i < kaa_nmorphs; i++)
    {
        size_t      j;
        const char *m = kaa_morphs[i].morph;

        morph_voiced[i] = morph_unvoiced[i] = morph_gan[i] = false;
        for (j = 0; j < kaa_voiced_morphs.n; j++)
            morph_voiced[i] |= strcmp(m, kaa_voiced_morphs.words[j]) == 0;
        for (j = 0; j < kaa_unvoiced_morphs.n; j++)
            morph_unvoiced[i] |= strcmp(m, kaa_unvoiced_morphs.words[j]) == 0;
        for (j = 0; j < kaa_gan.n; j++)
            morph_gan[i] |= strcmp(m, kaa_gan.words[j]) == 0;
    }
    group_morphs = calloc(kaa_nmorphs, sizeof(short));
    if (!group_morphs)
        abort();
    {
        int         pos = 0,
                    c;

        for (c = 0; c < KAA_NCLASSES; c++)
        {
            int         mi;

            class_ngroups[c] = 0;
            for (mi = class_first[c]; mi >= 0 && mi <= class_last[c]; mi++)
            {
                cp_t        f = morph_cp[mi].c[0];
                int         g,
                            seen = 0;

                for (g = 0; g < class_ngroups[c]; g++)
                    if (class_groups[c][g].first == f)
                        seen = 1;
                if (seen)
                    continue;
                if (class_ngroups[c] == MAX_GROUPS)
                    abort();                /* the generated table never has this many */
                class_groups[c][class_ngroups[c]].first = f;
                class_groups[c][class_ngroups[c]].start = (short) pos;
                for (g = mi; g <= class_last[c]; g++)
                    if (morph_cp[g].c[0] == f)
                        group_morphs[pos++] = (short) g;
                class_groups[c][class_ngroups[c]].count = (short) (pos - class_groups[c][class_ngroups[c]].start);
                class_ngroups[c]++;
            }
        }
    }
    for (i = 0; i < kaa_nvoice_suffixes && i < 32; i++)
    {
        kstr_set(&voice_cp[i], kaa_voice_suffixes[i]);
        voice_class[i] = (kstr_eq(&voice_cp[i], "d\304\261r") || kstr_eq(&voice_cp[i], "dir") ||
                          kstr_eq(&voice_cp[i], "\307\265\304\261z") || kstr_eq(&voice_cp[i], "giz")) ? 1 :
            (kstr_eq(&voice_cp[i], "t\304\261r") || kstr_eq(&voice_cp[i], "tir") ||
             kstr_eq(&voice_cp[i], "q\304\261z") || kstr_eq(&voice_cp[i], "kiz")) ? 2 : 0;
    }
    if (kaa_nderiv_harmonic > 16 || kaa_deriv_fixed.n > 16 || kaa_nderiv_verbal > 16 || kaa_nvoice_suffixes > 32)
        abort();                            /* the arrays above are sized for the generated tables */
    for (i = 0; i < kaa_nderiv_harmonic; i++)
    {
        kstr_set(&deriv_harm[i][0], kaa_deriv_harmonic[i].back);
        kstr_set(&deriv_harm[i][1], kaa_deriv_harmonic[i].front);
    }
    for (i = 0; i < kaa_deriv_fixed.n; i++)
        kstr_set(&deriv_fixed[i], kaa_deriv_fixed.words[i]);
    for (i = 0; i < kaa_nderiv_verbal; i++)
    {
        kstr_set(&deriv_verbal[i][0], kaa_deriv_verbal[i].back);
        kstr_set(&deriv_verbal[i][1], kaa_deriv_verbal[i].front);
    }
    morph_flags = calloc(kaa_nmorphs, sizeof(unsigned short));
    if (!morph_flags)
        abort();
    {
        static const char *const px_short[] = {"m", "\305\204", "s\304\261", "si", "m\304\261z", "miz",
        "\305\204\304\261z", "\305\204iz", NULL};
        static const char *const man_heads[] = {"man", "men", "ban", "ben", "pan", "pen", NULL};
        static const char *const miz_heads[] = {"m\304\261z", "miz", "b\304\261z", "biz", "p\304\261z", "piz", NULL};
        static const char *const na_ne[] = {"na", "ne", NULL};
        static const char *const nshi[] = {"nsh\304\261", "nshi", NULL};
        static const char *const num_c[] = {"\304\261nsh\304\261", "inshi", "aw", "ew", NULL};
        static const char *const imp_v[] = {"\305\204", "\305\204\304\261z", "\305\204iz", "y\304\261n", "yin",
        "y\304\261q", "yik", NULL};
        static const char *const sin[] = {"s\304\261n", "sin", NULL};

        for (i = 0; i < kaa_nmorphs; i++)
        {
            KStr        head = morph_cp[i];
            unsigned short f = 0;

            if (head.n > 3)
                head.n = 3;
            f |= in_list(&morph_cp[i], px_short) ? MF_PX_SHORT : 0;
            f |= in_list(&head, man_heads) ? MF_MAN_HEAD : 0;
            f |= in_list(&head, miz_heads) ? MF_MIZ_HEAD : 0;
            f |= in_list(&morph_cp[i], na_ne) ? MF_NA_NE : 0;
            f |= in_list(&morph_cp[i], nshi) ? MF_NSHI : 0;
            f |= in_list(&morph_cp[i], num_c) ? MF_NUM_C : 0;
            f |= in_list(&morph_cp[i], imp_v) ? MF_IMP_V : 0;
            f |= in_list(&morph_cp[i], sin) ? MF_SIN : 0;
            morph_flags[i] = f;
        }
    }
    nloan = 0;
    for (i = 0; i < kaa_loan_clusters.n && i < 8; i++)
        kstr_set(&loan_cp[nloan++], kaa_loan_clusters.words[i]);
    inited = true;
}

/* ------------------------------------------------------------------------ word helpers */

static void
word_set(KaaWord *w, const cp_t *c, int n)
{
    if (n > WORD_CAP)
        n = WORD_CAP;
    memcpy(w->c, c, n * sizeof(cp_t));
    w->n = n;
}

static void
word_from_utf8(KaaWord *w, const char *s)
{
    const unsigned char *p = (const unsigned char *) s;
    const unsigned char *end = p + strlen(s);

    w->n = 0;
    while (p < end && w->n < WORD_CAP)
        p += utf8_decode(p, end, &w->c[w->n++]);
}

static bool
ends_with(const cp_t *s, int n, const KStr *suf)
{
    return n >= suf->n && memcmp(s + n - suf->n, suf->c, suf->n * sizeof(cp_t)) == 0;
}

static bool
seg_eq(const cp_t *s, int n, const char *lit)
{
    KStr        k;

    kstr_set(&k, lit);
    return n == k.n && memcmp(s, k.c, n * sizeof(cp_t)) == 0;
}

/* kaastem.lexicon.key: h → x, except in sh and ch. Writes UTF-8 into buf. */
static void
key_utf8(const cp_t *s, int n, char *buf)
{
    int         i,
                pos = 0;

    for (i = 0; i < n; i++)
    {
        cp_t        c = s[i];

        if (c == 'h' && !(i > 0 && (s[i - 1] == 's' || s[i - 1] == 'c')))
            c = 'x';
        pos += utf8_encode(c, buf + pos);
    }
    buf[pos] = '\0';
}

static void
raw_utf8(const cp_t *s, int n, char *buf)
{
    int         i,
                pos = 0;

    for (i = 0; i < n; i++)
        pos += utf8_encode(s[i], buf + pos);
    buf[pos] = '\0';
}

static int
cmp_str(const void *a, const void *b)
{
    return strcmp(*(const char *const *) a, *(const char *const *) b);
}

/*
 * Word sets are looked up through hash tables built on first use (open addressing, FNV-1a); the
 * generated arrays stay the single source. Small sets keep using bsearch.
 */
typedef struct
{
    const KaaWordSet *set;
    uint32_t   *slots;          /* index + 1 into set->words; 0 = empty */
    uint32_t    mask;
} SetHash;

#define MAX_HASHED 16
static SetHash set_hashes[MAX_HASHED];
static int  nset_hashes;

static uint32_t
fnv1a(const char *k)
{
    uint32_t    h = 2166136261u;

    for (; *k; k++)
        h = (h ^ (unsigned char) *k) * 16777619u;
    return h;
}

static SetHash *
set_hash(const KaaWordSet *set)
{
    int         i;
    SetHash    *sh;
    uint32_t    size = 64;
    size_t      w;

    for (i = 0; i < nset_hashes; i++)
        if (set_hashes[i].set == set)
            return &set_hashes[i];
    if (nset_hashes == MAX_HASHED)
        return NULL;
    while (size < set->n * 2)
        size *= 2;
    sh = &set_hashes[nset_hashes];
    sh->slots = calloc(size, sizeof(uint32_t));
    if (!sh->slots)
        return NULL;                        /* fall back to bsearch */
    sh->set = set;
    sh->mask = size - 1;
    for (w = 0; w < set->n; w++)
    {
        uint32_t    h = fnv1a(set->words[w]) & sh->mask;

        while (sh->slots[h])
            h = (h + 1) & sh->mask;
        sh->slots[h] = (uint32_t) w + 1;
    }
    nset_hashes++;
    return sh;
}

static bool
set_has_utf8(const KaaWordSet *set, const char *k)
{
    SetHash    *sh;

    if (!set->n)
        return false;
    if (set->n >= 64 && (sh = set_hash(set)) != NULL)
    {
        uint32_t    h = fnv1a(k) & sh->mask;

        while (sh->slots[h])
        {
            if (strcmp(set->words[sh->slots[h] - 1], k) == 0)
                return true;
            h = (h + 1) & sh->mask;
        }
        return false;
    }
    return bsearch(&k, set->words, set->n, sizeof(char *), cmp_str) != NULL;
}

/* ------------------------------------------------------------------------ user lexicon */

typedef struct
{
    char      **slots;          /* keyed words; NULL = empty */
    uint32_t    mask;
    size_t      n;
} StrSet;

struct KaaExtra
{
    StrSet      nom;
    StrSet      verb;
};

/* The user lexicon of the call in progress (set by kaa_analyze_ex; the core is not reentrant). */
static const KaaExtra *cur_extra;

static bool
strset_has(const StrSet *ss, const char *k)
{
    uint32_t    h;

    if (!ss->slots)
        return false;
    h = fnv1a(k) & ss->mask;
    while (ss->slots[h])
    {
        if (strcmp(ss->slots[h], k) == 0)
            return true;
        h = (h + 1) & ss->mask;
    }
    return false;
}

static int
strset_add(StrSet *ss, const char *k)
{
    uint32_t    h;

    if (strset_has(ss, k))
        return 0;
    if (!ss->slots || (ss->n + 1) * 2 > ss->mask + 1)
    {
        uint32_t    size = ss->slots ? (ss->mask + 1) * 2 : 64;
        char      **slots = calloc(size, sizeof(char *));
        uint32_t    i;

        if (!slots)
            return -1;
        if (ss->slots)
        {
            for (i = 0; i <= ss->mask; i++)
                if (ss->slots[i])
                {
                    h = fnv1a(ss->slots[i]) & (size - 1);
                    while (slots[h])
                        h = (h + 1) & (size - 1);
                    slots[h] = ss->slots[i];
                }
            free(ss->slots);
        }
        ss->slots = slots;
        ss->mask = size - 1;
    }
    h = fnv1a(k) & ss->mask;
    while (ss->slots[h])
        h = (h + 1) & ss->mask;
    {
        size_t      kl = strlen(k) + 1;

        if (!(ss->slots[h] = malloc(kl)))
            return -1;
        memcpy(ss->slots[h], k, kl);
    }
    ss->n++;
    return 0;
}

static void
strset_free(StrSet *ss)
{
    uint32_t    i;

    if (ss->slots)
        for (i = 0; i <= ss->mask; i++)
            free(ss->slots[i]);
    free(ss->slots);
    memset(ss, 0, sizeof(*ss));
}

/* k in set, with k = key(s) */
static bool
in_set(const KaaWordSet *set, const cp_t *s, int n)
{
    char        buf[KEY_BYTES];

    key_utf8(s, n, buf);
    return set_has_utf8(set, buf);
}

/* the lexicon: built-in lists plus the user lexicon of the current call */
static bool
in_nom(const cp_t *s, int n)
{
    char        buf[KEY_BYTES];

    key_utf8(s, n, buf);
    return set_has_utf8(&kaa_nom, buf) || (cur_extra && strset_has(&cur_extra->nom, buf));
}

static bool
in_verb(const cp_t *s, int n)
{
    char        buf[KEY_BYTES];

    key_utf8(s, n, buf);
    return set_has_utf8(&kaa_verb, buf) || (cur_extra && strset_has(&cur_extra->verb, buf));
}

static int
cmp_pair(const void *a, const void *b)
{
    return strcmp(*(const char *const *) a, ((const KaaPair *) b)->key);
}

static const char *
map_get(const KaaPairMap *map, const cp_t *s, int n)
{
    char        buf[KEY_BYTES];
    const char *k = buf;
    const KaaPair *p;

    if (!map->n)
        return NULL;
    key_utf8(s, n, buf);
    p = bsearch(&k, map->items, map->n, sizeof(KaaPair), cmp_pair);
    return p ? p->value : NULL;
}

static int
cmp_ready(const void *a, const void *b)
{
    return strcmp(*(const char *const *) a, ((const KaaReady *) b)->form);
}

static const KaaReady *
ready_prefix_get(const cp_t *s, int n)
{
    char        buf[KEY_BYTES];
    const char *k = buf;

    key_utf8(s, n, buf);
    return bsearch(&k, kaa_ready_prefix, kaa_nready_prefix, sizeof(KaaReady), cmp_ready);
}

/* ======================================================================== tables.py */

static bool
is_back_vowel(cp_t c)
{
    return c == 'a' || c == CP_I_DOTLESS || c == 'o' || c == 'u';
}

static bool
is_front_vowel(cp_t c)
{
    return c == CP_A_ACUTE || c == 'e' || c == 'i' || c == CP_O_ACUTE || c == CP_U_ACUTE;
}

static bool
is_vowel(cp_t c)
{
    return is_back_vowel(c) || is_front_vowel(c);
}

static bool
has_vowel(const cp_t *s, int n)
{
    int         i;

    for (i = 0; i < n; i++)
        if (is_vowel(s[i]))
            return true;
    return false;
}

static int
vowel_count(const cp_t *s, int n)
{
    int         i,
                k = 0;

    for (i = 0; i < n; i++)
        k += is_vowel(s[i]);
    return k;
}

/* tables.is_back: is the last syllable back (no vowel counts as back) */
static bool
is_back(const cp_t *s, int n)
{
    int         i;

    for (i = n - 1; i >= 0; i--)
        if (is_vowel(s[i]))
            return is_back_vowel(s[i]);
    return true;
}

/* tables.sound_class: V, L, Z, N or T */
static char
sound_class(const cp_t *s, int n)
{
    cp_t        c = s[n - 1];

    if (is_vowel(c))
        return 'V';
    if (c == 'l' || c == 'r' || c == 'w' || c == 'y')
        return 'L';
    if (c == 'z' || c == 'j')
        return 'Z';
    if (c == 'm' || c == 'n' || c == CP_N_ACUTE)
        return 'N';
    return 'T';
}

static char
devoice(cp_t c)
{
    if (c == 'b')
        return 'p';
    if (c == 'g')
        return 'k';
    if (c == CP_G_ACUTE)
        return 'q';
    return 0;
}

/* tables.harmony_ok; cls < 0 is used for voice suffixes */
static bool
harmony_ok(const cp_t *prev, int pn, const KStr *m, int cls)
{
    cp_t        mv[16];
    int         nv = 0,
                i;
    bool        back,
                ok;

    for (i = 0; i < m->n; i++)
        if (is_vowel(m->c[i]))
            mv[nv++] = m->c[i];
    if (nv == 0 || cls == KAA_JAN || cls == KAA_NIKI)
        return true;
    {
        static KStr tugin,
                    jaq,
                    jag;
        static bool k_inited = false;

        if (!k_inited)
        {
            kstr_set(&tugin, "tu\307\265\304\261n");    /* tuǵın */
            kstr_set(&jaq, "jaq");
            kstr_set(&jag, "ja\307\265");              /* jaǵ */
            k_inited = true;
        }
        if (ends_with(m->c, m->n, &tugin) || ends_with(m->c, m->n, &jaq) || ends_with(m->c, m->n, &jag))
        {
            nv = is_vowel(m->c[0]) ? 1 : 0;
            if (nv == 0)
                return true;
        }
    }
    if (!has_vowel(prev, pn))
        return true;
    back = is_back(prev, pn);
    ok = true;
    for (i = 0; i < nv; i++)
        if (is_back_vowel(mv[i]) != back)
            ok = false;
    if (ok)
        return true;
    for (i = 0; i < nloan; i++)
        if (ends_with(prev, pn, &loan_cp[i]))
        {
            int         j;

            ok = true;
            for (j = 0; j < nv; j++)
                if (!is_front_vowel(mv[j]))
                    ok = false;
            if (ok)
                return true;
            break;
        }
    {
        cp_t        last = prev[pn - 1];

        if (last == 'x' || last == 'q' || last == CP_G_ACUTE)
        {
            ok = true;
            for (i = 0; i < nv; i++)
                if (!is_back_vowel(mv[i]))
                    ok = false;
            if (ok)
                return true;
        }
    }
    return false;
}

static char
mbp(char sc)                                /* _MBP */
{
    return (sc == 'V' || sc == 'L') ? 'm' : (sc == 'T' ? 'p' : 'b');
}

static char
ndt_gen(char sc)                            /* _NDT_GEN */
{
    return (sc == 'V' || sc == 'N') ? 'n' : (sc == 'T' ? 't' : 'd');
}

static char
ndt_acc(char sc)                            /* _NDT_ACC */
{
    return sc == 'V' ? 'n' : (sc == 'T' ? 't' : 'd');
}

static char
ndt_abl(char sc)                            /* _NDT_ABL */
{
    return sc == 'N' ? 'n' : (sc == 'T' ? 't' : 'd');
}

static char
man(char sc)                                /* _MAN */
{
    return (sc == 'Z') ? 'b' : (sc == 'T' ? 'p' : 'm');
}

static char
miz(char sc)                                /* _MIZ */
{
    return (sc == 'V' || sc == 'L') ? 'm' : (sc == 'T' ? 'p' : 'b');
}

static bool
is_qk(cp_t f)
{
    return f == 'q' || f == 'k';
}

static bool
is_gg(cp_t f)
{
    return f == CP_G_ACUTE || f == 'g';
}

/* tables.class_ok */
static bool
class_ok(const cp_t *prev, int pn, int mi, int cls, int prevcls)
{
    const KStr *m = &morph_cp[mi];
    char        sc = sound_class(prev, pn);
    cp_t        f = m->c[0];
    cp_t        last = prev[pn - 1];
    bool        zd = pn >= 2 && prev[pn - 2] == 'z' && prev[pn - 1] == 'd';
    char        scd = (last == CP_G_ACUTE || zd) ? 'Z' : sc;
    char        scg = zd ? 'Z' : sc;
    bool        after_px3 = (prevcls == KAA_PX || prevcls == KAA_NIKI) && (last == CP_I_DOTLESS || last == 'i');

    switch (cls)
    {
        case KAA_PL:
        case KAA_PLV:
        case KAA_JAN:
        case KAA_COND:
        case KAA_NMLZ:
            return true;
        case KAA_PX:
            return ((morph_flags[mi] & MF_PX_SHORT) != 0) == (sc == 'V');
        case KAA_GEN:
            return f == (cp_t) ndt_gen(scd);
        case KAA_ACC:
            return !after_px3 && f == (cp_t) ndt_acc(scd);
        case KAA_DAT:
            if (prevcls == KAA_PX && last != 'z')
                return false;
            if (after_px3)
                return false;
            return scg == 'T' ? is_qk(f) : is_gg(f);
        case KAA_LOC:
            return !after_px3 && f == (cp_t) (scd == 'T' ? 't' : 'd');
        case KAA_ABL:
            if (after_px3)
                return f == 'n';
            return f == (cp_t) ndt_abl(scd);
        case KAA_DATP:
            if (morph_flags[mi] & MF_NA_NE)
                return after_px3;
            return prevcls == KAA_PX && (last == 'm' || last == CP_N_ACUTE);
        case KAA_ACCP:
        case KAA_LOCP:
            return after_px3;
        case KAA_PRED:
        case KAA_P1:
            if (morph_flags[mi] & MF_MAN_HEAD)
                return f == (cp_t) man(sc);
            if (morph_flags[mi] & MF_MIZ_HEAD)
                return f == (cp_t) miz(sc);
            return true;
        case KAA_Q2:
            return f == (cp_t) mbp(sc);
        case KAA_GI:
            return scg == 'T' ? is_qk(f) : is_gg(f);
        case KAA_DAY:
            if (f == 'n')
                return after_px3;
            return !after_px3 && f == (cp_t) (scd == 'T' ? 't' : 'd');
        case KAA_SHA:
            return (f == 'n') == after_px3;
        case KAA_NIKI:
            return f == (cp_t) ndt_gen(scd);
        case KAA_DEG:
            if (f == 'r')
                return sc == 'V';
            if (f == CP_I_DOTLESS || f == 'i')
                return sc != 'V';
            return true;
        case KAA_NUM:
            if (morph_flags[mi] & MF_NSHI)
                return sc == 'V';
            if (morph_flags[mi] & MF_NUM_C)
                return sc != 'V';
            return true;
        case KAA_NEG:
        case KAA_INT:
        case KAA_PROG:
        case KAA_SUP:
            return f == (cp_t) mbp(sc);
        case KAA_PAST:
            return f == (cp_t) (scd == 'T' ? 't' : 'd');
        case KAA_PRES:
            return (m->n == 1 && f == 'y') == (sc == 'V');
        case KAA_PTCP:
            if (is_gg(f) || is_qk(f))
                return sc == 'T' ? is_qk(f) : is_gg(f);
            if (f == 'm' || f == 'b' || f == 'p')
                return f == (cp_t) mbp(sc);
            if (f == 'r' || f == 'y')
                return sc == 'V';
            return sc != 'V';
        case KAA_CVBP:
            return (m->n == 1 && f == 'p') == (sc == 'V');
        case KAA_CVB:
            if (f == 'm' || f == 'b' || f == 'p')
                return f == (cp_t) mbp(sc);
            return sc == 'T' ? is_qk(f) : is_gg(f);
        case KAA_OPT:
            return sc == 'T' ? is_qk(f) : is_gg(f);
        case KAA_IMP:
            if (morph_flags[mi] & MF_IMP_V)
                return sc == 'V';
            if (morph_flags[mi] & MF_SIN)
                return true;
            return sc != 'V';
        case KAA_NMSH:
            return sc != 'V';
        case KAA_VN:
            return (m->n == 1 && f == 'w') == (sc == 'V');
        case KAA_P2:
            if (m->n == 1 && (f == 'q' || f == 'k'))
                return (f == 'q') == is_back(prev, pn);
            return true;
        case KAA_P3:
            return (prevcls == KAA_CVBP) == (f == 't');
        default:
            return true;
    }
}

static void
paths_push(Ctx *ctx, PathList *pl, const Path *p)
{
    if (pl->n == pl->cap)
    {
        int         cap = pl->cap ? pl->cap * 2 : 16;
        Path       *items = realloc(pl->items, cap * sizeof(Path));

        if (!items)
        {
            ctx->oom = true;
            return;
        }
        pl->items = items;
        pl->cap = cap;
    }
    pl->items[pl->n++] = *p;
}

/* The position after morph mi at pos, or -1 when it does not fit there (tables.parse checks). */
static int
morph_fits(const KaaWord *w, int pos, int mi, int cls, int state)
{
    const KStr *m = &morph_cp[mi];
    int         after = pos + m->n;
    bool        vowel_next;
    int         q;

    if (after > w->n)
        return -1;
    for (q = 1; q < m->n && w->c[pos + q] == m->c[q]; q++)
        ;
    if (q < m->n)
        return -1;
    vowel_next = after < w->n && is_vowel(w->c[after]);
    if (morph_voiced[mi] && !vowel_next)
        return -1;
    if (morph_unvoiced[mi] && vowel_next)
        return -1;
    if (!harmony_ok(w->c, pos, m, cls))
        return -1;
    if (!class_ok(w->c, pos, mi, cls, state))
        return -1;
    return after;
}

static const MorphGroup *
find_group(int cls, cp_t first)
{
    int         g;

    for (g = 0; g < class_ngroups[cls]; g++)
        if (class_groups[cls][g].first == first)
            return &class_groups[cls][g];
    return NULL;
}

/* tables.parse: split w[pos:] into suffixes, starting from state; prev is always w[:pos]. */
static void
parse(Ctx *ctx, const KaaWord *w, int pos, int state, bool bare, Path *cur, PathList *out)
{
    const KaaClass *order = kaa_next_order[state];
    int         k;

    if (pos == w->n)
    {
        if (kaa_next[state] & KAA_BIT(KAA_END))
            paths_push(ctx, out, cur);
        return;
    }
    if (cur->n >= MAX_DEPTH)
        return;
    for (k = 0; order[k] != KAA_NSTATES; k++)
    {
        int         cls = order[k];
        const MorphGroup *grp;
        int         gi;

        if (cls == KAA_END)
            continue;
        if (state == KAA_START && (kaa_bare_first & KAA_BIT(cls)) && !bare)
            continue;
        if (!(grp = find_group(cls, w->c[pos])))
            continue;
        for (gi = grp->start; gi < grp->start + grp->count; gi++)
        {
            int         mi = group_morphs[gi];
            int         after = morph_fits(w, pos, mi, cls, state);

            if (after < 0)
                continue;
            cur->m[cur->n++] = (short) mi;
            parse(ctx, w, after, cls, false, cur, out);
            cur->n--;
            if (ctx->oom)
                return;
        }
    }
}

static void
parse_all(Ctx *ctx, const KaaWord *w, int pos, int state, bool bare, PathList *out)
{
    Path        cur;

    cur.n = 0;
    out->n = 0;
    parse(ctx, w, pos, state, bare, &cur, out);
}

#define PCLS(p, i) ((int) kaa_morphs[(p)->m[i]].cls)

/* ======================================================================== stemmer.py */

static bool is_verb(const cp_t *s, int n);

/* Stemmer._is_nom: 2 = in the lexicon; 1 = derived; 0 = no */
static int  is_nom_d(const cp_t *s, int n, int depth);

static bool
is_derived(const cp_t *s, int n, int depth)
{
    size_t      i;

#define BASE_OK(bn) ((bn) >= 2 && has_vowel(s, (bn)) && !in_set(&kaa_not_base, s, (bn)) && is_nom_d(s, (bn), depth + 1))
    for (i = 0; i < kaa_nderiv_harmonic; i++)
    {
        int         j;

        for (j = 0; j < 2; j++)
        {
            const KStr *suf = &deriv_harm[i][j];
            int         bn;

            if (!ends_with(s, n, suf))
                continue;
            bn = n - suf->n;
            if (bn > 0 && is_back(s, bn) == (j == 0) && BASE_OK(bn))
                return true;
        }
    }
    for (i = 0; i < kaa_deriv_fixed.n; i++)
    {
        const KStr *suf = &deriv_fixed[i];

        if (ends_with(s, n, suf) && BASE_OK(n - suf->n))
            return true;
    }
#undef BASE_OK
    for (i = 0; i < kaa_nderiv_verbal; i++)
    {
        int         j;

        for (j = 0; j < 2; j++)
        {
            const KStr *suf = &deriv_verbal[i][j];
            int         bn;

            if (!ends_with(s, n, suf))
                continue;
            bn = n - suf->n;
            if (bn < 2 || !has_vowel(s, bn) || (is_vowel(s[bn - 1]) ? 1 : 0) != kaa_deriv_verbal[i].after_vowel ||
                is_back(s, bn) != (j == 0))
                continue;
            if (is_verb(s, bn))
                return true;
            if (is_vowel(suf->c[0]) && devoice(s[bn - 1]) && is_vowel(s[bn - 2]))
            {
                cp_t        tmp[WORD_CAP];

                memcpy(tmp, s, bn * sizeof(cp_t));
                tmp[bn - 1] = (cp_t) devoice(s[bn - 1]);
                if (is_verb(tmp, bn))
                    return true;            /* shıǵıs = shıq + ıs */
            }
        }
    }
    return false;
}

static int
is_nom_d(const cp_t *s, int n, int depth)
{
    if (in_nom(s, n) || in_set(&kaa_protected, s, n))
        return 2;
    if (depth >= 2 || n < 4)
        return 0;
    return is_derived(s, n, depth) ? 1 : 0;
}

static int
is_nom(const cp_t *s, int n)
{
    return is_nom_d(s, n, 0);
}

/* Stemmer._voice_fit */
static bool
voice_fit(const cp_t *rem, int rn, const KStr *suf, int vclass)
{
    char        sc;

    if (!harmony_ok(rem, rn, suf, -1))
        return false;
    sc = sound_class(rem, rn);
    if (suf->n == 1 && (suf->c[0] == 'l' || suf->c[0] == 'n' || suf->c[0] == 's'))
        return sc == 'V' && vowel_count(rem, rn) >= 2;
    if (suf->n == 1 && suf->c[0] == 't')
        return (sc == 'V' || rem[rn - 1] == 'r' || rem[rn - 1] == 'y') && vowel_count(rem, rn) >= 2;
    if (vclass == 1)
        return sc != 'T';
    if (vclass == 2)
        return sc == 'T';
    return sc != 'V';
}

/* Stemmer._voice_base: strip one voice suffix. Returns true and sets base, or false. */
static bool
voice_base(const cp_t *s, int n, KaaWord *base)
{
    const char *explicit_base = map_get(&kaa_voice_explicit, s, n);
    KaaWord     opts[32];
    int         nopt = 0,
                i;
    size_t      v;

    if (explicit_base)
    {
        word_from_utf8(base, explicit_base);
        return true;
    }
    for (v = 0; v < kaa_nvoice_suffixes && v < 32; v++)
    {
        const KStr *suf = &voice_cp[v];
        int         rn = n - suf->n;

        if (!ends_with(s, n, suf) || rn < 2)
            continue;
        if (!has_vowel(s, rn) || !voice_fit(s, rn, suf, voice_class[v]))
            continue;
        if (is_verb(s, rn))
        {
            word_set(&opts[nopt++], s, rn);
            continue;
        }
        if (is_vowel(suf->c[0]) && devoice(s[rn - 1]) && is_vowel(s[rn - 2]))
        {
            KaaWord     d;

            word_set(&d, s, rn);
            d.c[rn - 1] = (cp_t) devoice(s[rn - 1]);
            if (is_verb(d.c, d.n))
                opts[nopt++] = d;           /* shıǵ-ar → shıq */
        }
    }
    if (nopt == 0)
        return false;
    /* the preferred stem in pairs such as qur / qurı (prefer.tsv) */
    {
        bool        loser[32] = {false};
        bool        any_left = false;
        int         pass;

        for (pass = 0; pass < 2; pass++)
        {
            const KaaStemPair *pairs = pass ? kaa_ambiguous : kaa_prefer;
            size_t      np = pass ? kaa_nambiguous : kaa_nprefer;
            size_t      p;

            for (p = 0; p < np; p++)
            {
                bool        win_present = false;

                if (pairs[p].first_kind != 'v' || pairs[p].second_kind != 'v')
                    continue;
                for (i = 0; i < nopt; i++)
                {
                    char        buf[KEY_BYTES];

                    key_utf8(opts[i].c, opts[i].n, buf);
                    if (strcmp(buf, pairs[p].first) == 0)
                        win_present = true;
                }
                if (!win_present)
                    continue;
                for (i = 0; i < nopt; i++)
                {
                    char        buf[KEY_BYTES];

                    key_utf8(opts[i].c, opts[i].n, buf);
                    if (strcmp(buf, pairs[p].second) == 0)
                        loser[i] = true;
                }
            }
        }
        for (i = 0; i < nopt; i++)
            any_left |= !loser[i];
        for (i = 0; i < nopt; i++)
            if (!any_left || !loser[i])
            {
                *base = opts[i];
                return true;
            }
    }
    return false;
}

/* Stemmer._is_verb */
static bool
is_verb(const cp_t *s, int n)
{
    KaaWord     base;

    return in_verb(s, n) || voice_base(s, n, &base);
}

/* Stemmer._voice_reduce */
static void
voice_reduce(KaaWord *stem)
{
    for (;;)
    {
        KaaWord     base;

        if (map_get(&kaa_overrides, stem->c, stem->n) || in_set(&kaa_voice_stop, stem->c, stem->n) ||
            in_set(&kaa_protected, stem->c, stem->n) || in_set(&kaa_bare, stem->c, stem->n))
            break;
        if (!voice_base(stem->c, stem->n, &base))
            break;
        *stem = base;
    }
}

/* Stemmer._valid */
static int
valid(const KaaWord *stem, char kind, const Path *path)
{
    int         cls0 = PCLS(path, 0);

    if (kind == 'V')
        return is_verb(stem->c, stem->n) ? 2 : 0;
    if (in_set(&kaa_not_base, stem->c, stem->n) || in_set(&kaa_function, stem->c, stem->n))
        return 0;
    if (cls0 == KAA_NUM)
        return in_set(&kaa_numerals, stem->c, stem->n) ? 2 : 0;
    if (cls0 == KAA_JAN)
        return in_set(&kaa_kinship, stem->c, stem->n) ? 2 : 0;
    if (cls0 == KAA_GI)
        return in_set(&kaa_gi_bases, stem->c, stem->n) ? 2 : 0;
    return is_nom(stem->c, stem->n);
}

static void
cands_push(Ctx *ctx, CandList *cl, const Cand *c)
{
    if (cl->n == cl->cap)
    {
        int         cap = cl->cap ? cl->cap * 2 : 16;
        Cand       *items = realloc(cl->items, cap * sizeof(Cand));

        if (!items)
        {
            ctx->oom = true;
            return;
        }
        cl->items = items;
        cl->cap = cap;
    }
    cl->items[cl->n++] = *c;
}

/* Stemmer._restorations + the loop body of _candidates: add the first restoration that is valid */
static void
add_restored(Ctx *ctx, CandList *out, const KaaWord *w, int cut, char kind, const Path *path)
{
    KaaWord     surf;
    KaaWord     opt[8];
    bool        weak[8];
    int         nopt = 0,
                i;
    int         cls0 = PCLS(path, 0);
    const KStr *m0 = &morph_cp[path->m[0]];
    bool        vowel0 = is_vowel(m0->c[0]);
    int         n = cut;
    cp_t        last;

    word_set(&surf, w->c, cut);
    last = surf.c[n - 1];
    opt[nopt] = surf;
    weak[nopt++] = false;
    if (vowel0 && devoice(last) && is_vowel(surf.c[n - 2]))
    {
        opt[nopt] = surf;
        opt[nopt].c[n - 1] = (cp_t) devoice(last);
        weak[nopt++] = false;
    }
    if (cls0 == KAA_CVBP && vowel0 && last == 'w')
    {
        opt[nopt] = surf;
        opt[nopt].c[n - 1] = 'p';
        weak[nopt++] = false;
    }
    if (cls0 == KAA_PX && vowel0 && n >= 3 && !is_vowel(last) && !is_vowel(surf.c[n - 2]) && n + 1 <= WORD_CAP)
    {
        KaaWord     full = surf;

        full.c[n - 1] = is_back(surf.c, n) ? CP_I_DOTLESS : 'i';
        full.c[n] = last;
        full.n = n + 1;
        opt[nopt] = full;
        weak[nopt++] = !in_set(&kaa_elision, full.c, full.n);
        if (seg_eq(surf.c, n, "dost"))
        {
            word_from_utf8(&opt[nopt], "dos");
            weak[nopt++] = false;
        }
    }
    if (cls0 == KAA_NUM && (kstr_eq(m0, "aw") || kstr_eq(m0, "ew")) && n + 1 <= WORD_CAP)
    {
        opt[nopt] = surf;
        opt[nopt].c[n] = is_back(surf.c, n) ? CP_I_DOTLESS : 'i';
        opt[nopt].n = n + 1;
        weak[nopt++] = false;
    }
    if (!(kaa_verb_first & KAA_BIT(cls0)) && !is_vowel(last) && n + 1 <= WORD_CAP)
    {
        opt[nopt] = surf;
        opt[nopt].c[n] = last;
        opt[nopt].n = n + 1;
        weak[nopt++] = true;
    }
    for (i = 0; i < nopt; i++)
    {
        int         level = valid(&opt[i], kind, path);

        if (level)
        {
            Cand        c;

            c.cut = cut;
            c.stem = opt[i];
            c.kind = kind;
            c.path = *path;
            c.weak = weak[i] || level == 1;
            cands_push(ctx, out, &c);
            break;
        }
    }
}

/* Stemmer._candidates */
static void
candidates(Ctx *ctx, const KaaWord *w, CandList *out)
{
    PathList    pl = {NULL, 0, 0};
    int         cut;

    out->n = 0;
    for (cut = w->n - 1; cut > 1 && !ctx->oom; cut--)
    {
        const KaaReady *ready;
        int         i;

        if (!has_vowel(w->c, cut))
            continue;
        ready = ready_prefix_get(w->c, cut);
        if (ready)
        {
            parse_all(ctx, w, cut, ready->state, true, &pl);
            for (i = 0; i < pl.n; i++)
            {
                Cand        c;

                c.cut = cut;
                word_from_utf8(&c.stem, ready->stem);
                c.kind = 'R';
                c.path = pl.items[i];
                c.weak = false;
                cands_push(ctx, out, &c);
            }
        }
        parse_all(ctx, w, cut, KAA_START, in_set(&kaa_bare, w->c, cut), &pl);
        for (i = 0; i < pl.n; i++)
        {
            const Path *p = &pl.items[i];
            int         cls0 = PCLS(p, 0);

            if (kaa_bare_first & KAA_BIT(cls0))
            {
                Cand        c;

                c.cut = cut;
                word_set(&c.stem, w->c, cut);
                c.kind = 'B';
                c.path = *p;
                c.weak = false;
                cands_push(ctx, out, &c);
                continue;
            }
            add_restored(ctx, out, w, cut, (kaa_verb_first & KAA_BIT(cls0)) ? 'V' : 'N', p);
        }
    }
    free(pl.items);
}

static char
kind_letter(char kind)
{
    return kind == 'V' ? 'v' : 'n';
}

static bool
stem_is(const Cand *c, const char *keyed, char kind)
{
    char        buf[KEY_BYTES];

    if (kind_letter(c->kind) != kind)
        return false;
    key_utf8(c->stem.c, c->stem.n, buf);
    return strcmp(buf, keyed) == 0;
}

/* Stemmer._exceptions: filters cands in place */
static void
exceptions(Ctx *ctx, const KaaWord *w, CandList *cl)
{
    int         n = cl->n,
                i,
                j;
    bool       *drop,
               *keep,
               *nominal;
    bool        any_strong = false;
    Cand       *c = cl->items;

    for (i = 0; i < n; i++)
        any_strong |= !c[i].weak;
    if (any_strong)
    {
        int         k = 0;

        for (i = 0; i < n; i++)
            if (!c[i].weak)
                c[k++] = c[i];
        cl->n = n = k;
    }
    {
        bool        two = false;

        for (i = 1; i < n; i++)
            if (c[i].cut != c[0].cut)
                two = true;
        if (!two)
            return;
    }
    drop = calloc(3 * (size_t) n, sizeof(bool));
    if (!drop)
    {
        ctx->oom = true;
        return;
    }
    keep = drop + n;
    nominal = keep + n;

    /* (0) pair list */
    {
        size_t      p;

        for (p = 0; p < kaa_nprefer; p++)
        {
            bool        win = false,
                        lose = false;

            for (i = 0; i < n; i++)
            {
                win |= stem_is(&c[i], kaa_prefer[p].first, kaa_prefer[p].first_kind);
                lose |= stem_is(&c[i], kaa_prefer[p].second, kaa_prefer[p].second_kind);
            }
            if (!(win && lose))
                continue;
            for (i = 0; i < n; i++)
            {
                if (stem_is(&c[i], kaa_prefer[p].second, kaa_prefer[p].second_kind) &&
                    (c[i].path.n || c[i].kind == 'V'))
                    drop[i] = true;
                if (stem_is(&c[i], kaa_prefer[p].first, kaa_prefer[p].first_kind))
                    keep[i] = true;
            }
        }
    }
    for (i = 0; i < n; i++)
        nominal[i] = (c[i].kind == 'N' || c[i].kind == 'R') && c[i].path.n && !drop[i];

    /* (1)–(4) */
    for (i = 0; i < n; i++)
    {
        const Cand *cv = &c[i];
        int         cls0;
        const KStr *m0;

        if (cv->kind != 'V' || keep[i])
            continue;
        if (!cv->path.n)
        {
            for (j = 0; j < n; j++)
                if (nominal[j])
                {
                    drop[i] = true;
                    break;
                }
            continue;
        }
        cls0 = PCLS(&cv->path, 0);
        m0 = &morph_cp[cv->path.m[0]];
        for (j = 0; j < n; j++)
        {
            const Cand *cn = &c[j];
            int         ncls;
            const KStr *nm;
            const cp_t *extra;
            int         en;

            if (!nominal[j])
                continue;
            ncls = PCLS(&cn->path, 0);
            nm = &morph_cp[cn->path.m[0]];
            extra = w->c + cn->cut;
            en = cv->cut > cn->cut ? cv->cut - cn->cut : 0;
            /* (2) */
            if ((seg_eq(extra, en, "\304\261n") || seg_eq(extra, en, "in") || seg_eq(extra, en, "s\304\261n") ||
                 seg_eq(extra, en, "sin")) && ncls == KAA_PX &&
                nm->n == en - 1 && memcmp(nm->c, extra, (en - 1) * sizeof(cp_t)) == 0 && cn->path.n > 1)
                drop[i] = true;
            /* (3) */
            if (seg_eq(extra, en, "n") && ncls == KAA_GEN && cls0 == KAA_IMP)
                drop[i] = true;
            /* (4) */
            if (cls0 == KAA_PTCP && (kstr_eq(m0, "r") || kstr_eq(m0, "ar") || kstr_eq(m0, "er")) &&
                ncls == KAA_PL && cv->cut + m0->n == cn->cut + 3)
                drop[i] = true;
        }
    }
    /* (5)–(7) */
    for (i = 0; i < n; i++)
    {
        const Cand *cv = &c[i];
        int         nc;
        int         c0,
                    c1;

        if (cv->kind != 'V' || !cv->path.n || drop[i])
            continue;
        nc = cv->path.n;
        c0 = PCLS(&cv->path, 0);
        c1 = nc > 1 ? PCLS(&cv->path, 1) : -1;
        for (j = 0; j < n; j++)
        {
            const Cand *cn = &c[j];
            int         ncls;

            if (!nominal[j] || keep[j] || cn->cut <= cv->cut || cn->path.n != 1)
                continue;
            ncls = PCLS(&cn->path, 0);
            if (c0 == KAA_PRES && (c1 == KAA_P1 || c1 == KAA_Q2) && nc <= 3 &&
                (ncls == KAA_PRED || ncls == KAA_PX || ncls == KAA_Q2))
                drop[j] = true;
            if (nc == 2 && c0 == KAA_PRES && c1 == KAA_P3 && kstr_eq(&morph_cp[cv->path.m[0]], "y") &&
                ncls == KAA_ACC)
                drop[j] = true;
            if (c0 == KAA_PROG && ncls == KAA_LOC)
                drop[j] = true;
        }
    }
    /* (8), (9) */
    for (j = 0; j < n; j++)
    {
        const Cand *cn = &c[j];
        const KStr *nm;

        if (!nominal[j] || cn->path.n != 1 || PCLS(&cn->path, 0) != KAA_PX || keep[j])
            continue;
        nm = &morph_cp[cn->path.m[0]];
        if (nm->c[0] == CP_N_ACUTE)
        {
            for (i = 0; i < n; i++)
                if (c[i].cut < cn->cut && !drop[i])
                {
                    drop[j] = true;
                    break;
                }
        }
        if (kstr_eq(nm, "\304\261\305\204") || kstr_eq(nm, "i\305\204"))
        {
            for (i = 0; i < n; i++)
                if (nominal[i] && c[i].cut == cn->cut - 1 && c[i].path.n == 1 && PCLS(&c[i].path, 0) == KAA_GEN)
                {
                    drop[j] = true;
                    break;
                }
        }
    }
    {
        int         k = 0;

        for (i = 0; i < n; i++)
            if (!drop[i])
                c[k++] = c[i];
        cl->n = k;
    }
    free(drop);
}

static int
kind_order(char kind)
{
    switch (kind)
    {
        case 'R':
            return 0;
        case 'N':
            return 1;
        case 'B':
            return 2;
        default:
            return 3;
    }
}

/* Stemmer._select: index of the winner, or -1 */
static int
select_best(const CandList *cl)
{
    int         i,
                best = -1;

    for (i = 0; i < cl->n; i++)
    {
        const Cand *c = &cl->items[i];

        if (best < 0 || c->cut > cl->items[best].cut ||
            (c->cut == cl->items[best].cut && kind_order(c->kind) < kind_order(cl->items[best].kind)))
            best = i;
    }
    return best;
}

/* Stemmer._light: pure(path) */
static bool
pure(const Path *p)
{
    int         i = 0,
                j;

    while (i < p->n && PCLS(p, i) == KAA_NEG)
        i++;
    if (i < p->n && (PCLS(p, i) == KAA_VN || PCLS(p, i) == KAA_NMSH ||
                     (PCLS(p, i) == KAA_PTCP && morph_gan[p->m[i]])))
        i++;
    else if (i)
        return false;
    for (j = i; j < p->n; j++)
    {
        int         c = PCLS(p, j);

        if (kaa_light & KAA_BIT(c))
            continue;
        if ((c == KAA_LOC || c == KAA_LOCP) && j + 1 < p->n && PCLS(p, j + 1) == KAA_GI)
            continue;
        return false;
    }
    return true;
}

/* Stemmer._light: the winning light analysis of stem, copied into *out; false if none */
static bool
light(Ctx *ctx, const KaaWord *stem, Cand *out)
{
    CandList    cl = {NULL, 0, 0};
    int         i,
                k = 0,
                best;

    candidates(ctx, stem, &cl);
    for (i = 0; i < cl.n; i++)
        if (pure(&cl.items[i].path))
            cl.items[k++] = cl.items[i];
    cl.n = k;
    exceptions(ctx, stem, &cl);
    best = select_best(&cl);
    if (best >= 0)
        *out = cl.items[best];
    free(cl.items);
    return best >= 0;
}

/* ======================================================================== results */

typedef struct
{
    KaaWord     stem;
    KaaWord     segs[MAX_DEPTH * 2 + 2];
    int         nsegs;
    int         classes[MAX_DEPTH * 2 + 2];
    int         nclasses;
    KaaSource   source;
} Result;

static void
result_simple(Result *r, const KaaWord *w, KaaSource source)
{
    r->stem = *w;
    r->segs[0] = *w;
    r->nsegs = 1;
    r->nclasses = 0;
    r->source = source;
}

/* Stemmer._safe */
static void
safe_mode(Ctx *ctx, const KaaWord *w, Result *r)
{
    PathList    pl = {NULL, 0, 0};
    int         cut,
                i,
                j;

    for (cut = 3; cut < w->n - 2 && !ctx->oom; cut++)
    {
        if (!has_vowel(w->c, cut))
            continue;
        parse_all(ctx, w, cut, KAA_START, false, &pl);
        for (i = 0; i < pl.n; i++)
        {
            const Path *p = &pl.items[i];
            bool        ok = true;

            for (j = 0; j < p->n; j++)
                if (!(kaa_safe & KAA_BIT(PCLS(p, j))) || morph_cp[p->m[j]].n < 3)
                    ok = false;
            if (!ok)
                continue;
            if (kstr_eq(&morph_cp[p->m[0]], "tan") && w->c[cut - 1] == 's')
                continue;                   /* Qazaqstan, Tatarstan: -stan */
            word_set(&r->stem, w->c, cut);
            r->segs[0] = r->stem;
            for (j = 0; j < p->n; j++)
            {
                const KStr *m = &morph_cp[p->m[j]];

                word_set(&r->segs[j + 1], m->c, m->n);
                r->classes[j] = PCLS(p, j);
            }
            r->nsegs = p->n + 1;
            r->nclasses = p->n;
            r->source = KAA_SOURCE_SAFE;
            free(pl.items);
            return;
        }
    }
    free(pl.items);
    result_simple(r, w, KAA_SOURCE_UNCHANGED);
}

static bool
is_alpha(cp_t c)
{
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
        return true;
    if (c == 0xAA || c == 0xB5 || c == 0xBA)
        return true;
    if (c >= 0xC0 && c <= 0x24F)
        return c != 0xD7 && c != 0xF7;
    if (c >= 0x250 && c <= 0x2C1)
        return true;
    if (c >= 0x370 && c <= 0x3FF)
        return c != 0x375 && c != 0x37E && c != 0x384 && c != 0x385 && c != 0x387 && c != 0x3F6;
    if (c >= 0x400 && c <= 0x52F)
        return !(c >= 0x482 && c <= 0x489);
    if (c >= 0x1E00 && c <= 0x1EFF)
        return true;
    return false;
}

/* Stemmer._analyze_uncached */
static void
analyze_simple(Ctx *ctx, const KaaWord *w, Result *r)
{
    CandList    cl = {NULL, 0, 0};
    const char *ready;
    int         level,
                best,
                i;
    char        kind;
    KaaWord     stem;

    {
        bool        alpha = w->n > 0;

        for (i = 0; i < w->n; i++)
            alpha &= is_alpha(w->c[i]);
        if (w->n < 2 || !alpha || !has_vowel(w->c, w->n))
        {
            result_simple(r, w, KAA_SOURCE_UNCHANGED);
            return;
        }
    }
    /* step 2: ready answers */
    if (in_set(&kaa_protected, w->c, w->n) || in_set(&kaa_function, w->c, w->n))
    {
        result_simple(r, w, KAA_SOURCE_LIST);
        return;
    }
    ready = map_get(&kaa_ready, w->c, w->n);
    if (ready)
    {
        KaaWord     s;
        const char *ov;

        word_from_utf8(&s, ready);
        ov = map_get(&kaa_overrides, s.c, s.n);
        if (ov)
            word_from_utf8(&s, ov);
        result_simple(r, w, KAA_SOURCE_LIST);
        r->stem = s;
        return;
    }
    /* steps 3–5 */
    candidates(ctx, w, &cl);
    level = is_nom(w->c, w->n);
    if (level || is_verb(w->c, w->n))
    {
        Cand        c;

        c.cut = w->n;
        c.stem = *w;
        c.kind = level ? 'N' : 'V';
        c.path.n = 0;
        c.weak = level ? level == 1 : !in_verb(w->c, w->n);
        cands_push(ctx, &cl, &c);
    }
    /* ambiguous pair ("=" in prefer.tsv): if both stems fit, the word is left unchanged */
    if (kaa_nambiguous)
    {
        bool        any_strong = false;
        size_t      p;

        for (i = 0; i < cl.n; i++)
            any_strong |= !cl.items[i].weak;
        for (p = 0; p < kaa_nambiguous; p++)
        {
            bool        a = false,
                        b = false;

            for (i = 0; i < cl.n; i++)
            {
                if (any_strong && cl.items[i].weak)
                    continue;
                a |= stem_is(&cl.items[i], kaa_ambiguous[p].first, kaa_ambiguous[p].first_kind);
                b |= stem_is(&cl.items[i], kaa_ambiguous[p].second, kaa_ambiguous[p].second_kind);
            }
            if (a && b)
            {
                free(cl.items);
                result_simple(r, w, KAA_SOURCE_LIST);
                return;
            }
        }
    }
    exceptions(ctx, w, &cl);
    best = select_best(&cl);
    if (best < 0)
    {
        free(cl.items);
        safe_mode(ctx, w, r);
        return;
    }
    {
        const Cand *b = &cl.items[best];

        stem = b->stem;
        kind = b->kind;
        word_set(&r->segs[0], w->c, b->cut);
        for (i = 0; i < b->path.n; i++)
        {
            const KStr *m = &morph_cp[b->path.m[i]];

            word_set(&r->segs[i + 1], m->c, m->n);
            r->classes[i] = PCLS(&b->path, i);
        }
        r->nsegs = b->path.n + 1;
        r->nclasses = b->path.n;
    }
    free(cl.items);
    while (kind == 'N' && !in_set(&kaa_protected, stem.c, stem.n) && !ctx->oom)
    {
        Cand        nxt;
        int         k;

        if (!light(ctx, &stem, &nxt))
            break;
        if (r->nsegs + nxt.path.n > MAX_DEPTH * 2 + 1)
            break;
        /* segs[:1] = [stem[:cut]] + morphs */
        memmove(&r->segs[nxt.path.n + 1], &r->segs[1], (r->nsegs - 1) * sizeof(KaaWord));
        word_set(&r->segs[0], stem.c, nxt.cut);
        for (k = 0; k < nxt.path.n; k++)
        {
            const KStr *m = &morph_cp[nxt.path.m[k]];

            word_set(&r->segs[k + 1], m->c, m->n);
        }
        r->nsegs += nxt.path.n;
        /* classes[:0] = nxt classes */
        memmove(&r->classes[nxt.path.n], &r->classes[0], r->nclasses * sizeof(int));
        for (k = 0; k < nxt.path.n; k++)
            r->classes[k] = PCLS(&nxt.path, k);
        r->nclasses += nxt.path.n;
        stem = nxt.stem;
        kind = nxt.kind;
    }
    /* step 6: voice */
    if (kind == 'V')
        voice_reduce(&stem);
    {
        const char *ov = map_get(&kaa_overrides, stem.c, stem.n);

        if (ov)
            word_from_utf8(&stem, ov);
    }
    r->stem = stem;
    r->source = KAA_SOURCE_LEXICON;
}

static bool
source_found(KaaSource s)
{
    return s == KAA_SOURCE_LIST || s == KAA_SOURCE_LEXICON;
}

/* Stemmer._analyze_token */
static void
analyze_token(Ctx *ctx, const KaaWord *w, Result *r)
{
    analyze_simple(ctx, w, r);
    /* 2009–2016 orthography: ye- → e-, wo- → o-, wó- → ó- (only if the result is in the lexicon) */
    if (!source_found(r->source) && w->n > 2 &&
        ((w->c[0] == 'y' && w->c[1] == 'e') || (w->c[0] == 'w' && (w->c[1] == 'o' || w->c[1] == CP_O_ACUTE))))
    {
        KaaWord     tail;
        Result     *b = malloc(sizeof(Result));

        if (!b)
        {
            ctx->oom = true;
            return;
        }
        word_set(&tail, w->c + 1, w->n - 1);
        analyze_simple(ctx, &tail, b);
        if (source_found(b->source))
            *r = *b;
        free(b);
    }
}

/* Stemmer._analyze_hyphen */
static void
analyze_hyphen(Ctx *ctx, const KaaWord *w, Result *r)
{
    int         starts[WORD_CAP],
                lens[WORD_CAP];
    int         np = 0,
                i,
                s = 0;

    for (i = 0; i <= w->n; i++)
        if (i == w->n || w->c[i] == '-')
        {
            starts[np] = s;
            lens[np++] = i - s;
            s = i + 1;
        }
    /* a particle written with a hyphen is dropped: sen-aq → sen, kelseń-á → kel */
    if (np >= 2)
    {
        char        buf[KEY_BYTES];
        bool        prev_alpha = lens[np - 2] > 0;

        raw_utf8(w->c + starts[np - 1], lens[np - 1], buf);
        for (i = 0; i < lens[np - 2]; i++)
            prev_alpha &= is_alpha(w->c[starts[np - 2] + i]);
        if (set_has_utf8(&kaa_particles, buf) && prev_alpha)
        {
            KaaWord     head;
            int         hn = starts[np - 1] - 1;
            bool        head_hyphen = false;

            word_set(&head, w->c, hn);
            for (i = 0; i < hn; i++)
                head_hyphen |= head.c[i] == '-';
            if (head_hyphen)
                analyze_hyphen(ctx, &head, r);
            else
                analyze_token(ctx, &head, r);
            if (!source_found(r->source) && hn > 0 && devoice(head.c[hn - 1]) && !head_hyphen)
            {
                Result     *b = malloc(sizeof(Result));

                if (!b)
                {
                    ctx->oom = true;
                    return;
                }
                head.c[hn - 1] = (cp_t) devoice(head.c[hn - 1]);    /* joǵ-á → joq */
                analyze_token(ctx, &head, b);
                if (source_found(b->source))
                    *r = *b;
                free(b);
            }
            return;
        }
    }
    if (in_nom(w->c, w->n) || in_set(&kaa_protected, w->c, w->n) || in_set(&kaa_function, w->c, w->n))
    {
        result_simple(r, w, KAA_SOURCE_LEXICON);   /* ata-ana */
        return;
    }
    /* paired words and «5-mektepke»: each part is stemmed separately, the hyphen stays */
    {
        KaaWord     stem;
        KaaSource   source = KAA_SOURCE_UNCHANGED;
        Result     *a = malloc(sizeof(Result));

        if (!a)
        {
            ctx->oom = true;
            return;
        }
        stem.n = 0;
        for (i = 0; i < np; i++)
        {
            KaaWord     part;
            bool        alpha = lens[i] > 0;
            int         k;

            word_set(&part, w->c + starts[i], lens[i]);
            for (k = 0; k < part.n; k++)
                alpha &= is_alpha(part.c[k]);
            if (i > 0 && stem.n < WORD_CAP)
                stem.c[stem.n++] = '-';
            if (alpha)
            {
                analyze_token(ctx, &part, a);
                for (k = 0; k < a->stem.n && stem.n < WORD_CAP; k++)
                    stem.c[stem.n++] = a->stem.c[k];
                if (source_found(a->source) || (a->source == KAA_SOURCE_SAFE && source == KAA_SOURCE_UNCHANGED))
                    source = a->source;
            }
            else
                for (k = 0; k < part.n && stem.n < WORD_CAP; k++)
                    stem.c[stem.n++] = part.c[k];
            r->segs[i < MAX_DEPTH * 2 + 2 ? i : MAX_DEPTH * 2 + 1] = part;
        }
        free(a);
        r->stem = stem;
        r->nsegs = np < MAX_DEPTH * 2 + 2 ? np : MAX_DEPTH * 2 + 2;
        r->nclasses = 0;
        r->source = source;
    }
}

/* ======================================================================== step 1: normalize.py */

static bool
is_cyr_letter(cp_t c)                       /* _CYR_RE */
{
    return (c >= 0x0410 && c <= 0x044F) || c == 0x0451 || c == 0x0401 || c == 0x04D9 || c == 0x04D8 ||
        c == 0x0493 || c == 0x0492 || c == 0x049B || c == 0x049A || c == 0x04A3 || c == 0x04A2 ||
        c == 0x04E9 || c == 0x04E8 || c == 0x04AF || c == 0x04AE || c == 0x045E || c == 0x040E ||
        c == 0x04B3 || c == 0x04B2 || c == 0x0456 || c == 0x0406;
}

static bool
is_lat_letter(cp_t c)                       /* _LAT_RE */
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == CP_A_ACUTE || c == CP_G_ACUTE ||
        c == CP_I_DOTLESS || c == CP_N_ACUTE || c == CP_O_ACUTE || c == CP_U_ACUTE || c == 0x00C1 ||
        c == 0x01F4 || c == 0x00CD || c == 0x0143 || c == 0x00D3 || c == 0x00DA;
}

static cp_t
homoglyph(cp_t c)                           /* _HOMOGLYPH */
{
    switch (c)
    {
        case 0x0430: return 'a';
        case 0x0435: return 'e';
        case 0x043E: return 'o';
        case 0x0440: return 'p';
        case 0x0441: return 'c';
        case 0x0445: return 'x';
        case 0x0443: return 'y';
        case 0x0456: return 'i';
        case 0x0410: return 'A';
        case 0x0412: return 'B';
        case 0x0415: return 'E';
        case 0x041A: return 'K';
        case 0x041C: return 'M';
        case 0x041D: return 'H';
        case 0x041E: return 'O';
        case 0x0420: return 'P';
        case 0x0421: return 'C';
        case 0x0422: return 'T';
        case 0x0425: return 'X';
        case 0x0423: return 'Y';
        case 0x0406: return 'I';
        default: return c;
    }
}

/* lower(): Í → ı, í → ı, I → i, İ → i, then ordinary lower case */
static cp_t
lower_cp(cp_t c)
{
    if (c == 'I' || c == 0x0130)
        return 'i';
    if (c == 0x00CD || c == 0x00ED)
        return CP_I_DOTLESS;
    if (c >= 'A' && c <= 'Z')
        return c + 0x20;
    if (c >= 0x00C0 && c <= 0x00DE && c != 0x00D7)
        return c + 0x20;
    if ((c >= 0x0100 && c <= 0x012F) || (c >= 0x0132 && c <= 0x0137) || (c >= 0x014A && c <= 0x0177))
        return (c % 2 == 0) ? c + 1 : c;
    if ((c >= 0x0139 && c <= 0x0148) || (c >= 0x0179 && c <= 0x017E))
        return (c % 2 == 1) ? c + 1 : c;
    if (c == 0x0178)
        return 0x00FF;
    if (c == 0x01F4)
        return 0x01F5;
    if (c >= 0x0410 && c <= 0x042F)
        return c + 0x20;
    if (c >= 0x0400 && c <= 0x040F)
        return c + 0x50;
    if ((c >= 0x0460 && c <= 0x0481) || (c >= 0x048A && c <= 0x04BF) || (c >= 0x04D0 && c <= 0x052F))
        return (c % 2 == 0) ? c + 1 : c;
    if ((c >= 0x1E00 && c <= 0x1E95) || (c >= 0x1EA0 && c <= 0x1EFF))
        return (c % 2 == 0) ? c + 1 : c;    /* Latin Extended Additional */
    if (c == 0x04C0)
        return 0x04CF;
    if (c >= 0x04C1 && c <= 0x04CE)
        return (c % 2 == 1) ? c + 1 : c;
    return c;
}

static const char *
cyr_latin(cp_t c)                           /* _CYR */
{
    switch (c)
    {
        case 0x0430: return "a";
        case 0x04D9: return "\303\241";     /* á */
        case 0x0431: return "b";
        case 0x0432: return "v";
        case 0x0433: return "g";
        case 0x0493: return "\307\265";     /* ǵ */
        case 0x0434: return "d";
        case 0x0435: return "e";
        case 0x0451: return "yo";
        case 0x0436: return "j";
        case 0x0437: return "z";
        case 0x0438: return "i";
        case 0x0439: return "y";
        case 0x043A: return "k";
        case 0x049B: return "q";
        case 0x043B: return "l";
        case 0x043C: return "m";
        case 0x043D: return "n";
        case 0x04A3: return "\305\204";     /* ń */
        case 0x043E: return "o";
        case 0x04E9: return "\303\263";     /* ó */
        case 0x043F: return "p";
        case 0x0440: return "r";
        case 0x0441: return "s";
        case 0x0442: return "t";
        case 0x0443: return "u";
        case 0x04AF: return "\303\272";     /* ú */
        case 0x045E: return "w";
        case 0x0444: return "f";
        case 0x0445: return "x";
        case 0x04B3: return "h";
        case 0x0446: return "c";
        case 0x0447: return "ch";
        case 0x0448: return "sh";
        case 0x0449: return "sh";
        case 0x044A: return "";
        case 0x044B: return "\304\261";     /* ı */
        case 0x044C: return "";
        case 0x044D: return "e";
        case 0x044E: return "yu";
        case 0x044F: return "ya";
        default: return NULL;
    }
}

static cp_t
cyr_variant(cp_t c)                         /* _CYR_VARIANT */
{
    switch (c)
    {
        case 0x0473: return 0x04E9;         /* ѳ → ө */
        case 0x0472: return 0x04E8;
        case 0x04CA: return 0x04A3;         /* ӊ → ң */
        case 0x04C9: return 0x04A2;
        case 0x04EF: case 0x04F3: case 0x04F1: return 0x045E;     /* ӯ ӳ ӱ → ў */
        case 0x04EE: case 0x04F2: case 0x04F0: return 0x040E;
        case 0x04FD: return 0x04B3;         /* ӽ → ҳ */
        case 0x04FC: return 0x04B2;
        default: return c;
    }
}

static bool
is_apostrophe(cp_t c)                       /* APOSTROPHES */
{
    return c == '\'' || c == 0x2019 || c == 0x2018 || c == 0x02BB || c == 0x02BC || c == '`' ||
        c == 0x2032 || c == 0x00B4;
}

static bool
is_space(cp_t c)
{
    return c == ' ' || (c >= 0x09 && c <= 0x0D) || (c >= 0x1C && c <= 0x1F) || c == 0x85 || c == 0xA0 ||
        c == 0x1680 || (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x202F ||
        c == 0x205F || c == 0x3000;
}

static int
cmp_compose(const void *a, const void *b)
{
    const KaaCompose *x = a,
               *y = b;

    if (x->base != y->base)
        return x->base < y->base ? -1 : 1;
    if (x->mark != y->mark)
        return x->mark < y->mark ? -1 : 1;
    return 0;
}

/* NFC of a letter and a following combining mark (table generated from Python's unicodedata) */
static cp_t
compose(cp_t base, cp_t mark)
{
    KaaCompose  k;
    const KaaCompose *hit;

    if (mark < 0x0300 || mark > 0x036F)
        return 0;
    k.base = base;
    k.mark = mark;
    k.composed = 0;
    hit = bsearch(&k, kaa_compose, kaa_ncompose, sizeof(KaaCompose), cmp_compose);
    return hit ? hit->composed : 0;
}

static int
nfc(cp_t *s, int n)
{
    int         i,
                k = 0;

    for (i = 0; i < n; i++)
    {
        cp_t        c;

        if (k > 0 && (c = compose(s[k - 1], s[i])) != 0)
            s[k - 1] = c;
        else
            s[k++] = s[i];
    }
    return k;
}

/* normalize.normalize on a code point buffer of capacity cap; returns the new length */
static int
normalize_cps(cp_t *s, int n, int cap)
{
    int         i,
                k;
    bool        has_cyr = false,
                has_lat = false;

    n = nfc(s, n);
    k = 0;
    for (i = 0; i < n; i++)
    {
        cp_t        c = s[i];

        if (c == 0x00AD || c == 0x200B || c == 0x200C || c == 0x200D || c == 0x2060 || c == 0xFEFF)
            continue;                       /* _INVISIBLE */
        if (c >= 0x2010 && c <= 0x2013)
            c = '-';                        /* _DASHES */
        s[k++] = cyr_variant(c);
    }
    n = k;
    while (n > 0 && is_space(s[n - 1]))
        n--;
    for (i = 0; i < n && is_space(s[i]); i++)
        ;
    if (i)
    {
        memmove(s, s + i, (n - i) * sizeof(cp_t));
        n -= i;
    }
    for (i = 0; i < n; i++)
    {
        has_cyr |= is_cyr_letter(s[i]);
        has_lat |= is_lat_letter(s[i]);
    }
    if (has_cyr && has_lat)
        for (i = 0; i < n; i++)
            s[i] = homoglyph(s[i]);         /* shаqаp (Cyrillic а) → shaqap */
    has_cyr = false;
    for (i = 0; i < n; i++)
    {
        s[i] = lower_cp(s[i]);
        has_cyr |= is_cyr_letter(s[i]);
    }
    if (has_cyr)                            /* мектепке → mektepke */
    {
        cp_t       *out = malloc((size_t) cap * sizeof(cp_t));
        int         m = 0;
        cp_t        prev = 0;

        if (!out)
            return -1;
        for (i = 0; i < n; i++)
        {
            cp_t        c = s[i];
            const char *lat = NULL;
            char        two[3] = {'y', 0, 0};

            if ((prev == 0x044A || prev == 0x044C) && (c == 0x0435 || c == 0x043E || c == 0x0438))
            {
                two[1] = c == 0x0435 ? 'e' : (c == 0x043E ? 'o' : 'i');
                lat = two;
            }
            else
                lat = cyr_latin(c);
            if (lat)
            {
                const unsigned char *p = (const unsigned char *) lat;
                const unsigned char *end = p + strlen(lat);

                while (p < end && m < cap)
                    p += utf8_decode(p, end, &out[m++]);
            }
            else if (m < cap)
                out[m++] = c;
            prev = c;
        }
        memcpy(s, out, m * sizeof(cp_t));
        free(out);
        n = m;
    }
    /* so'z → sóz, qi'z → qız */
    k = 0;
    for (i = 0; i < n; i++)
    {
        cp_t        c = s[i];

        if (i + 1 < n && is_apostrophe(s[i + 1]) &&
            (c == 'a' || c == 'g' || c == 'i' || c == 'n' || c == 'o' || c == 'u'))
        {
            s[k++] = c == 'a' ? CP_A_ACUTE : c == 'g' ? CP_G_ACUTE : c == 'i' ? CP_I_DOTLESS :
                c == 'n' ? CP_N_ACUTE : c == 'o' ? CP_O_ACUTE : CP_U_ACUTE;
            i++;
            continue;
        }
        s[k++] = c;
    }
    return nfc(s, k);
}

/* ======================================================================== public API */

static char *
word_to_utf8(const cp_t *s, int n)
{
    char       *out = malloc((size_t) n * 4 + 1);

    if (out)
        raw_utf8(s, n, out);
    return out;
}

/* Decode and normalise; returns a malloc'd code point buffer and its length in *n (NULL on OOM). */
static cp_t *
decode_normalize(const char *word, size_t len, int *n)
{
    const unsigned char *p = (const unsigned char *) word;
    const unsigned char *end = p + len;
    int         cap = (int) len * 2 + 8;    /* Cyrillic letters may become two Latin letters */
    cp_t       *s = malloc((size_t) cap * sizeof(cp_t));
    int         k = 0;

    if (!s)
        return NULL;
    while (p < end)
        p += utf8_decode(p, end, &s[k++]);
    k = normalize_cps(s, k, cap);
    if (k < 0)
    {
        free(s);
        return NULL;
    }
    *n = k;
    return s;
}

char *
kaa_normalize(const char *word, size_t len)
{
    int         n;
    cp_t       *s = decode_normalize(word, len, &n);
    char       *out;

    if (!s)
        return NULL;
    out = word_to_utf8(s, n);
    free(s);
    return out;
}

const char *
kaa_source_name(KaaSource source)
{
    switch (source)
    {
        case KAA_SOURCE_LIST:
            return "list";
        case KAA_SOURCE_LEXICON:
            return "lexicon";
        case KAA_SOURCE_SAFE:
            return "safe";
        default:
            return "unchanged";
    }
}

void
kaa_analysis_free(KaaAnalysis *a)
{
    int         i;

    if (!a)
        return;
    free(a->word);
    free(a->stem);
    if (a->segments)
        for (i = 0; i < a->nsegments; i++)
            free(a->segments[i]);
    free(a->segments);
    free(a->classes);
    memset(a, 0, sizeof(*a));
}

int
kaa_analyze(const char *word, size_t len, KaaAnalysis *out)
{
    return kaa_analyze_ex(word, len, NULL, out);
}

int
kaa_analyze_ex(const char *word, size_t len, const KaaExtra *extra, KaaAnalysis *out)
{
    Ctx         ctx = {false};
    Result     *r;
    KaaWord     w;
    cp_t       *s;
    int         n,
                i;
    bool        hyphen = false;

    kaa_init();
    memset(out, 0, sizeof(*out));
    s = decode_normalize(word, len, &n);
    if (!s)
        return -1;
    r = malloc(sizeof(Result));
    if (!r)
    {
        free(s);
        return -1;
    }
    out->word = word_to_utf8(s, n);
    if (n > KAA_MAX_WORD)
    {
        /* too long to analyse: returned unchanged */
        out->stem = word_to_utf8(s, n);
        out->segments = malloc(sizeof(char *));
        if (out->segments)
            out->segments[0] = word_to_utf8(s, n);
        out->nsegments = 1;
        out->source = KAA_SOURCE_UNCHANGED;
        free(s);
        free(r);
        if (!out->word || !out->stem || !out->segments || !out->segments[0])
        {
            kaa_analysis_free(out);
            return -1;
        }
        return 0;
    }
    word_set(&w, s, n);
    free(s);
    for (i = 0; i < w.n; i++)
        hyphen |= w.c[i] == '-';
    cur_extra = extra;
    if (hyphen)
        analyze_hyphen(&ctx, &w, r);
    else
        analyze_token(&ctx, &w, r);
    cur_extra = NULL;
    if (ctx.oom || !out->word)
    {
        free(r);
        kaa_analysis_free(out);
        return -1;
    }
    out->stem = word_to_utf8(r->stem.c, r->stem.n);
    out->segments = calloc(r->nsegs ? r->nsegs : 1, sizeof(char *));
    out->classes = calloc(r->nclasses ? r->nclasses : 1, sizeof(KaaClass));
    if (!out->stem || !out->segments || !out->classes)
    {
        free(r);
        kaa_analysis_free(out);
        return -1;
    }
    out->nsegments = r->nsegs;
    for (i = 0; i < r->nsegs; i++)
        if (!(out->segments[i] = word_to_utf8(r->segs[i].c, r->segs[i].n)))
        {
            free(r);
            kaa_analysis_free(out);
            return -1;
        }
    out->nclasses = r->nclasses;
    for (i = 0; i < r->nclasses; i++)
        out->classes[i] = (KaaClass) r->classes[i];
    out->source = r->source;
    free(r);
    return 0;
}

/*
 * Cache of recent results of kaa_stem_word: real text repeats a small set of words, so most lookups
 * hit. Direct-mapped by a hash of the input bytes; inputs and stems that do not fit are not cached.
 * The stem depends only on the input bytes, so a hit gives exactly what kaa_analyze would.
 */
#define CACHE_SLOTS 4096
#define CACHE_KEY   48
#define CACHE_STEM  64

typedef struct
{
    unsigned char klen;         /* 0 = empty */
    bool        verbal;
    const KaaExtra *extra;
    char        key[CACHE_KEY];
    char        stem[CACHE_STEM];
} CacheSlot;

static CacheSlot *stem_cache;

static uint32_t
hash_bytes(const char *p, size_t len)
{
    uint32_t    h = 2166136261u;
    size_t      i;

    for (i = 0; i < len; i++)
        h = (h ^ (unsigned char) p[i]) * 16777619u;
    return h;
}

static bool
analysis_verbal(const KaaAnalysis *a)
{
    return a->nclasses > 0 && (kaa_verb_first & KAA_BIT(a->classes[0])) != 0;
}

char *
kaa_stem_word(const char *word, size_t len)
{
    return kaa_stem_word_ex(word, len, NULL, NULL);
}

char *
kaa_stem_word_ex(const char *word, size_t len, const KaaExtra *extra, bool *verbal)
{
    KaaAnalysis a;
    char       *stem;
    CacheSlot  *slot = NULL;

    if (len > 0 && len < CACHE_KEY)
    {
        if (!stem_cache)
            stem_cache = calloc(CACHE_SLOTS, sizeof(CacheSlot));
        if (stem_cache)
        {
            slot = &stem_cache[(hash_bytes(word, len) ^ (uint32_t) (uintptr_t) extra) & (CACHE_SLOTS - 1)];
            if (slot->klen == len && slot->extra == extra && memcmp(slot->key, word, len) == 0)
            {
                size_t      n = strlen(slot->stem);

                stem = malloc(n + 1);
                if (stem)
                    memcpy(stem, slot->stem, n + 1);
                if (verbal)
                    *verbal = slot->verbal;
                return stem;
            }
        }
    }
    if (kaa_analyze_ex(word, len, extra, &a) != 0)
        return NULL;
    if (verbal)
        *verbal = analysis_verbal(&a);
    stem = a.stem;
    a.stem = NULL;
    if (slot && strlen(stem) < CACHE_STEM)
    {
        slot->klen = (unsigned char) len;
        slot->extra = extra;
        slot->verbal = analysis_verbal(&a);
        memcpy(slot->key, word, len);
        strcpy(slot->stem, stem);
    }
    kaa_analysis_free(&a);
    return stem;
}

/* ------------------------------------------------------------------------ user lexicon API */

KaaExtra *
kaa_extra_new(void)
{
    return calloc(1, sizeof(KaaExtra));
}

void
kaa_extra_free(KaaExtra *x)
{
    if (!x)
        return;
    strset_free(&x->nom);
    strset_free(&x->verb);
    free(x);
}

/*
 * Add a stem: kind 'n' (non-verb) or 'v' (verb stem). The word is normalised like any input and
 * stored by its key, as kaastem.lexicon.Data.load_lexicon does. As there, a negative form whose base
 * is a known verb (qoyma = qoy + ma) is not taken as a verb stem.
 */
int
kaa_extra_add(KaaExtra *x, const char *word, size_t len, char kind)
{
    int         n;
    cp_t       *s;
    char        buf[KEY_BYTES];

    kaa_init();
    s = decode_normalize(word, len, &n);
    if (!s)
        return -1;
    if (n == 0 || n > KAA_MAX_WORD)
    {
        free(s);
        return 0;
    }
    if (kind == 'v' && n > 3)
    {
        cp_t        a = s[n - 2],
                    b = s[n - 1];
        int         bn = n - 2;

        if ((a == 'm' || a == 'b' || a == 'p') && (b == 'a' || b == 'e') && has_vowel(s, bn) &&
            in_verb(s, bn) && (cp_t) mbp(sound_class(s, bn)) == a && is_back(s, bn) == (b == 'a'))
        {
            free(s);
            return 0;
        }
    }
    key_utf8(s, n, buf);
    free(s);
    return strset_add(kind == 'v' ? &x->verb : &x->nom, buf);
}

/* ------------------------------------------------------------------------ text normalisation */

static bool
is_word_cp(cp_t c)
{
    return is_alpha(c) || (c >= '0' && c <= '9') || (c >= 0x0300 && c <= 0x036F) || is_apostrophe(c) ||
        c == '-' || (c >= 0x2010 && c <= 0x2013) || c == 0x00AD || c == 0x200B || c == 0x200C ||
        c == 0x200D || c == 0x2060 || c == 0xFEFF;
}

char *
kaa_normalize_text(const char *text, size_t len)
{
    const unsigned char *p = (const unsigned char *) text;
    const unsigned char *end = p + len;
    size_t      cap = len * 2 + 16,
                pos = 0;
    char       *out = malloc(cap);

    if (!out)
        return NULL;
    kaa_init();
    while (p < end)
    {
        const unsigned char *q = p;
        cp_t        c;

        /* a run of word characters is normalised as one word; everything else is copied */
        while (q < end)
        {
            int         k = utf8_decode(q, end, &c);

            if (!is_word_cp(c))
                break;
            q += k;
        }
        if (q > p)
        {
            char       *w = kaa_normalize((const char *) p, q - p);
            size_t      wl;

            if (!w)
            {
                free(out);
                return NULL;
            }
            wl = strlen(w);
            if (pos + wl + 1 > cap)
            {
                char       *o;

                cap = (pos + wl + 1) * 2;
                if (!(o = realloc(out, cap)))
                {
                    free(w);
                    free(out);
                    return NULL;
                }
                out = o;
            }
            memcpy(out + pos, w, wl);
            pos += wl;
            free(w);
            p = q;
            continue;
        }
        {
            int         k = utf8_decode(p, end, &c);

            if (pos + k + 1 > cap)
            {
                char       *o;

                cap = (pos + k + 1) * 2;
                if (!(o = realloc(out, cap)))
                {
                    free(out);
                    return NULL;
                }
                out = o;
            }
            memcpy(out + pos, p, k);
            pos += k;
            p += k;
        }
    }
    out[pos] = '\0';
    return out;
}
