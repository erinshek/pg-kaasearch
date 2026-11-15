-- Load extension
CREATE EXTENSION pg_kaasearch;

-- 1. Basic Latin tokenization
SELECT to_tsvector('kaasearch', 'Qaraqalpaqstan');

-- 2. Basic Cyrillic tokenization (should produce same result as Latin)
SELECT to_tsvector('kaasearch', 'Қарақалпақстан');

-- 3. Cross-script: both scripts produce identical lexemes
SELECT to_tsvector('kaasearch', 'Qaraqalpaqstan') =
       to_tsvector('kaasearch', 'Қарақалпақстан') AS cross_script_match;

-- 4. Stop words are filtered out, numerals are kept
SELECT to_tsvector('kaasearch', 'ol hám bul, sebebi bir eki');

-- 5. Mixed text: stop words removed, content words kept
SELECT to_tsvector('kaasearch', 'bul Respublika');

-- 6. Stemming: plural suffix -lar stripped
SELECT to_tsvector('kaasearch', 'kitaplar');

-- 7. Stemming: case suffix -da stripped
SELECT to_tsvector('kaasearch', 'qalada');

-- 8. Stemming: genitive suffix -nıń stripped, vowel restored
SELECT to_tsvector('kaasearch', 'xalıqtıń xalqımızdıń');

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

-- 14. Karakalpak ў (U+045E) and Ў (U+040E) → w
SELECT to_tsvector('kaasearch', 'суў СУЎ таўлар');
SELECT to_tsvector('kaasearch', 'суў')
    @@ to_tsquery('kaasearch', 'suw') AS w_match;

-- 15. Lower case: Í → ı, I → i, Karakalpak capitals (locale independent)
SELECT to_tsvector('kaasearch', 'ÍDÍS Ídıs ıdıs');
SELECT to_tsvector('kaasearch', 'ÓZBEKSTAN ǴÁREZSIZLIK JAŃA');
SELECT to_tsvector('kaasearch', 'Ídıs') @@ to_tsquery('kaasearch', 'ıdıs') AS dotless_match;

-- 16. Numbers and words with digits are indexed
SELECT to_tsvector('kaasearch', '1991-jılı 5-mektepke COVID19 2016 3.14 -7');
SELECT to_tsvector('kaasearch', 'Ǵárezsizlik 1991-jılı') @@ to_tsquery('kaasearch', '1991') AS number_match;

-- 17. Real words that look like suffixes are not stop words
SELECT to_tsvector('kaasearch', 'tar kóshe, ter shıqtı');

-- 18. Soft sign before е → ye
SELECT to_tsvector('kaasearch', 'пьеса');

-- 19. Apostrophe spelling (a' i' g' o' n' u'): the parser joins a letter and its apostrophe, so
--     o'zbek is one word, ózbek; all apostrophe signs are read alike; a word in quotation marks
--     keeps its last n ('Qazan'); kaa_normalize() gives the same text
SELECT to_tsvector('kaasearch', 'O''zbekstan qi''zdi''n'' g''alaba U''YDE A''DEBIYAT');
SELECT to_tsvector('kaasearch', 'g''alaba o''zbek') = to_tsvector('kaasearch', 'ǵalaba ózbek') AS apostrophe_match;
SELECT to_tsvector('kaasearch', 'so‘z soʻz so’z so`z');
SELECT to_tsvector('kaasearch', '''Qazan'' qalası, ten'' huqıq');
-- a letter with its apostrophe at the end of a word (bardi' = bardı), and a quotation of several
-- words whose closing mark follows n ('Jańa Ózbekstan' stays ózbekstan)
SELECT to_tsvector('kaasearch', 'bardi'' kitabi'' xalqi''');
SELECT to_tsvector('kaasearch', '''Jańa Ózbekstan'' gazetası, ''O''zbekstan'' kitabi''');
SELECT kaa_normalize('''Jańa Ózbekstan'' gazetası, bardi'' ''Qazan''');
SELECT to_tsvector('kaasearch', kaa_normalize('g''alaba o''zbek')) = to_tsvector('kaasearch', 'ǵalaba ózbek') AS normalize_match;
SELECT ts_lexize('kaasearch_dict', 'o''zbekstanda');
SELECT ts_headline('kaasearch', 'Bizler O''zbekstan mektebinde oqıdıq.', to_tsquery('kaasearch', 'ózbekstan & mektep'));
SELECT alias, token, lexemes FROM ts_debug('kaasearch', 'o''zbek tili');

-- 19b. Letters that old fonts use in place of Karakalpak letters: ѳ → ө, ӊ → ң, ӯ ӳ ӱ → ў, ӽ → ҳ
SELECT to_tsvector('kaasearch', 'Нѳкис оқытыўшыныӊ қорғаӯ ӼАЯЛЛАР');
SELECT to_tsvector('kaasearch', 'Нѳкис оқытыўшыныӊ') = to_tsvector('kaasearch', 'Нөкис оқытыўшының') AS old_font_match;

-- 19c. Punctuation around a word is left out: in a database with the C locale the default parser
--      gives «Mektep» or “Мектеп” as one token
SELECT ts_lexize('kaasearch_dict', '«Mektep»'), ts_lexize('kaasearch_dict', '“Мектепке”'),
       ts_lexize('kaasearch_dict', '—'), ts_lexize('kaasearch_dict', '…');

-- 20. Verb forms: tense, person, negation and voice suffixes are stripped
SELECT to_tsvector('kaasearch', 'jazıladı barmadım keleseń oqıp otır');

-- 21. A stem that is a stop word is dropped, whatever its suffixes (eken, ekenin); other words are
--     kept in every form (bardı, bardıq, barǵan → bar)
SELECT to_tsvector('kaasearch', 'eken ekenin mektep');
SELECT to_tsvector('kaasearch', 'bardı bardıq barǵan bolǵan bolıp');

-- 22. A stem of two words gives one lexeme per word
SELECT to_tsvector('kaasearch', 'ákeldi');
SELECT to_tsvector('kaasearch', 'Ол китапты әкелди') @@ to_tsquery('kaasearch', 'alıp & keldi') AS two_word_stem;

-- 23. Search: every form of a word finds the others
CREATE TEMP TABLE docs(id int, body text);
INSERT INTO docs VALUES
    (1, 'Bizler mektebimizde jańa kitaplardı oqıdıq.'),
    (2, 'Мектепке барған балалар китап оқыйды.'),
    (3, 'Qaraqalpaq xalqınıń tariyxı haqqında maqala.'),
    (4, 'Ol keshe qalaǵa ketti.');
SELECT id FROM docs WHERE to_tsvector('kaasearch', body) @@ plainto_tsquery('kaasearch', 'mektep kitap') ORDER BY id;
SELECT id FROM docs WHERE to_tsvector('kaasearch', body) @@ plainto_tsquery('kaasearch', 'xalıqtıń tariyxın') ORDER BY id;
SELECT id FROM docs WHERE to_tsvector('kaasearch', body) @@ websearch_to_tsquery('kaasearch', 'qala or oqıydı') ORDER BY id;

-- 24. Ranking and an index
CREATE INDEX docs_fts ON docs USING gin (to_tsvector('kaasearch', body));
SELECT id, round(ts_rank(to_tsvector('kaasearch', body), q)::numeric, 4) AS rank
FROM docs, to_tsquery('kaasearch', 'kitap | mektep') q
WHERE to_tsvector('kaasearch', body) @@ q ORDER BY rank DESC, id;

-- 25. Phrase search: words in a row, stop words keep their position
SELECT to_tsvector('kaasearch', 'Qaraqalpaq xalqınıń tariyxı') @@ phraseto_tsquery('kaasearch', 'xalıq tariyxı') AS phrase_match;
SELECT to_tsvector('kaasearch', 'tariyxı xalqınıń') @@ phraseto_tsquery('kaasearch', 'xalıq tariyxı') AS phrase_wrong_order;
SELECT phraseto_tsquery('kaasearch', 'mektep hám kitap');

-- 26. ts_headline marks the forms found
SELECT ts_headline('kaasearch', 'Bizler mektebimizde jańa kitaplardı oqıdıq.',
                   to_tsquery('kaasearch', 'mektep & kitap'));

-- 27. ts_debug shows the dictionary used for each token
SELECT alias, token, dictionaries, lexemes FROM ts_debug('kaasearch', 'Mektepke 2 bala keldi');

-- 28. SQL functions
SELECT kaa_stem('mektebimizde'), kaa_stem('Мектепке'), kaa_stem('ákeldi'), kaa_stem('');
SELECT kaa_normalize('Мектепке BARDIQ, g''alaba! ÍDÍS');
SELECT * FROM kaa_analyze('kelgenligin');
SELECT * FROM kaa_analyze('maǵan');
SELECT * FROM kaa_analyze('Lissabonnan');
SELECT kaa_stem(NULL) IS NULL AS strict;

-- 29. User lexicon: stems the built-in lexicon does not know
SELECT to_tsvector('kaasearch', 'Lissabonǵa skanerledi');
ALTER TEXT SEARCH DICTIONARY kaasearch_dict (StopWords = kaasearch, Lexicon = kaasearch_example);
SELECT to_tsvector('kaasearch', 'Lissabonǵa skanerledi');
ALTER TEXT SEARCH DICTIONARY kaasearch_dict (Lexicon);
SELECT to_tsvector('kaasearch', 'Lissabonǵa skanerledi');

-- 30. Dictionary option errors
-- (the message has the install path, so only the error code is printed)
DO $$
BEGIN
    ALTER TEXT SEARCH DICTIONARY kaasearch_dict (Lexicon = no_such_lexicon);
EXCEPTION WHEN OTHERS THEN
    RAISE NOTICE 'missing lexicon: %', SQLSTATE;
END $$;
CREATE TEXT SEARCH DICTIONARY kaa_bad (TEMPLATE = kaasearch_template, Foo = bar);
CREATE TEXT SEARCH DICTIONARY kaa_nostop (TEMPLATE = kaasearch_template);
SELECT ts_lexize('kaa_nostop', 'maǵan'), ts_lexize('kaasearch_dict', 'maǵan');
DROP TEXT SEARCH DICTIONARY kaa_nostop;

-- Clean up
DROP TABLE docs;
DROP EXTENSION pg_kaasearch;
