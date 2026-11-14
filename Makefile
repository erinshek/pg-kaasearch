# Shared library name (produces kaasearch.so / kaasearch.dylib)
MODULE_big = kaasearch

# Extension name for CREATE EXTENSION
EXTENSION = kaasearch

# SQL script executed on CREATE EXTENSION
DATA = kaasearch--1.0.sql

# Stop-word file, installed into $(sharedir)/tsearch_data/
DATA_TSEARCH = data/kaasearch.stop data/kaasearch_example.kaalex

# C source files to compile
OBJS = src/kaasearch.o src/kaadata.o src/kaacore.o

# Regression test (runs sql/kaasearch_test.sql, compares with expected/)
REGRESS = kaasearch_test kaasearch_words kaasearch_search

# Path to pg_config (override with PG_CONFIG=/path/to/pg_config)
PG_CONFIG ?= pg_config

# Include PostgreSQL Extension Build System (PGXS)
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

# Regenerate src/kaadata.[ch] from kaastem after editing its rules or lists
.PHONY: tables
tables:
	python3 tools/gen_tables.py
	python3 tools/gen_sql_tests.py

# Equivalence test: the C stemmer core against kaastem (needs python3 and a C compiler)
.PHONY: check-core
check-core:
	$(CC) -std=c99 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
		-Isrc test/core/kaacore_cli.c src/kaacore.c src/kaadata.c -o test/core/kaacore_cli
	python3 test/core/compare.py test/core/kaacore_cli

# Lexicon coverage on the text corpora in corpus/ (not in the repository); writes corpus/unknown.tsv
CORPUS ?= $(wildcard corpus/*.txt)
.PHONY: coverage
coverage:
	$(CC) -std=c99 -O2 -Isrc test/core/kaacore_cli.c src/kaacore.c src/kaadata.c -o test/core/kaacore_cli
	python3 tools/coverage.py test/core/kaacore_cli $(CORPUS) --unknown corpus/unknown.tsv

# Fuzz the C stemmer core (needs clang with libFuzzer); FUZZ_TIME is in seconds
FUZZ_TIME ?= 60
.PHONY: fuzz-core
fuzz-core:
	clang -g -O1 -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined \
		-Isrc test/core/fuzz_kaacore.c src/kaacore.c src/kaadata.c -o test/core/fuzz_kaacore
	test/core/fuzz_kaacore -max_total_time=$(FUZZ_TIME) -max_len=256 -timeout=5
