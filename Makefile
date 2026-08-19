# Shared library name (produces kaasearch.so / kaasearch.dylib)
MODULE_big = kaasearch

# Extension name for CREATE EXTENSION
EXTENSION = kaasearch

# SQL script executed on CREATE EXTENSION
DATA = kaasearch--1.0.sql

# C source files to compile
OBJS = src/kaasearch.o src/translit.o src/stemmer.o

# Regression test (runs sql/kaasearch_test.sql, compares with expected/)
REGRESS = kaasearch_test

# Path to pg_config (override with PG_CONFIG=/path/to/pg_config)
PG_CONFIG ?= pg_config

# Include PostgreSQL Extension Build System (PGXS)
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
