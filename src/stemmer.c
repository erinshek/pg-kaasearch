#include "postgres.h"
#include "fmgr.h"

#include <string.h>

#include "stemmer.h"

/*
 * A suffix rule: if the word ends with `suffix`, strip it.
 * Longer suffixes must come first (longest match wins).
 */
typedef struct
{
    const char *suffix;
    int         len;    /* byte length of suffix */
} SuffixRule;

/*
 * Suffix rules for Karakalpak, grouped by category.
 * Ordered longest-first within each group.
 *
 * Note: some suffixes contain multi-byte UTF-8 characters:
 *   ń = \xC5\x84 (2 bytes)
 *   á = \xC3\xA1 (2 bytes)
 *   ǵ = \xC7\xB5 (2 bytes)
 *   ı = \xC4\xB1 (2 bytes)
 *   ó = \xC3\xB3 (2 bytes)
 *   ú = \xC3\xBA (2 bytes)
 */
static const SuffixRule suffixes[] = {

    /* Participle: -ǵan, -gen, -qan, -ken */
    {"\xC7\xB5" "an",  4},     /* -ǵan */
    {"gen",             3},     /* -gen */
    {"qan",             3},     /* -qan */
    {"ken",             3},     /* -ken */

    /* Ablative: -dan, -den (not -tan/-ten: too many false positives) */
    {"dan",             3},     /* -dan */
    {"den",             3},     /* -den */

    /* Plural: -lar, -ler, -dar, -der, -tar, -ter */
    {"lar",             3},     /* -lar */
    {"ler",             3},     /* -ler */
    {"dar",             3},     /* -dar */
    {"der",             3},     /* -der */
    {"tar",             3},     /* -tar */
    {"ter",             3},     /* -ter */

    /* 1st person plural possessive: -mız, -miz */
    {"m\xC4\xB1z",     4},     /* -mız */
    {"miz",             3},     /* -miz */

    /* 2nd person plural possessive: -ńız, -ńiz */
    {"\xC5\x84\xC4\xB1z", 5},  /* -ńız */
    {"\xC5\x84iz",     4},     /* -ńiz */

    /* Genitive: -nıń, -niń, -tıń, -tiń */
    {"n\xC4\xB1\xC5\x84", 5},  /* -nıń */
    {"t\xC4\xB1\xC5\x84", 5},  /* -tıń */
    {"ni\xC5\x84",     4},     /* -niń */
    {"ti\xC5\x84",     4},     /* -tiń */

    /* Dative: -ǵa, -ge, -qa, -ke */
    {"\xC7\xB5" "a",   3},     /* -ǵa */
    {"ge",              2},     /* -ge */
    {"qa",              2},     /* -qa */
    {"ke",              2},     /* -ke */

    /* Accusative: -nı, -ni */
    {"n\xC4\xB1",      3},     /* -nı */
    {"ni",              2},     /* -ni */

    /* Locative: -da, -de, -ta, -te */
    {"da",              2},     /* -da */
    {"de",              2},     /* -de */
    {"ta",              2},     /* -ta */
    {"te",              2},     /* -te */

    /* Past tense: -dı, -di, -tı, -ti */
    {"d\xC4\xB1",      3},     /* -dı */
    {"di",              2},     /* -di */
    {"t\xC4\xB1",      3},     /* -tı */
    {"ti",              2},     /* -ti */

    /* 3rd person possessive: -sı, -si */
    {"s\xC4\xB1",      3},     /* -sı */
    {"si",              2},     /* -si */

    /* 1st person singular possessive: -ım, -im */
    {"\xC4\xB1m",      3},     /* -ım */
    {"im",              2},     /* -im */

    /* 2nd person singular possessive: -ıń, -iń */
    {"\xC4\xB1\xC5\x84", 4},   /* -ıń */
    {"i\xC5\x84",      3},     /* -iń */

    /* 3rd person possessive (short): -ı, -i */
    {"\xC4\xB1",       2},     /* -ı */
    {"i",               1},     /* -i */

    /* Short possessive/person: -m, -ń */
    {"m",               1},     /* -m */
    {"\xC5\x84",       2},     /* -ń */
};

#define SUFFIX_COUNT (sizeof(suffixes) / sizeof(SuffixRule))

/*
 * Count UTF-8 characters in a byte string.
 * Used to enforce minimum stem length.
 */
static int
utf8_char_count(const char *s, size_t bytes)
{
    const unsigned char *p = (const unsigned char *) s;
    const unsigned char *end = p + bytes;
    int count = 0;

    while (p < end)
    {
        if (*p < 0x80)
            p += 1;
        else if ((*p & 0xE0) == 0xC0)
            p += 2;
        else if ((*p & 0xF0) == 0xE0)
            p += 3;
        else
            p += 4;

        count++;
    }

    return count;
}

char *
kaa_stem(const char *word, size_t len)
{
    size_t  i;
    char   *stem;

    for (i = 0; i < SUFFIX_COUNT; i++)
    {
        int slen = suffixes[i].len;

        /* Word must be longer than suffix */
        if ((int) len <= slen)
            continue;

        /* Check if word ends with this suffix */
        if (memcmp(word + len - slen, suffixes[i].suffix, slen) == 0)
        {
            size_t stem_bytes = len - slen;

            /* Stem must be at least 2 UTF-8 characters */
            if (utf8_char_count(word, stem_bytes) < 2)
                continue;

            stem = palloc(stem_bytes + 1);
            memcpy(stem, word, stem_bytes);
            stem[stem_bytes] = '\0';
            return stem;
        }
    }

    /* No suffix matched: return a copy of the original */
    stem = palloc(len + 1);
    memcpy(stem, word, len);
    stem[len] = '\0';
    return stem;
}
