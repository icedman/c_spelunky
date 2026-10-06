/*
 * gm_string - see gm_string.h. References are Function_String.js lines.
 */
#include "gm_string.h"

#include "gm_heap.h"

#include <stdbool.h>
#include <string.h>

static bool is_cont(unsigned char c)
{
    return (c & 0xC0u) == 0x80u;
}

/* Byte offset after `n` code points from the start (clamped to the end). */
static size_t advance(const char *s, int32_t n)
{
    size_t i = 0;

    while (n > 0 && s[i] != '\0') {
        i++;
        while (s[i] != '\0' && is_cont((unsigned char)s[i])) {
            i++;
        }
        n--;
    }
    return i;
}

static const char *safe(const char *s)
{
    return s != NULL ? s : "";
}

int gm_string_length(const char *s)
{
    int n = 0;

    for (s = safe(s); *s != '\0'; ++s) {
        n += !is_cont((unsigned char)*s);
    }
    return n;
}

/* L544: 1-based; indices below 1 give the first character, past the end "". */
const char *gm_string_char_at(const char *s, int32_t index)
{
    size_t start, end;

    s = safe(s);
    if (*s == '\0' || (int64_t)gm_string_length(s) <= (int64_t)index - 1) {
        return "";
    }
    start = advance(s, index > 1 ? index - 1 : 0);
    end = start + advance(s + start, 1);
    return gm_heap_str_n(s + start, end - start);
}

/* L781 */
const char *gm_string_delete(const char *s, int32_t index, int32_t count)
{
    size_t start, end, len;
    gm_value_t out;
    char *buf;

    s = safe(s);
    if (count <= 0 || index <= 0) {
        return s;
    }
    start = advance(s, index - 1);
    end = start + advance(s + start, count);
    len = strlen(s);
    buf = gm_heap_string_alloc(len - (end - start), &out);
    if (buf == NULL) {
        return "";
    }
    memcpy(buf, s, start);
    memcpy(buf + start, s + end, len - end);
    return out.str;
}

/* L828: positions before 1 insert at the front, past the end append. */
const char *gm_string_insert(const char *substr, const char *s, int32_t index)
{
    size_t start, len, sub;
    gm_value_t out;
    char *buf;

    substr = safe(substr);
    s = safe(s);
    start = advance(s, index > 1 ? index - 1 : 0);
    len = strlen(s);
    sub = strlen(substr);
    buf = gm_heap_string_alloc(len + sub, &out);
    if (buf == NULL) {
        return "";
    }
    memcpy(buf, s, start);
    memcpy(buf + start, substr, sub);
    memcpy(buf + start + sub, s + start, len - start);
    return out.str;
}

/* L439: indexOf + 1 in characters; an empty substring is found at 1. */
int gm_string_pos(const char *substr, const char *s)
{
    const char *hit;
    int pos = 1;

    s = safe(s);
    hit = strstr(s, safe(substr));
    if (hit == NULL) {
        return 0;
    }
    for (; s < hit; ++s) {
        pos += !is_cont((unsigned char)*s);
    }
    return pos;
}

static const char *map_case(const char *s, char lo, char hi, int delta)
{
    size_t i, len;
    gm_value_t out;
    char *buf;

    s = safe(s);
    len = strlen(s);
    buf = gm_heap_string_alloc(len, &out);
    if (buf == NULL) {
        return "";
    }
    for (i = 0; i < len; ++i) {
        buf[i] = (s[i] >= lo && s[i] <= hi) ? (char)(s[i] + delta) : s[i];
    }
    return out.str;
}

const char *gm_string_lower(const char *s)
{
    return map_case(s, 'A', 'Z', 'a' - 'A');
}

const char *gm_string_upper(const char *s)
{
    return map_case(s, 'a', 'z', 'A' - 'a');
}

/* L956 */
const char *gm_string_hash_to_newline(const char *s)
{
    size_t i, n = 0, len, size;
    gm_value_t out;
    char *buf;

    s = safe(s);
    if (strchr(s, '#') == NULL) {
        return s;
    }
    len = size = strlen(s);
    for (i = 0; i < len; ++i) {
        if (s[i] == '#') {
            size = (i > 0 && s[i - 1] == '\\') ? size - 1 : size + 1;
        }
    }
    buf = gm_heap_string_alloc(size, &out);
    if (buf == NULL) {
        return "";
    }
    for (i = 0; i < len; ++i) {
        if (s[i] == '#' && (i == 0 || s[i - 1] != '\\')) {
            buf[n++] = '\r';
            buf[n++] = '\n';
        } else if (s[i] == '#') {
            buf[n - 1] = '#';   /* "\#": the backslash becomes the hash */
        } else {
            buf[n++] = s[i];
        }
    }
    return out.str;
}

/* ord (L76): the first code point. Malformed UTF-8 (only possible with raw file
 * bytes; JS strings are always well formed) yields the first byte's value. */
float gm_string_ord(const char *s)
{
    const unsigned char *u = (const unsigned char *)safe(s);
    uint32_t cp;
    int extra, i;

    if (u[0] < 0x80u) {
        return (float)u[0];
    }
    if ((u[0] & 0xE0u) == 0xC0u) {
        cp = u[0] & 0x1Fu;
        extra = 1;
    } else if ((u[0] & 0xF0u) == 0xE0u) {
        cp = u[0] & 0x0Fu;
        extra = 2;
    } else if ((u[0] & 0xF8u) == 0xF0u) {
        cp = u[0] & 0x07u;
        extra = 3;
    } else {
        return (float)u[0];
    }
    for (i = 1; i <= extra; ++i) {
        if (!is_cont(u[i])) {
            return (float)u[0];
        }
        cp = (cp << 6) | (u[i] & 0x3Fu);
    }
    return (float)cp;
}
