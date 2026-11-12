/*
 * Command-line driver of the C stemmer core, used by test/core/compare.py.
 *
 * Reads one word per line from standard input and prints, tab-separated:
 *   word, stem, segments joined with "|", suffix classes joined with " ", source
 *
 * Option: -l FILE  a user lexicon (lines "word<TAB>n|v", as kaastem's lexicon files)
 *
 * Build: cc -std=c99 -O2 -Isrc test/core/kaacore_cli.c src/kaacore.c src/kaadata.c -o kaacore_cli
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kaacore.h"

static KaaExtra *
load_lexicon(const char *path)
{
    static char line[4096];
    FILE       *f = fopen(path, "r");
    KaaExtra   *x = kaa_extra_new();

    if (!f || !x)
        return NULL;
    while (fgets(line, sizeof line, f))
    {
        char       *tab;

        line[strcspn(line, "#\r\n")] = '\0';
        tab = strchr(line, '\t');
        if (!tab)
            continue;
        *tab = '\0';
        if (kaa_extra_add(x, line, strlen(line), tab[1] == 'v' ? 'v' : 'n') != 0)
            return NULL;
    }
    fclose(f);
    return x;
}

int
main(int argc, char **argv)
{
    static char line[1 << 16];
    KaaExtra   *extra = NULL;

    if (argc == 3 && strcmp(argv[1], "-l") == 0 && !(extra = load_lexicon(argv[2])))
    {
        fprintf(stderr, "cannot load %s\n", argv[2]);
        return 2;
    }

    while (fgets(line, sizeof line, stdin))
    {
        size_t      n = strcspn(line, "\n");
        KaaAnalysis a;
        int         i;

        line[n] = '\0';
        if (kaa_analyze_ex(line, n, extra, &a) != 0)
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
    kaa_extra_free(extra);
    return 0;
}
