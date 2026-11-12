/*
 * libFuzzer target for the C stemmer core: arbitrary bytes (invalid UTF-8, NUL bytes, very long
 * input) must never crash it, read or write out of bounds, leak memory or loop forever.
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
    KaaAnalysis a;
    char       *s;

    if (kaa_analyze((const char *) data, size, &a) == 0)
    {
        /* every string must be terminated and the counts consistent */
        if (strlen(a.word) > 4 * size + 16 || a.nsegments < 1 || a.nclasses < 0)
            abort();
        kaa_analysis_free(&a);
    }
    s = kaa_normalize((const char *) data, size);
    free(s);
    s = kaa_stem_word((const char *) data, size);
    free(s);
    return 0;
}
