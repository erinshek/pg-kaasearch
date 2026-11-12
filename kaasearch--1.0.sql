/* kaasearch extension: full-text search for the Karakalpak language */

\echo Use "CREATE EXTENSION kaasearch" to load this file. \quit

/* C functions of the text search template */
CREATE FUNCTION kaasearch_init(internal)
    RETURNS internal
    AS 'MODULE_PATHNAME'
    LANGUAGE C STRICT;

CREATE FUNCTION kaasearch_lexize(internal, internal, internal, internal)
    RETURNS internal
    AS 'MODULE_PATHNAME'
    LANGUAGE C STRICT;

/* Step 1: the text search template */
CREATE TEXT SEARCH TEMPLATE kaasearch_template (
    INIT   = kaasearch_init,
    LEXIZE = kaasearch_lexize
);

/* Step 2: the dictionary with its stop words ($SHAREDIR/tsearch_data/kaasearch.stop).
   A user lexicon can be added: ALTER TEXT SEARCH DICTIONARY kaasearch_dict (Lexicon = name)
   reads $SHAREDIR/tsearch_data/name.kaalex */
CREATE TEXT SEARCH DICTIONARY kaasearch_dict (
    TEMPLATE  = kaasearch_template,
    StopWords = kaasearch
);

/* Step 3: the text search configuration, based on the default parser */
CREATE TEXT SEARCH CONFIGURATION kaasearch (
    PARSER = pg_catalog.default
);

COMMENT ON TEXT SEARCH CONFIGURATION kaasearch
    IS 'Configuration for Karakalpak language (Latin/Cyrillic)';

/* Step 4: map word tokens to our dictionary (words with digits included: covid19, 5-mektepke) */
ALTER TEXT SEARCH CONFIGURATION kaasearch
    ALTER MAPPING FOR asciiword, asciihword, hword_asciipart,
                      word, hword, hword_part,
                      numword, numhword, hword_numpart
    WITH kaasearch_dict;

/* Step 5: index numbers as they are (2016, 3.14, 1.2.3) */
ALTER TEXT SEARCH CONFIGURATION kaasearch
    ALTER MAPPING FOR int, uint, float, sfloat, version
    WITH simple;

/* Step 6: functions for working with the stemmer directly */
CREATE FUNCTION kaa_stem(word text)
    RETURNS text
    AS 'MODULE_PATHNAME', 'kaa_stem_pg'
    LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

COMMENT ON FUNCTION kaa_stem(text)
    IS 'Stem of one Karakalpak word: kaa_stem(''mektebimizde'') = ''mektep''';

CREATE FUNCTION kaa_analyze(word text,
                            OUT word text, OUT stem text, OUT segments text[],
                            OUT classes text[], OUT source text)
    RETURNS record
    AS 'MODULE_PATHNAME', 'kaa_analyze_pg'
    LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

COMMENT ON FUNCTION kaa_analyze(text)
    IS 'Analysis of one Karakalpak word: normalised word, stem, segmentation, suffix classes and source';

CREATE FUNCTION kaa_normalize(txt text)
    RETURNS text
    AS 'MODULE_PATHNAME', 'kaa_normalize_pg'
    LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

COMMENT ON FUNCTION kaa_normalize(text)
    IS 'Text with every word in 2016 Latin spelling and lower case (Cyrillic, apostrophe spelling)';
