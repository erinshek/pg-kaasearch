#ifndef KAASEARCH_STEMMER_H
#define KAASEARCH_STEMMER_H

#include <stddef.h>

/*
 * Strip one suffix from a Karakalpak word (Latin, lowercase).
 * Returns a palloc'd stem, or a copy of the original if no suffix matched.
 * Minimum stem length: 2 characters.
 * Non-recursive: only one suffix layer is removed.
 */
char *kaa_stem(const char *word, size_t len);

#endif /* KAASEARCH_STEMMER_H */
