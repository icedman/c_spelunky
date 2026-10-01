/*
 * gm_string - GML string built-ins (GameMaker-HTML5 Function_String.js).
 *
 * The runner works on JS (UTF-16) strings but counts characters by code point
 * (surrogate pairs are one character, e.g. string_length L333, string_char_at L544).
 * Strings here are UTF-8, so the same code-point positions are walked in UTF-8:
 * results agree for any text. string_lower / string_upper only map ASCII letters
 * (JS toLowerCase is Unicode-wide; Spelunky only changes ASCII text).
 *
 * Results are gm_heap strings (or one of the arguments / a literal when nothing
 * changes); on heap exhaustion they are "" (gm_heap_failures counts it). Indices
 * are GML reals already converted with yyGetInt32 by the caller.
 */
#ifndef GM_STRING_H
#define GM_STRING_H

#include <stdint.h>

int gm_string_length(const char *s);                               /* code points */
const char *gm_string_char_at(const char *s, int32_t index);       /* 1-based */
const char *gm_string_delete(const char *s, int32_t index, int32_t count);
const char *gm_string_insert(const char *substr, const char *s, int32_t index);
int gm_string_pos(const char *substr, const char *s);              /* 1-based, 0 = absent */
const char *gm_string_lower(const char *s);
const char *gm_string_upper(const char *s);
const char *gm_string_hash_to_newline(const char *s);              /* "#" -> "\r\n", "\#" -> "#" */
double gm_string_ord(const char *s);                               /* first code point, 0 if empty */

#endif /* GM_STRING_H */
