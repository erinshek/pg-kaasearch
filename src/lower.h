#ifndef KAASEARCH_LOWER_H
#define KAASEARCH_LOWER_H

#include <stddef.h>

/*
 * Lower-case a UTF-8 string for Karakalpak, independent of the database
 * locale (lc_ctype) and of the PostgreSQL version.
 *
 *   Í → ı   (Karakalpak: Í is the capital of dotless ı)
 *   I → i
 *   Á Ó Ú Ǵ Ń and other Latin-1 capitals → their small letters
 *   Cyrillic capitals (incl. Ғ Қ Ң Ү Ў Ҳ Ә Ө) → small letters
 *
 * Other characters are copied unchanged. The result is never longer than
 * the input. Returns a palloc'd, null-terminated string.
 */
char *kaa_lower(const char *input, size_t len);

#endif /* KAASEARCH_LOWER_H */
