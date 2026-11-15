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

/* The user lexicon is malloc'd by the core: free it when PostgreSQL drops the dictionary's memory */
static void
free_extra(void *arg)
{
    kaa_extra_free((KaaExtra *) arg);
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
            d->extra = read_lexicon(defGetString(defel));
            {
                /* init runs in the dictionary's own memory context, which is reset when the
                 * dictionary changes (ALTER TEXT SEARCH DICTIONARY) or is dropped */
                MemoryContextCallback *cb = palloc0(sizeof(MemoryContextCallback));

                cb->func = free_extra;
                cb->arg = d->extra;
                MemoryContextRegisterResetCallback(CurrentMemoryContext, cb);
            }
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

    /* the surface form is checked against the stop list (only a dictionary with one needs it) */
    if (d->stoplist.len > 0)
    {
        norm = kaa_normalize(in, len);
        if (!norm)
            out_of_memory();
        if (is_stop(d, norm))
        {
            free(norm);
            PG_RETURN_POINTER(palloc0(sizeof(TSLexeme)));
        }
        free(norm);
    }

    stem = kaa_stem_word_ex(in, len, d->extra, &verbal);
    if (!stem)
        out_of_memory();
    if (stem[0] == '\0' || (!verbal && is_stop(d, stem)))
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

/*
 * Parser start function: the default parser on the text with the apostrophe spelling joined
 * (kaa_join_apostrophes: o'zbek → ózbek, bardi' → bardı; a closing quotation mark is kept).
 * The other parser functions are the default parser's own (see pg_kaasearch--1.0.sql).
 */
PG_FUNCTION_INFO_V1(kaasearch_prsd_start);

Datum
kaasearch_prsd_start(PG_FUNCTION_ARGS)
{
    char       *in = (char *) PG_GETARG_POINTER(0);
    int         len = PG_GETARG_INT32(1);
    size_t      outlen;
    char       *joined;
    char       *text;

    check_encoding();
    joined = kaa_join_apostrophes(in, len, &outlen);
    if (!joined)
        out_of_memory();
    /* the default parser keeps a pointer to the text until it ends, so it lives in palloc'd memory */
    text = pnstrdup(joined, outlen);
    free(joined);
    return DirectFunctionCall2(prsd_start, PointerGetDatum(text), Int32GetDatum((int) outlen));
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
