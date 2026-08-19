#include "postgres.h"
#include "fmgr.h"

#include <string.h>

#include "translit.h"
#include "charmap.h"

/*
 * Decode one UTF-8 character into a Unicode code point.
 * Sets *bytes to the number of bytes consumed.
 */
static uint32
utf8_decode(const unsigned char *p, int *bytes)
{
    /* ASCII: single byte */
    if (p[0] < 0x80)
    {
        *bytes = 1;
        return p[0];
    }

    /* 2-byte sequence: 110xxxxx 10xxxxxx */
    if ((p[0] & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80)
    {
        *bytes = 2;
        return ((uint32)(p[0] & 0x1F) << 6) | (p[1] & 0x3F);
    }

    /* 3-byte sequence: 1110xxxx 10xxxxxx 10xxxxxx */
    if ((p[0] & 0xF0) == 0xE0 &&
        (p[1] & 0xC0) == 0x80 &&
        (p[2] & 0xC0) == 0x80)
    {
        *bytes = 3;
        return ((uint32)(p[0] & 0x0F) << 12) |
               ((uint32)(p[1] & 0x3F) << 6) |
               (p[2] & 0x3F);
    }

    /* 4-byte sequence: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx */
    if ((p[0] & 0xF8) == 0xF0 &&
        (p[1] & 0xC0) == 0x80 &&
        (p[2] & 0xC0) == 0x80 &&
        (p[3] & 0xC0) == 0x80)
    {
        *bytes = 4;
        return ((uint32)(p[0] & 0x07) << 18) |
               ((uint32)(p[1] & 0x3F) << 12) |
               ((uint32)(p[2] & 0x3F) << 6) |
               (p[3] & 0x3F);
    }

    /* Invalid UTF-8: skip one byte */
    *bytes = 1;
    return p[0];
}

/*
 * Binary search for a Cyrillic code point in cyr_to_lat table.
 * Returns the Latin string, or NULL if not found.
 */
static const char *
lookup_cyr(uint32 cp)
{
    int lo = 0;
    int hi = CYR_TO_LAT_SIZE - 1;

    while (lo <= hi)
    {
        int mid = (lo + hi) / 2;

        if (cyr_to_lat[mid].codepoint == cp)
            return cyr_to_lat[mid].latin;
        else if (cyr_to_lat[mid].codepoint < cp)
            lo = mid + 1;
        else
            hi = mid - 1;
    }

    return NULL;
}

/*
 * Peek at the next Unicode code point without advancing.
 */
static uint32
peek_next(const unsigned char *p, const unsigned char *end)
{
    int dummy;

    if (p >= end)
        return 0;

    return utf8_decode(p, &dummy);
}

int
contains_cyrillic(const char *input, size_t len)
{
    const unsigned char *p = (const unsigned char *) input;
    const unsigned char *end = p + len;

    while (p < end)
    {
        int     bytes;
        uint32  cp = utf8_decode(p, &bytes);

        /* Cyrillic block: U+0400 – U+04FF */
        if (cp >= 0x0400 && cp <= 0x04FF)
            return 1;

        p += bytes;
    }

    return 0;
}

char *
cyrillic_to_latin(const char *input, size_t len)
{
    const unsigned char *p = (const unsigned char *) input;
    const unsigned char *end = p + len;

    /* Worst case: each 2-byte Cyrillic char becomes 2 Latin chars */
    char   *result = palloc(len * 2 + 1);
    size_t  pos = 0;

    while (p < end)
    {
        int     bytes;
        uint32  cp = utf8_decode(p, &bytes);

        /* Not Cyrillic: copy as-is */
        if (cp < 0x0400 || cp > 0x04FF)
        {
            memcpy(result + pos, p, bytes);
            pos += bytes;
            p += bytes;
            continue;
        }

        /*
         * Context-sensitive rules (before normal lookup):
         *
         *   ь/Ь (U+044C/U+042C) + и/И (U+0438/U+0418) → "yi"
         *   ь/Ь (U+044C/U+042C) + о/О (U+043E/U+041E) → "yo"
         *   ъ/Ъ (U+044A/U+042A) + е/Е (U+0435/U+0415) → "ye"
         */
        if (cp == 0x044C || cp == 0x042C)   /* ь or Ь */
        {
            uint32 next = peek_next(p + bytes, end);

            if (next == 0x0438 || next == 0x0418)   /* и/И → yi */
            {
                int skip;
                result[pos++] = 'y';
                result[pos++] = 'i';
                p += bytes;
                utf8_decode(p, &skip);
                p += skip;
                continue;
            }
            if (next == 0x043E || next == 0x041E)   /* о/О → yo */
            {
                int skip;
                result[pos++] = 'y';
                result[pos++] = 'o';
                p += bytes;
                utf8_decode(p, &skip);
                p += skip;
                continue;
            }
        }

        if (cp == 0x044A || cp == 0x042A)   /* ъ or Ъ */
        {
            uint32 next = peek_next(p + bytes, end);

            if (next == 0x0435 || next == 0x0415)   /* е/Е → ye */
            {
                int skip;
                result[pos++] = 'y';
                result[pos++] = 'e';
                p += bytes;
                utf8_decode(p, &skip);
                p += skip;
                continue;
            }
        }

        /* Normal lookup in charmap */
        const char *latin = lookup_cyr(cp);

        if (latin != NULL)
        {
            size_t llen = strlen(latin);
            memcpy(result + pos, latin, llen);
            pos += llen;
        }
        /* Unknown Cyrillic: silently skip */

        p += bytes;
    }

    result[pos] = '\0';
    return result;
}
