/* kaasearch extension: Full-text search for Karakalpak language */

/* Register C functions for the text search template */
CREATE FUNCTION kaasearch_init(internal)
    RETURNS internal
    AS 'MODULE_PATHNAME'
    LANGUAGE C STRICT;

CREATE FUNCTION kaasearch_lexize(internal, internal, internal, internal)
    RETURNS internal
    AS 'MODULE_PATHNAME'
    LANGUAGE C STRICT;

/* Step 1: Create a text search template using the C functions */
CREATE TEXT SEARCH TEMPLATE kaasearch_template (
    INIT   = kaasearch_init,
    LEXIZE = kaasearch_lexize
);

/* Step 2: Create a dictionary using the template + stop words file */
CREATE TEXT SEARCH DICTIONARY kaasearch_dict (
    TEMPLATE  = kaasearch_template,
    StopWords = kaasearch
);

/* Step 3: Create a text search configuration based on the default parser */
CREATE TEXT SEARCH CONFIGURATION kaasearch (
    PARSER = pg_catalog.default
);

COMMENT ON TEXT SEARCH CONFIGURATION kaasearch
    IS 'Configuration for Karakalpak language (Latin/Cyrillic)';

/* Step 4: Map word tokens to our dictionary (words with digits included: covid19, 5-mektepke) */
ALTER TEXT SEARCH CONFIGURATION kaasearch
    ALTER MAPPING FOR asciiword, asciihword, hword_asciipart,
                      word, hword, hword_part,
                      numword, numhword, hword_numpart
    WITH kaasearch_dict;

/* Step 5: Index numbers as they are (2016, 3.14, 1.2.3) */
ALTER TEXT SEARCH CONFIGURATION kaasearch
    ALTER MAPPING FOR int, uint, float, sfloat, version
    WITH simple;
