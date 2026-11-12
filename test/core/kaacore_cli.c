/*
 * Command-line driver of the C stemmer core, used by test/core/compare.py.
 *
 * Reads one word per line from standard input and prints, tab-separated:
 *   word, stem, segments joined with "|", suffix classes joined with " ", source
 *
 * Build: cc -std=c99 -O2 -Isrc test/core/kaacore_cli.c src/kaacore.c src/kaadata.c -o kaacore_cli
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kaacore.h"

int
main(void)
{
    static char line[1 << 16];

    while (fgets(line, sizeof line, stdin))
    {
        size_t      n = strcspn(line, "\n");
        KaaAnalysis a;
        int         i;

        line[n] = '\0';
        if (kaa_analyze(line, n, &a) != 0)
        {
            fprintf(stderr, "out of memory on: %s\n", line);
            return 2;
        }
        printf("%s\t%s\t", line, a.stem);
        for (i = 0; i < a.nsegments; i++)
            printf("%s%s", i ? "|" : "", a.segments[i]);
        printf("\t");
        for (i = 0; i < a.nclasses; i++)
            printf("%s%s", i ? " " : "", kaa_class_names[a.classes[i]]);
        printf("\t%s\n", kaa_source_name(a.source));
        kaa_analysis_free(&a);
    }
    return 0;
}
