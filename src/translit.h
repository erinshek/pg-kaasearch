#ifndef KAASEARCH_TRANSLIT_H
#define KAASEARCH_TRANSLIT_H

#include <stddef.h>

/*
 * Convert a UTF-8 Cyrillic string to Latin (Karakalpak).
 * Non-Cyrillic characters are copied as-is.
 * Returns a palloc'd null-terminated string.
 *
 * Special context rules:
 *   ьи → yi,  ьо → yo,  ье → ye,  ъе → ye
 */
char *cyrillic_to_latin(const char *input, size_t len);

/* Check if a UTF-8 string contains any Cyrillic characters */
int contains_cyrillic(const char *input, size_t len);

#endif /* KAASEARCH_TRANSLIT_H */
