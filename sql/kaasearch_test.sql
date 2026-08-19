-- Load extension
CREATE EXTENSION kaasearch;

-- 1. Basic Latin tokenization
SELECT to_tsvector('kaasearch', 'Qaraqalpaqstan');

-- 2. Basic Cyrillic tokenization (should produce same result as Latin)
SELECT to_tsvector('kaasearch', 'Қарақалпақстан');

-- 3. Cross-script: both scripts produce identical lexemes
SELECT to_tsvector('kaasearch', 'Qaraqalpaqstan') =
       to_tsvector('kaasearch', 'Қарақалпақстан') AS cross_script_match;

-- 4. Stop words are filtered out
SELECT to_tsvector('kaasearch', 'ol bir eki');

-- 5. Mixed text: stop words removed, content words kept
SELECT to_tsvector('kaasearch', 'bul Respublika');

-- 6. Stemming: plural suffix -lar stripped
SELECT to_tsvector('kaasearch', 'kitaplar');

-- 7. Stemming: case suffix -da stripped
SELECT to_tsvector('kaasearch', 'qalada');

-- 8. Stemming: genitive suffix -nıń stripped
SELECT to_tsvector('kaasearch', 'xalıqtıń');

-- 9. Cyrillic stemming: transliterate then stem
SELECT to_tsvector('kaasearch', 'китаплар');

-- 10. Cross-script stemming match
SELECT to_tsvector('kaasearch', 'kitaplar') =
       to_tsvector('kaasearch', 'китаплар') AS stem_cross_match;

-- 11. Query matching: Latin query finds Cyrillic document
SELECT to_tsvector('kaasearch', 'Қарақалпақстан Республикасы')
    @@ to_tsquery('kaasearch', 'respublika') AS query_match;

-- 12. Special transliteration: ъе → ye
SELECT to_tsvector('kaasearch', 'объект');

-- 13. Multi-word cross-script search
SELECT to_tsvector('kaasearch', 'Қарақалпақ тили')
    @@ to_tsquery('kaasearch', 'qaraqalpaq & til') AS multi_word_match;

-- Clean up
DROP EXTENSION kaasearch;
