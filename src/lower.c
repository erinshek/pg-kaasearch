#include "postgres.h"

#include <string.h>

#include "lower.h"

/*
 * Decode one UTF-8 sequence starting at p (p < end).
 * Returns the code point and sets *bytes; a malformed or truncated
 * sequence is reported as a single byte with code point 0xFFFFFFFF.
 */
static uint32
decode(const unsigned char *p, const unsigned char *end, int *bytes)
{
    size_t  avail = end - p;

    if (p[0] < 0x80)
    {
        *bytes = 1;
        return p[0];
    }
    if ((p[0] & 0xE0) == 0xC0 && avail >= 2 && (p[1] & 0xC0) == 0x80)
    {
        *bytes = 2;
        return ((uint32) (p[0] & 0x1F) << 6) | (p[1] & 0x3F);
    }
    if ((p[0] & 0xF0) == 0xE0 && avail >= 3 &&
        (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80)
    {
        *bytes = 3;
        return ((uint32) (p[0] & 0x0F) << 12) |
               ((uint32) (p[1] & 0x3F) << 6) | (p[2] & 0x3F);
    }
    *bytes = 1;
    return 0xFFFFFFFF;
}

/* Lower-case one code point; returns cp itself if it has no small form here. */
static uint32
lower_cp(uint32 cp)
{
    if (cp >= 'A' && cp <= 'Z')
        return cp + 0x20;
    if (cp == 0x00CD)                       /* Í → ı */
        return 0x0131;
    if (cp >= 0x00C0 && cp <= 0x00DE && cp != 0x00D7)
        return cp + 0x20;                   /* Latin-1: Á Ó Ú ... */
    if (cp == 0x0130)                       /* İ → i */
        return 'i';
    if (cp == 0x0143)                       /* Ń → ń */
        return 0x0144;
    if (cp == 0x01F4)                       /* Ǵ → ǵ */
        return 0x01F5;
    if (cp >= 0x0410 && cp <= 0x042F)       /* А–Я */
        return cp + 0x20;
    if (cp >= 0x0400 && cp <= 0x040F)       /* Ё, Ў ... */
        return cp + 0x50;
    if (cp >= 0x0490 && cp <= 0x04BF && cp % 2 == 0)
        return cp + 1;                      /* Ғ Қ Ң Ү Ұ Ҳ ... */
    if (cp >= 0x04D0 && cp <= 0x04FF && cp % 2 == 0)
        return cp + 1;                      /* Ә Ө ... */
    return cp;
}

static int
encode(uint32 cp, char *out)
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
    out[0] = (char) (0xE0 | (cp >> 12));
    out[1] = (char) (0x80 | ((cp >> 6) & 0x3F));
    out[2] = (char) (0x80 | (cp & 0x3F));
    return 3;
}

char *
kaa_lower(const char *input, size_t len)
{
    const unsigned char *p = (const unsigned char *) input;
    const unsigned char *end = p + len;
    char       *result = palloc(len + 1);
    size_t      pos = 0;

    while (p < end)
    {
        int     bytes;
        uint32  cp = decode(p, end, &bytes);
        uint32  lc = (cp == 0xFFFFFFFF) ? cp : lower_cp(cp);

        if (lc == cp)
        {
            memcpy(result + pos, p, bytes);     /* unchanged: copy as is */
            pos += bytes;
        }
        else
            pos += encode(lc, result + pos);    /* never longer than the input */
        p += bytes;
    }
    result[pos] = '\0';
    return result;
}
