/*
 * gm_value - tagged GML value for dynamically typed storage.
 *
 * Statically real data (the vast majority of Spelunky's variables) stays in
 * plain doubles; gm_value_t is used for strings, arrays, `undefined` and
 * values whose type the transpiler cannot pin down (slice 7 value model).
 *
 * Strings: `str` is either borrowed (string literals, ds_map storage...) or
 * owned by gm_heap. gm_heap decides ownership by address, so a heap string
 * boxed with gm_value_string() is still relocated by the collector; `ref ==
 * GM_VALUE_REF_HEAP` is only informational. Only heap-owned strings and
 * static literals may be stored in long-lived GML variables.
 *
 * Arrays: `ref` is a gm_heap array handle (GML arrays are reference types).
 */
#ifndef GM_VALUE_H
#define GM_VALUE_H

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum gm_value_kind {
    GM_VALUE_UNDEFINED = 0, /* zero-initialised storage reads as undefined */
    GM_VALUE_REAL,
    GM_VALUE_STRING,
    GM_VALUE_ARRAY
} gm_value_kind_t;

/* `ref` for strings that live in the gm_heap string space. */
#define GM_VALUE_REF_HEAP 1

typedef struct gm_value {
    gm_value_kind_t kind;
    int32_t ref;     /* ARRAY: array handle; STRING: GM_VALUE_REF_HEAP if owned */
    double real;     /* valid when kind == GM_VALUE_REAL */
    const char *str; /* valid when kind == GM_VALUE_STRING */
} gm_value_t;

/* Static initialiser equivalent to gm_value_undefined(). */
#define GM_VALUE_UNDEFINED_INIT { GM_VALUE_UNDEFINED, 0, 0.0, NULL }

static inline gm_value_t gm_value_undefined(void)
{
    gm_value_t v;
    v.kind = GM_VALUE_UNDEFINED;
    v.ref = 0;
    v.real = 0.0;
    v.str = NULL;
    return v;
}

static inline gm_value_t gm_value_real(double r)
{
    gm_value_t v;
    v.kind = GM_VALUE_REAL;
    v.ref = 0;
    v.real = r;
    v.str = NULL;
    return v;
}

/* Borrowed string. A NULL string is stored as the empty string. */
static inline gm_value_t gm_value_string(const char *s)
{
    gm_value_t v;
    v.kind = GM_VALUE_STRING;
    v.ref = 0;
    v.real = 0.0;
    v.str = s != NULL ? s : "";
    return v;
}

/* ---- GML value semantics (GameMaker-HTML5 yyTypes.js / Function_Maths.js) ----- */

/* g_GMLMathEpsilon (Function_Maths.js L19). math_set_epsilon is not supported. */
#define GM_EPSILON 1e-5

/* yyCompareVal's "unordered" result (NaN involved, or undefined operand). */
#define GM_CMP_UNORDERED (-2)

/* yyCompareVal for two numbers: 0 when |a - b| <= GM_EPSILON, else -1 / 1;
 * GM_CMP_UNORDERED for NaN (equal infinities compare equal). */
static inline int gm_compare_real(double a, double b)
{
    double f = a - b;

    if (isnan(f)) {
        return (!isnan(a) && !isnan(b)) ? 0 : GM_CMP_UNORDERED;
    }
    return fabs(f) <= GM_EPSILON ? 0 : (f < 0.0 ? -1 : 1);
}

/* yyCompareVal for arbitrary values (Function_Maths.js L1654): numbers with
 * epsilon, strings by code unit, undefined == undefined, arrays by identity,
 * otherwise both sides converted to numbers where possible. Returns -1, 0,
 * 1 or GM_CMP_UNORDERED. Array comparisons return the length difference
 * sign, as the runner does. */
int gm_value_compare(gm_value_t a, gm_value_t b);

/* yyGetReal (yyTypes.js L173): strings are trimmed and must start with a
 * number (g_NumberRE prefix match); undefined, arrays and non-numeric
 * strings are runner errors and yield 0 here. */
double gm_value_to_real(gm_value_t v);

/* yyGetBool (yyTypes.js L318): reals are true above 0.5; strings "true" /
 * "false" or a numeric prefix; undefined is false. */
bool gm_value_to_bool(gm_value_t v);

/* yyGetString / string() (yyTypes.js L426): integers in int32 range print
 * without decimals, other finite reals with toFixed(2); strings are returned
 * as is; arrays print as "[ a,b ]". New strings live in gm_heap. */
gm_value_t gm_value_to_string(gm_value_t v);

/* Formats a real exactly like yyGetString into buf (at least 64 bytes). */
void gm_real_format(double x, char *buf, size_t size);

/* real() on a string (Function_String.js L106): JS parseFloat, or parseInt
 * for a "0x" prefix. Unparseable strings are runner errors; *ok is set false
 * and 0 is returned. `ok` may be NULL. */
double gm_string_parse_real(const char *s, bool *ok);

/* Length of the g_NumberRE match at the start of s (0 = no match). */
size_t gm_number_prefix(const char *s);

#endif /* GM_VALUE_H */
