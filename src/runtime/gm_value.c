/*
 * gm_value - GML value conversions and comparison, ported from the
 * GameMaker-HTML5 runner (yyTypes.js yyGetReal / yyGetBool / yyGetString,
 * Function_Maths.js yyCompareVal / g_NumberRE, Function_String.js real()).
 *
 * Runner errors (yyError, which aborts the game in HTML5) become soft
 * results here: 0, false or "". Numeric text is parsed with strtod on an
 * already validated, bounded prefix, so the C locale's decimal point is
 * assumed (the port never calls setlocale).
 */
#include "gm_value.h"

#include "gm_heap.h"
#include "gm_math.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Longest g_NumberRE match: sign + 30 digits + '.' + 30 digits + e+29. */
#define NUMBER_MAX 72

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

/* JS String.prototype.trim / StrWhiteSpaceChar (ASCII subset). */
static bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

static size_t count_digits(const char *s, size_t max)
{
    size_t n = 0;

    while (n < max && is_digit(s[n])) {
        n++;
    }
    return n;
}

size_t gm_number_prefix(const char *s)
{
    size_t i = 0, end = 0, n;

    if (s == NULL) {
        return 0;
    }
    if (s[i] == '+' || s[i] == '-') {
        i++;
    }
    /* (?:[0-9]{0,30}\.)?[0-9]{1,30} */
    n = count_digits(s + i, 31);
    if (n <= 30 && s[i + n] == '.') {
        size_t frac = count_digits(s + i + n + 1, 30);
        if (frac > 0) {
            end = i + n + 1 + frac;
        }
    }
    if (end == 0) {
        n = count_digits(s + i, 30);
        if (n == 0) {
            return 0;
        }
        end = i + n;
    }
    /* (?:[Ee][-+]?[1-2]?[0-9])? */
    if (s[end] == 'e' || s[end] == 'E') {
        size_t p = end + 1;
        if (s[p] == '+' || s[p] == '-') {
            p++;
        }
        if ((s[p] == '1' || s[p] == '2') && is_digit(s[p + 1])) {
            end = p + 2;
        } else if (is_digit(s[p])) {
            end = p + 1;
        }
    }
    return end;
}

/* Number(<prefix of s of length n>). */
static float number_of(const char *s, size_t n)
{
    char buf[NUMBER_MAX + 1];

    if (n > NUMBER_MAX) {
        n = NUMBER_MAX;
    }
    memcpy(buf, s, n);
    buf[n] = '\0';
    return strtof(buf, NULL);
}

static const char *skip_space(const char *s)
{
    while (is_space(*s)) {
        s++;
    }
    return s;
}

/* yyGetReal-style string conversion: trimmed, g_NumberRE prefix. */
static bool string_number(const char *s, bool trim, float *out)
{
    size_t n;

    if (s == NULL) {
        return false;
    }
    if (trim) {
        s = skip_space(s);
    }
    n = gm_number_prefix(s);
    if (n == 0) {
        return false;
    }
    *out = number_of(s, n);
    return true;
}

float gm_value_to_real(gm_value_t v)
{
    float d;

    switch (v.kind) {
    case GM_VALUE_REAL:
        return v.real;
    case GM_VALUE_STRING:
        return string_number(v.str, true, &d) ? d : 0.0f;
    case GM_VALUE_UNDEFINED:
    case GM_VALUE_ARRAY:
    default:
        return 0.0f;
    }
}

bool gm_value_to_bool(gm_value_t v)
{
    float d;

    switch (v.kind) {
    case GM_VALUE_REAL:
        return v.real > 0.5f;
    case GM_VALUE_STRING:
        if (v.str == NULL) {
            return false;
        }
        if (strcmp(v.str, "true") == 0) {
            return true;
        }
        if (strcmp(v.str, "false") == 0) {
            return false;
        }
        /* yyGetBool matches the untrimmed string. */
        return string_number(v.str, false, &d) && d > 0.5f;
    case GM_VALUE_ARRAY:     /* runner error */
    case GM_VALUE_UNDEFINED:
    default:
        return false;
    }
}

int gm_value_compare(gm_value_t a, gm_value_t b)
{
    bool ret_set = false, ret_nan = false, a_num = false, b_num = false;
    int ret = 0;
    float na = 0.0f, nb = 0.0f;

    if (a.kind == GM_VALUE_REAL && b.kind == GM_VALUE_REAL) {
        return gm_compare_real(a.real, b.real);
    }
    if (a.kind == GM_VALUE_STRING && b.kind == GM_VALUE_STRING) {
        int c = strcmp(a.str != NULL ? a.str : "", b.str != NULL ? b.str : "");
        return c == 0 ? 0 : (c > 0 ? 1 : -1);
    }
    if (a.kind == GM_VALUE_UNDEFINED && b.kind == GM_VALUE_UNDEFINED) {
        return 0;
    }
    if (a.kind == GM_VALUE_ARRAY && b.kind == GM_VALUE_ARRAY) {
        int d = gm_array_length(a) - gm_array_length(b);
        if (d != 0) {
            return d < 0 ? -1 : 1;
        }
        return a.ref == b.ref ? 0 : 1;
    }
    if ((a.kind == GM_VALUE_UNDEFINED && b.kind == GM_VALUE_ARRAY) ||
        (b.kind == GM_VALUE_UNDEFINED && a.kind == GM_VALUE_ARRAY)) {
        return 1;
    }

    /* Mixed kinds: convert each side to a number where possible
     * (Function_Maths.js L1737-1818). */
    switch (a.kind) {
    case GM_VALUE_REAL:
        na = a.real;
        a_num = true;
        break;
    case GM_VALUE_STRING:
        if (string_number(a.str, true, &na)) {
            a_num = true;
        } else {
            ret_set = true;
            ret_nan = true;
        }
        break;
    case GM_VALUE_ARRAY:
        ret_set = true;
        ret = 1;
        break;
    case GM_VALUE_UNDEFINED:
    default:
        ret_set = true;
        ret = GM_CMP_UNORDERED;
        break;
    }
    switch (b.kind) {
    case GM_VALUE_REAL:
        nb = b.real;
        b_num = true;
        break;
    case GM_VALUE_STRING:
        if (string_number(b.str, true, &nb)) {
            b_num = true;
        } else {
            ret_set = true;
            ret_nan = true;
        }
        break;
    case GM_VALUE_ARRAY:
        break; /* runner error only */
    case GM_VALUE_UNDEFINED:
    default:
        ret_set = true;
        ret_nan = false;
        ret = GM_CMP_UNORDERED;
        break;
    }
    if (!ret_set) {
        return (a_num && b_num) ? gm_compare_real(na, nb) : 1;
    }
    if (ret_nan || ret != GM_CMP_UNORDERED) {
        return a_num ? -1 : 1;
    }
    return ret;
}

/* ------------------------------------------------------------------ strings */

void gm_real_format(float x, char *buf, size_t size)
{
    if (isnan(x)) {
        snprintf(buf, size, "NaN");
    } else if (isinf(x)) {
        snprintf(buf, size, x < 0.0f ? "-inf" : "inf");
    } else if ((float)gm_to_int32(x) == x) {
        /* (~~v) == v: integer in int32 range; -0 prints as "0". */
        snprintf(buf, size, "%ld", (long)gm_to_int32(x));
    } else if (fabsf(x) >= 1e21f) {
        snprintf(buf, size, "%.17g", (double)x); /* JS falls back to ToString */
    } else {
        /* toFixed(2): nearest multiple of 0.01, exact ties away from zero.
         * printf rounds the exact binary value correctly but breaks exact
         * ties to even; ties only exist for multiples of 1/8. */
        float ax = fabsf(x);
        /* ax == k/8 exactly (ax * 8 is exact in float); ax * 100 == 12.5k is
         * a tie iff k is odd, rounded away to (25k + 1) / 2 hundredths.
         * Integer math because ax * 100 is not exact in float. */
        long long k = (ax < 1e15f && fmodf(ax * 8.0f, 1.0f) == 0.0f) ? (long long)(ax * 8.0f) : 0;
        if (k & 1) {
            long long n = (25 * k + 1) / 2;
            snprintf(buf, size, "%s%lld.%02lld", x < 0.0f ? "-" : "", n / 100, n % 100);
        } else {
            snprintf(buf, size, "%.2f", (double)x);
        }
    }
}

typedef struct str_builder {
    char buf[4096];
    size_t len;
} str_builder_t;

static void sb_put(str_builder_t *sb, const char *s)
{
    size_t n = strlen(s);

    if (n > sizeof(sb->buf) - 1 - sb->len) {
        n = sizeof(sb->buf) - 1 - sb->len; /* truncate */
    }
    memcpy(sb->buf + sb->len, s, n);
    sb->len += n;
    sb->buf[sb->len] = '\0';
}

#define VISIT_MAX 16

static void format_value(str_builder_t *sb, gm_value_t v, int *visited, int depth, bool quote)
{
    char num[64];
    int i, n;

    switch (v.kind) {
    case GM_VALUE_REAL:
        gm_real_format(v.real, num, sizeof(num));
        sb_put(sb, num);
        return;
    case GM_VALUE_STRING:
        if (quote) {
            sb_put(sb, "\"");
        }
        sb_put(sb, v.str != NULL ? v.str : "");
        if (quote) {
            sb_put(sb, "\"");
        }
        return;
    case GM_VALUE_ARRAY:
        for (i = 0; i < depth; ++i) {
            if (visited[i] == v.ref) {
                sb_put(sb, "\"Warning: Recursive array found\"");
                return;
            }
        }
        if (depth >= VISIT_MAX) {
            sb_put(sb, "\"Warning: Recursive array found\"");
            return;
        }
        visited[depth] = v.ref;
        sb_put(sb, "[ ");
        n = gm_array_length(v);
        for (i = 0; i < n; ++i) {
            if (i != 0) {
                sb_put(sb, ",");
            }
            format_value(sb, gm_array_get(v, i), visited, depth + 1, true);
        }
        sb_put(sb, " ]");
        return;
    case GM_VALUE_UNDEFINED:
    default:
        sb_put(sb, "undefined");
        return;
    }
}

gm_value_t gm_value_to_string(gm_value_t v)
{
    static str_builder_t sb;
    int visited[VISIT_MAX];
    char num[64];

    switch (v.kind) {
    case GM_VALUE_STRING:
        return v.str != NULL ? v : gm_value_string("");
    case GM_VALUE_UNDEFINED:
        return gm_value_string("undefined");
    case GM_VALUE_REAL:
        gm_real_format(v.real, num, sizeof(num));
        return gm_heap_string(num);
    case GM_VALUE_ARRAY:
    default:
        sb.len = 0;
        sb.buf[0] = '\0';
        format_value(&sb, v, visited, 0, false);
        return gm_heap_string_n(sb.buf, sb.len);
    }
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

float gm_string_parse_real(const char *s, bool *ok)
{
    const char *p, *start;
    size_t d1, d2;
    bool neg = false;

    if (ok != NULL) {
        *ok = false;
    }
    if (s == NULL) {
        return 0.0f;
    }
    if (s[0] == '0' && s[1] == 'x') {
        /* parseInt("0x...") */
        float r = 0.0f;
        int digits = 0, h;
        for (p = s + 2; (h = hex_digit(*p)) >= 0; ++p, ++digits) {
            r = r * 16.0f + (float)h;
        }
        if (digits == 0) {
            return 0.0f;
        }
        if (ok != NULL) {
            *ok = true;
        }
        return r;
    }
    /* parseFloat: StrDecimalLiteral prefix after leading whitespace. */
    start = p = skip_space(s);
    if (*p == '+' || *p == '-') {
        neg = *p == '-';
        p++;
    }
    if (strncmp(p, "Infinity", 8) == 0) {
        if (ok != NULL) {
            *ok = true;
        }
        return neg ? -HUGE_VALF : HUGE_VALF;
    }
    d1 = count_digits(p, (size_t)-1);
    p += d1;
    d2 = 0;
    if (*p == '.') {
        d2 = count_digits(p + 1, (size_t)-1);
        if (d1 + d2 > 0) {
            p += 1 + d2;
        }
    }
    if (d1 + d2 == 0) {
        return 0.0f; /* NaN in JS: runner error */
    }
    if (*p == 'e' || *p == 'E') {
        const char *q = p + 1;
        if (*q == '+' || *q == '-') {
            q++;
        }
        if (is_digit(*q)) {
            p = q + count_digits(q, (size_t)-1);
        }
    }
    {
        /* strtod on exactly the validated prefix: in place it would also
         * accept forms parseFloat rejects (" 0x10", "inf"). */
        char buf[512];
        size_t n = (size_t)(p - start);

        if (n >= sizeof(buf)) {
            return 0.0f; /* absurdly long literal: treat as unparseable */
        }
        memcpy(buf, start, n);
        buf[n] = '\0';
        if (ok != NULL) {
            *ok = true;
        }
        return strtof(buf, NULL);
    }
}
