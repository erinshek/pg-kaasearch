/*
 * libFuzzer target for the C stemmer core: arbitrary bytes (invalid UTF-8, NUL bytes, very long
 * input) must never crash it, read or write out of bounds, leak memory or loop forever.
 *
 * Every public function is fed the input: word analysis and stemming, word and text normalisation,
 * the apostrophe joiner of the parser, and a user lexicon built from the input itself.
 *
 * Build and run (clang):
 *   clang -g -O1 -fsanitize=fuzzer,address,undefined -Isrc test/core/fuzz_kaacore.c \
 *         src/kaacore.c src/kaadata.c -o fuzz_kaacore
 *   ./fuzz_kaacore -max_total_time=60 -max_len=256
 */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "kaacore.h"

int         LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    const char *in = (const char *) data;
    KaaAnalysis a;
    KaaExtra   *x;
    char       *s;
    size_t      n,
                i,
                start;

    if (kaa_analyze(in, size, &a) == 0)
    {
        /* every string must be terminated and the counts consistent */
        if (strlen(a.word) > 4 * size + 16 || a.nsegments < 1 || a.nclasses < 0)
            abort();
        kaa_analysis_free(&a);
    }
    s = kaa_normalize(in, size);
    free(s);
    s = kaa_stem_word(in, size);
    free(s);

    /* the parser's apostrophe joiner never makes the text longer */
    s = kaa_join_apostrophes(in, size, &n);
    if (s && (n > size || strlen(s) != n) && memchr(in, '\0', size) == NULL)
        abort();
    free(s);
    s = kaa_normalize_text(in, size);
    free(s);
    n = kaa_word_span(in, size, &start);
    if (start + n > size)
        abort();

    /* a user lexicon made of the input's lines, alternately non-verbs and verb stems */
    x = kaa_extra_new();
    if (x)
    {
        for (i = 0, start = 0; i <= size; i++)
            if (i == size || data[i] == '\n')
            {
                kaa_extra_add(x, in + start, i - start, (start % 2) ? 'v' : 'n');
                start = i + 1;
            }
        s = kaa_stem_word_ex(in, size, x, NULL);
        free(s);
        if (kaa_analyze_ex(in, size, x, &a) == 0)
            kaa_analysis_free(&a);
        kaa_extra_free(x);
    }
    return 0;
}
