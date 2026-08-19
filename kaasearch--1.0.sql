/* kaasearch extension: Full-text search for Karakalpak language */

/* Step 1: Register C functions as a text search template */
CREATE TEXT SEARCH TEMPLATE kaasearch_template (
    INIT   = kaasearch_init,
    LEXIZE = kaasearch_lexize
);

/* Step 2: Create a dictionary using the template + stop words file */
CREATE TEXT SEARCH DICTIONARY kaasearch_dict (
    TEMPLATE  = kaasearch_template,
    StopWords = kaasearch
);

/* Step 3: Create a text search configuration based on pg_catalog.simple parser */
CREATE TEXT SEARCH CONFIGURATION kaasearch (
    PARSER = pg_catalog.default
);

COMMENT ON TEXT SEARCH CONFIGURATION kaasearch
    IS 'Configuration for Karakalpak language (Latin/Cyrillic)';

/* Step 4: Map all token types to our dictionary */
ALTER TEXT SEARCH CONFIGURATION kaasearch
    ALTER MAPPING FOR asciiword, asciihword, hword_asciipart,
                      word, hword, hword_part
    WITH kaasearch_dict;
