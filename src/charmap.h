#ifndef KAASEARCH_CHARMAP_H
#define KAASEARCH_CHARMAP_H

/*
 * Cyrillic to Latin character mapping for Karakalpak language.
 * Source: kaalin-python/kaalin/constants.py
 *
 * All output is lowercase Latin — pipeline normalizes to lowercase.
 *
 * Some Latin outputs are multi-byte UTF-8:
 *   á = \xC3\xA1  (U+00E1)
 *   ǵ = \xC7\xB5  (U+01F5)
 *   ı = \xC4\xB1  (U+0131, dotless i)
 *   ń = \xC5\x84  (U+0144)
 *   ó = \xC3\xB3  (U+00F3)
 *   ú = \xC3\xBA  (U+00FA)
 */

typedef struct
{
    uint32      codepoint;  /* Cyrillic Unicode code point */
    const char *latin;      /* Latin equivalent (lowercase) */
} CyrLatinEntry;

/* Sorted by codepoint for binary search */
static const CyrLatinEntry cyr_to_lat[] = {

    /* === Uppercase Cyrillic (U+0401, U+040E, U+0410–U+042F) === */

    {0x0401, "yo"},             /* Ё */
    {0x040E, "w"},              /* Ў (Karakalpak) */
    {0x0410, "a"},              /* А */
    {0x0411, "b"},              /* Б */
    {0x0412, "v"},              /* В */
    {0x0413, "g"},              /* Г */
    {0x0414, "d"},              /* Д */
    {0x0415, "e"},              /* Е */
    {0x0416, "j"},              /* Ж */
    {0x0417, "z"},              /* З */
    {0x0418, "i"},              /* И */
    {0x0419, "y"},              /* Й */
    {0x041A, "k"},              /* К */
    {0x041B, "l"},              /* Л */
    {0x041C, "m"},              /* М */
    {0x041D, "n"},              /* Н */
    {0x041E, "o"},              /* О */
    {0x041F, "p"},              /* П */
    {0x0420, "r"},              /* Р */
    {0x0421, "s"},              /* С */
    {0x0422, "t"},              /* Т */
    {0x0423, "u"},              /* У */
    {0x0424, "f"},              /* Ф */
    {0x0425, "x"},              /* Х */
    {0x0426, "c"},              /* Ц */
    {0x0427, "ch"},             /* Ч */
    {0x0428, "sh"},             /* Ш */
    {0x0429, "sh"},             /* Щ */
    {0x042A, ""},               /* Ъ (hard sign, dropped) */
    {0x042B, "\xC4\xB1"},      /* Ы → ı */
    {0x042C, ""},               /* Ь (soft sign, dropped) */
    {0x042D, "e"},              /* Э */
    {0x042E, "yu"},             /* Ю */
    {0x042F, "ya"},             /* Я */

    /* === Lowercase Cyrillic (U+0430–U+044F, U+0451, U+045E) === */

    {0x0430, "a"},              /* а */
    {0x0431, "b"},              /* б */
    {0x0432, "v"},              /* в */
    {0x0433, "g"},              /* г */
    {0x0434, "d"},              /* д */
    {0x0435, "e"},              /* е */
    {0x0436, "j"},              /* ж */
    {0x0437, "z"},              /* з */
    {0x0438, "i"},              /* и */
    {0x0439, "y"},              /* й */
    {0x043A, "k"},              /* к */
    {0x043B, "l"},              /* л */
    {0x043C, "m"},              /* м */
    {0x043D, "n"},              /* н */
    {0x043E, "o"},              /* о */
    {0x043F, "p"},              /* п */
    {0x0440, "r"},              /* р */
    {0x0441, "s"},              /* с */
    {0x0442, "t"},              /* т */
    {0x0443, "u"},              /* у */
    {0x0444, "f"},              /* ф */
    {0x0445, "x"},              /* х */
    {0x0446, "c"},              /* ц */
    {0x0447, "ch"},             /* ч */
    {0x0448, "sh"},             /* ш */
    {0x0449, "sh"},             /* щ */
    {0x044A, ""},               /* ъ (hard sign, dropped) */
    {0x044B, "\xC4\xB1"},      /* ы → ı */
    {0x044C, ""},               /* ь (soft sign, dropped) */
    {0x044D, "e"},              /* э */
    {0x044E, "yu"},             /* ю */
    {0x044F, "ya"},             /* я */
    {0x0451, "yo"},             /* ё */
    {0x045E, "w"},              /* ў (Karakalpak) */

    /* === Extended Karakalpak Cyrillic (sorted by codepoint) === */

    {0x0492, "\xC7\xB5"},      /* Ғ → ǵ */
    {0x0493, "\xC7\xB5"},      /* ғ → ǵ */
    {0x049A, "q"},              /* Қ */
    {0x049B, "q"},              /* қ */
    {0x04A2, "\xC5\x84"},      /* Ң → ń */
    {0x04A3, "\xC5\x84"},      /* ң → ń */
    {0x04AE, "\xC3\xBA"},      /* Ү → ú */
    {0x04AF, "\xC3\xBA"},      /* ү → ú */
    {0x04B2, "h"},              /* Ҳ */
    {0x04B3, "h"},              /* ҳ */
    {0x04D8, "\xC3\xA1"},      /* Ә → á */
    {0x04D9, "\xC3\xA1"},      /* ә → á */
    {0x04E8, "\xC3\xB3"},      /* Ө → ó */
    {0x04E9, "\xC3\xB3"},      /* ө → ó */
};

#define CYR_TO_LAT_SIZE (sizeof(cyr_to_lat) / sizeof(CyrLatinEntry))

#endif /* KAASEARCH_CHARMAP_H */
