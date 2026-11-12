/* Required PostgreSQL headers */
#include "postgres.h"
#include "fmgr.h"

/* Text search dictionary API */
#include "commands/defrem.h"
#include "tsearch/ts_public.h"
#include "utils/builtins.h"

#include "lower.h"
#include "translit.h"
#include "stemmer.h"

PG_MODULE_MAGIC;

/* Internal state for the dictionary */
typedef struct
{
    StopList    stoplist;   /* PostgreSQL stop word list */
} DictKaasearch;

PG_FUNCTION_INFO_V1(kaasearch_init);
PG_FUNCTION_INFO_V1(kaasearch_lexize);

/*
 * Dictionary init function.
 * Called once when the dictionary is created.
 * Loads stop words from the "stopwords" parameter.
 */
Datum
kaasearch_init(PG_FUNCTION_ARGS)
{
    List       *dictoptions = (List *) PG_GETARG_POINTER(0);
    DictKaasearch *d;
    ListCell   *l;
    bool        stoploaded = false;

    d = (DictKaasearch *) palloc0(sizeof(DictKaasearch));

    foreach(l, dictoptions)
    {
        DefElem *defel = (DefElem *) lfirst(l);

        if (strcmp(defel->defname, "stopwords") == 0)
        {
            if (stoploaded)
                ereport(ERROR,
                        (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                         errmsg("multiple StopWords parameters")));

            readstoplist(defGetString(defel), &d->stoplist, NULL);
            stoploaded = true;
        }
        else
        {
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                     errmsg("unrecognized parameter: \"%s\"",
                            defel->defname)));
        }
    }

    PG_RETURN_POINTER(d);
}

/*
 * Dictionary lexize function.
 * Called for each token by the text search parser.
 *
 * Pipeline:
 *   1. Lowercase the token
 *   2. If Cyrillic → transliterate to Latin
 *   3. Check stop words → if match, filter out
 *   4. Stem the word (strip one suffix)
 *   5. Return lexeme
 */
Datum
kaasearch_lexize(PG_FUNCTION_ARGS)
{
    DictKaasearch  *d = (DictKaasearch *) PG_GETARG_POINTER(0);
    char           *in = (char *) PG_GETARG_POINTER(1);
    int32           len = PG_GETARG_INT32(2);
    char           *txt;
    TSLexeme       *res;

    /* Step 1: lowercase (Karakalpak rules, independent of the database locale) */
    txt = kaa_lower(in, len);

    /* Step 2: transliterate Cyrillic to Latin if needed */
    if (contains_cyrillic(txt, strlen(txt)))
    {
        char *transliterated = cyrillic_to_latin(txt, strlen(txt));
        pfree(txt);
        txt = transliterated;
    }

    /* Step 3: check stop words */
    if (d->stoplist.len > 0 && searchstoplist(&d->stoplist, txt))
    {
        pfree(txt);
        /* Return empty array = stop word, filter it out */
        res = palloc0(sizeof(TSLexeme));
        PG_RETURN_POINTER(res);
    }

    /* Step 4: stem the word */
    {
        char *stemmed = kaa_stem(txt, strlen(txt));
        pfree(txt);
        txt = stemmed;
    }

    /* Step 5: return the lexeme */
    res = palloc0(sizeof(TSLexeme) * 2);
    res[0].lexeme = txt;

    PG_RETURN_POINTER(res);
}
