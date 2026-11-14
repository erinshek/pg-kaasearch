# pg_kaasearch

Full-text search for the Karakalpak language in PostgreSQL. It finds every
form of a word, searches Latin and Cyrillic text alike, reads the
apostrophe spelling and drops stop words.

## Example

```sql
CREATE EXTENSION pg_kaasearch;

SELECT to_tsvector('kaasearch', 'Bizler mektebimizde jańa kitaplardı oqıdıq.');
-- 'jańa':3 'kitap':4 'mektep':2 'oqı':5

SELECT to_tsvector('kaasearch', 'Мектепке барған балалар китап оқыйды.')
    @@ to_tsquery('kaasearch', 'mektep & kitap');
-- true
```

## Install

Requirements: PostgreSQL 15–18 with its development files, a C compiler and `make`. The database must
use the UTF8 encoding.

```sh
git clone https://github.com/erinshek/pg-kaasearch.git
cd pg-kaasearch
make
sudo make install
```

Then, in your database:

```sql
CREATE EXTENSION pg_kaasearch;
```

## Usage

Index a column and search it:

```sql
CREATE INDEX articles_fts ON articles USING gin (to_tsvector('kaasearch', body));

SELECT title
FROM articles
WHERE to_tsvector('kaasearch', body) @@ websearch_to_tsquery('kaasearch', 'mektep kitap');
```

Rank the results and highlight the words found:

```sql
SELECT title,
       ts_rank(to_tsvector('kaasearch', body), q) AS rank,
       ts_headline('kaasearch', body, q) AS snippet
FROM articles, websearch_to_tsquery('kaasearch', 'mektep kitap') AS q
WHERE to_tsvector('kaasearch', body) @@ q
ORDER BY rank DESC;
```

## License

[MIT License](LICENSE)
