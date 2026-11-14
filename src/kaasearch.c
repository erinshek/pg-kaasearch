/*
 * kaasearch: full-text search for the Karakalpak language.
 *
 * The text search dictionary (template kaasearch_template) and the SQL functions kaa_stem,
 * kaa_analyze and kaa_normalize. The stemming itself is done by the core in kaacore.c.
 */
#include "postgres.h"
#include "fmgr.h"

#include "catalog/pg_type.h"
#include "commands/defrem.h"
#include "funcapi.h"
#include "mb/pg_wchar.h"
#include "tsearch/ts_locale.h"
#include "tsearch/ts_public.h"
#include "utils/array.h"
#include "utils/builtins.h"

#include "kaacore.h"

PG_MODULE_MAGIC;

/* Internal state of a dictionary */
typedef struct
{
    StopList    stoplist;       /* StopWords = name: $SHAREDIR/tsearch_data/name.stop */
    KaaExtra   *extra;          /* Lexicon = name: $SHAREDIR/tsearch_data/name.kaalex, or NULL */
} DictKaasearch;

PG_FUNCTION_INFO_V1(kaasearch_init);
PG_FUNCTION_INFO_V1(kaasearch_lexize);
PG_FUNCTION_INFO_V1(kaa_stem_pg);
PG_FUNCTION_INFO_V1(kaa_analyze_pg);
PG_FUNCTION_INFO_V1(kaa_normalize_pg);

static void
check_encoding(void)
{
    /*
     * The core works on UTF-8 bytes, and the stop-word and lexicon files are UTF-8. Refuse other
     * encodings up front with a clear message instead of failing later on a decoding error.
     */
    if (GetDatabaseEncoding() != PG_UTF8)
        ereport(ERROR,
                (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                 errmsg("pg_kaasearch requires a UTF8 database"),
                 errdetail("The current database encoding is %s.",
                           GetDatabaseEncodingName()),
                 errhint("Create the database with ENCODING 'UTF8'.")));
}

static void
out_of_memory(void)
{
    ereport(ERROR,
            (errcode(ERRCODE_OUT_OF_MEMORY),
             errmsg("out of memory in the kaasearch stemmer")));
}

/*
 * Read a user lexicon: one stem per line, "word<TAB>n" for a non-verb or "word<TAB>v" for a verb
 * stem; a line with only a word is a non-verb. Text after '#' is a comment.
 */
static KaaExtra *
read_lexicon(const char *name)
{
    char       *filename = get_tsearch_config_filename(name, "kaalex");
    tsearch_readline_state trst;
    KaaExtra   *extra = kaa_extra_new();
    char       *line;

    if (!extra)
        out_of_memory();
    if (!tsearch_readline_begin(&trst, filename))
    {
        kaa_extra_free(extra);
        ereport(ERROR,
                (errcode(ERRCODE_CONFIG_FILE_ERROR),
                 errmsg("could not open kaasearch lexicon file \"%s\": %m", filename)));
    }
    while ((line = tsearch_readline(&trst)) != NULL)
    {
        char       *hash = strchr(line, '#');
        char       *word = line;
        char       *kind;
        size_t      wl;
        char        k = 'n';

        if (hash)
            *hash = '\0';
        while (*word == ' ' || *word == '\t')
            word++;
        wl = strcspn(word, " \t\r\n");
        kind = word + wl;
        while (*kind == ' ' || *kind == '\t')
            kind++;
        if (*kind == 'v' || *kind == 'V')
            k = 'v';
        else if (*kind != '\0' && *kind != '\r' && *kind != '\n' && *kind != 'n' && *kind != 'N')
        {
            kaa_extra_free(extra);
            ereport(ERROR,
                    (errcode(ERRCODE_CONFIG_FILE_ERROR),
                     errmsg("invalid line in kaasearch lexicon file \"%s\"", filename),
                     errdetail("The second column must be \"n\" (non-verb) or \"v\" (verb stem).")));
        }
        if (wl > 0 && kaa_extra_add(extra, word, wl, k) != 0)
        {
            kaa_extra_free(extra);
            out_of_memory();
        }
        pfree(line);
    }
    tsearch_readline_end(&trst);
    pfree(filename);
    return extra;
}

/*
 * Dictionary init function: options StopWords = name and Lexicon = name.
 */
Datum
kaasearch_init(PG_FUNCTION_ARGS)
{
    List       *dictoptions = (List *) PG_GETARG_POINTER(0);
    DictKaasearch *d;
    ListCell   *l;
    bool        stoploaded = false;

    check_encoding();
    d = (DictKaasearch *) palloc0(sizeof(DictKaasearch));

    foreach(l, dictoptions)
    {
        DefElem    *defel = (DefElem *) lfirst(l);

        if (strcmp(defel->defname, "stopwords") == 0)
        {
            if (stoploaded)
                ereport(ERROR,
                        (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                         errmsg("multiple StopWords parameters")));
            readstoplist(defGetString(defel), &d->stoplist, NULL);
            stoploaded = true;
        }
        else if (strcmp(defel->defname, "lexicon") == 0)
        {
            if (d->extra)
                ereport(ERROR,
                        (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                         errmsg("multiple Lexicon parameters")));
            /* lives as long as the dictionary: the backend keeps dictionaries until it exits */
            d->extra = read_lexicon(defGetString(defel));
        }
        else
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                     errmsg("unrecognized parameter: \"%s\"", defel->defname)));
    }

    PG_RETURN_POINTER(d);
}

static bool
is_stop(DictKaasearch *d, const char *word)
{
    return d->stoplist.len > 0 && searchstoplist(&d->stoplist, (char *) word);
}

/*
 * Dictionary lexize function, called for each token.
 *
 *   1. normalise the token (lower case, Cyrillic → Latin, apostrophe spelling)
 *   2. a stop word is dropped
 *   3. stem it (kaacore)
 *   4. a stem that is a stop word is dropped too (maǵan → men), unless it was found through a verb
 *      suffix chain (bardı → bar stays even if bar is a stop word)
 *   5. a stem of two words (ákeldi → alıp kel) gives one lexeme per word, each stemmed: al, kel
 */
Datum
kaasearch_lexize(PG_FUNCTION_ARGS)
{
    DictKaasearch *d = (DictKaasearch *) PG_GETARG_POINTER(0);
    char       *in = (char *) PG_GETARG_POINTER(1);
    int32       len = PG_GETARG_INT32(2);
    char       *norm;
    char       *stem;
    bool        verbal = false;
    TSLexeme   *res;
    int         nlex = 0;
    char       *p;

    norm = kaa_normalize(in, len);
    if (!norm)
        out_of_memory();
    if (norm[0] == '\0' || is_stop(d, norm))
    {
        free(norm);
        PG_RETURN_POINTER(palloc0(sizeof(TSLexeme)));
    }
    free(norm);

    stem = kaa_stem_word_ex(in, len, d->extra, &verbal);
    if (!stem)
        out_of_memory();
    if (!verbal && is_stop(d, stem))
    {
        free(stem);
        PG_RETURN_POINTER(palloc0(sizeof(TSLexeme)));
    }

    /* one lexeme per space-separated word of the stem; the array ends with an empty entry */
    res = palloc0(sizeof(TSLexeme) * (strlen(stem) / 2 + 2));
    for (p = stem; *p;)
    {
        size_t      n = strcspn(p, " ");

        if (n > 0 && p == stem && p[n] == '\0')
            res[nlex++].lexeme = pnstrdup(p, n);
        else if (n > 0)
        {
            /* a word of a multi-word stem is stemmed on its own: alıp → al */
            bool        v;
            char       *part = kaa_stem_word_ex(p, n, d->extra, &v);

            if (!part)
            {
                free(stem);
                out_of_memory();
            }
            res[nlex++].lexeme = pstrdup(part);
            free(part);
        }
        p += n;
        while (*p == ' ')
            p++;
    }
    free(stem);
    PG_RETURN_POINTER(res);
}

/* ------------------------------------------------------------------------ parser */

/* An apostrophe sign of the apostrophe spelling (normalize.APOSTROPHES) */
static bool
is_apostrophe_cp(pg_wchar c)
{
    return c == '\'' || c == '`' || c == 0x2019 || c == 0x2018 || c == 0x02BB || c == 0x02BC ||
        c == 0x2032 || c == 0x00B4;
}

/* A letter: Latin (with Latin-1, Extended-A/B and Extended Additional) or Cyrillic */
static bool
is_letter_cp(pg_wchar c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= 0x00C0 && c <= 0x024F && c != 0x00D7 && c != 0x00F7) ||
        (c >= 0x0400 && c <= 0x052F) || (c >= 0x1E00 && c <= 0x1EFF);
}

/* The 2016 letter for a letter of the apostrophe spelling: o' → ó, I' → Í, i' → ı */
static const char *
apostrophe_letter(pg_wchar c)
{
    switch (c)
    {
        case 'a': return "\303\241";        /* á */
        case 'A': return "\303\201";        /* Á */
        case 'g': return "\307\265";        /* ǵ */
        case 'G': return "\307\264";        /* Ǵ */
        case 'i': return "\304\261";        /* ı */
        case 'I': return "\303\215";        /* Í */
        case 'n': return "\305\204";        /* ń */
        case 'N': return "\305\203";        /* Ń */
        case 'o': return "\303\263";        /* ó */
        case 'O': return "\303\223";        /* Ó */
        case 'u': return "\303\272";        /* ú */
        case 'U': return "\303\232";        /* Ú */
        default: return NULL;
    }
}

/*
 * Write the apostrophe spelling in 2016 letters, so that the default parser, which ends a word at
 * an apostrophe, sees o'zbek as one word: ózbek. A letter + apostrophe is joined when a letter
 * follows (o'zbek, qi'z), and n' also at the end of a word (qi'zdi'n'), unless the word opened with
 * an apostrophe, which is then a quotation mark ('Qazan'). Case is kept, and the result is never
 * longer than the input.
 */
static char *
join_apostrophes(const char *in, int len, int *outlen)
{
    pg_wchar   *cp = palloc((len + 1) * sizeof(pg_wchar));
    int        *off = palloc((len + 2) * sizeof(int));
    char       *out = palloc(len + 1);
    int         n = 0,
                pos = 0,
                k = 0,
                i;

    /* decode, keeping the byte offset of every character */
    while (pos < len)
    {
        int         l = pg_utf_mblen((const unsigned char *) in + pos);

        if (l < 1 || pos + l > len)
            l = 1;
        off[n] = pos;
        cp[n] = (l == 1) ? (unsigned char) in[pos] : utf8_to_unicode((const unsigned char *) in + pos);
        n++;
        pos += l;
    }
    off[n] = len;

    for (i = 0; i < n; i++)
    {
        const char *rep = (i + 1 < n && is_apostrophe_cp(cp[i + 1])) ? apostrophe_letter(cp[i]) : NULL;

        if (rep)
        {
            bool        next_letter = i + 2 < n && is_letter_cp(cp[i + 2]);
            bool        quoted = false;

            if (!next_letter && (cp[i] == 'n' || cp[i] == 'N'))
            {
                /* find the start of the word, stepping over joined pairs (qi'z), and look before it */
                int         j = i;

                while (j > 0 && (is_letter_cp(cp[j - 1]) ||
                                 (j > 1 && is_apostrophe_cp(cp[j - 1]) && apostrophe_letter(cp[j - 2]))))
                    j -= is_letter_cp(cp[j - 1]) ? 1 : 2;
                quoted = j > 0 && is_apostrophe_cp(cp[j - 1]);
            }
            if (next_letter || ((cp[i] == 'n' || cp[i] == 'N') && !quoted))
            {
                out[k++] = rep[0];
                out[k++] = rep[1];
                i++;                        /* the apostrophe */
                continue;
            }
        }
        memcpy(out + k, in + off[i], off[i + 1] - off[i]);
        k += off[i + 1] - off[i];
    }
    out[k] = '\0';
    *outlen = k;
    pfree(cp);
    pfree(off);
    return out;
}

/*
 * Parser start function: the default parser on the text with the apostrophe spelling joined.
 * The other parser functions are the default parser's own (see pg_kaasearch--1.0.sql).
 */
PG_FUNCTION_INFO_V1(kaasearch_prsd_start);

Datum
kaasearch_prsd_start(PG_FUNCTION_ARGS)
{
    char       *in = (char *) PG_GETARG_POINTER(0);
    int         len = PG_GETARG_INT32(1);
    int         outlen;
    char       *text;

    check_encoding();
    text = join_apostrophes(in, len, &outlen);
    return DirectFunctionCall2(prsd_start, PointerGetDatum(text), Int32GetDatum(outlen));
}

/* ------------------------------------------------------------------------ SQL functions */

/* kaa_stem(word text) → text: the stem of one word (built-in lexicon) */
Datum
kaa_stem_pg(PG_FUNCTION_ARGS)
{
    text       *t = PG_GETARG_TEXT_PP(0);
    char       *stem;
    text       *res;

    check_encoding();
    stem = kaa_stem_word(VARDATA_ANY(t), VARSIZE_ANY_EXHDR(t));
    if (!stem)
        out_of_memory();
    res = cstring_to_text(stem);
    free(stem);
    PG_RETURN_TEXT_P(res);
}

/* kaa_normalize(text) → text: the text with every word normalised */
Datum
kaa_normalize_pg(PG_FUNCTION_ARGS)
{
    text       *t = PG_GETARG_TEXT_PP(0);
    char       *norm;
    text       *res;

    check_encoding();
    norm = kaa_normalize_text(VARDATA_ANY(t), VARSIZE_ANY_EXHDR(t));
    if (!norm)
        out_of_memory();
    res = cstring_to_text(norm);
    free(norm);
    PG_RETURN_TEXT_P(res);
}

static ArrayType *
text_array(char **items, int n)
{
    Datum      *elems = palloc(sizeof(Datum) * (n > 0 ? n : 1));
    int         i;

    for (i = 0; i < n; i++)
        elems[i] = PointerGetDatum(cstring_to_text(items[i]));
    return construct_array(elems, n, TEXTOID, -1, false, TYPALIGN_INT);
}

/*
 * kaa_analyze(word text, OUT word text, OUT stem text, OUT segments text[], OUT classes text[],
 *             OUT source text): the full analysis of one word
 */
Datum
kaa_analyze_pg(PG_FUNCTION_ARGS)
{
    text       *t = PG_GETARG_TEXT_PP(0);
    TupleDesc   tupdesc;
    KaaAnalysis a;
    Datum       values[5];
    bool        nulls[5] = {false, false, false, false, false};
    char      **classes;
    int         i;
    HeapTuple   tuple;

    check_encoding();
    if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
        ereport(ERROR,
                (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                 errmsg("function returning record called in context that cannot accept type record")));
    tupdesc = BlessTupleDesc(tupdesc);

    if (kaa_analyze_ex(VARDATA_ANY(t), VARSIZE_ANY_EXHDR(t), NULL, &a) != 0)
        out_of_memory();
    classes = palloc(sizeof(char *) * (a.nclasses > 0 ? a.nclasses : 1));
    for (i = 0; i < a.nclasses; i++)
        classes[i] = (char *) kaa_class_names[a.classes[i]];
    values[0] = PointerGetDatum(cstring_to_text(a.word));
    values[1] = PointerGetDatum(cstring_to_text(a.stem));
    values[2] = PointerGetDatum(text_array(a.segments, a.nsegments));
    values[3] = PointerGetDatum(text_array(classes, a.nclasses));
    values[4] = PointerGetDatum(cstring_to_text(kaa_source_name(a.source)));
    kaa_analysis_free(&a);

    tuple = heap_form_tuple(tupdesc, values, nulls);
    PG_RETURN_DATUM(HeapTupleGetDatum(tuple));
}
