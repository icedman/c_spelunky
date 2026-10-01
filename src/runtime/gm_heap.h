/*
 * gm_heap - static storage for GML strings and arrays, with a copying
 * collector that runs only at safe points.
 *
 * Why: GML creates strings (concatenation, string(), file reads...) and
 * arrays at will. The port must never call malloc during frame execution, so
 * both live in two fixed semispaces of GM_HEAP_BYTES each plus a fixed pool
 * of GM_ARRAY_MAX array headers.
 *
 * Allocation is a bump pointer in the current semispace. gm_heap_collect()
 * copies every string/array block reachable from the roots into the other
 * semispace (Cheney-style, arrays traced through their elements), rewrites
 * the root values in place and frees unreachable array headers.
 *
 * String ownership is decided by address: a string pointer that lies in the
 * current semispace (and points at the start of a string block) is heap
 * owned, anything else (literals, runtime buffers) is borrowed and never
 * touched by the collector. So a heap string may be held both as a bare
 * `const char *` (the transpiler's static string slots) and inside a
 * gm_value_t, and relocation works for either.
 *
 * Safe points: values held in C locals of running generated code are not
 * roots, so the game loop must only collect between events (when no GML code
 * is on the stack). Everything allocated between two collections must fit in
 * one semispace; when it does not, allocation fails soft (strings become "",
 * array writes are dropped) and gm_heap_failures() counts it.
 *
 * Arrays follow the HTML5 runner's JS arrays: reference semantics, writes past
 * the end grow the array and skipped slots read as undefined
 * (yyVariable.js array_set). Copy-on-write ownership (__yy_gml_array_check)
 * is not modelled.
 */
#ifndef GM_HEAP_H
#define GM_HEAP_H

#include "gm_value.h"

#include <stdbool.h>
#include <stddef.h>

#ifndef GM_HEAP_BYTES
#define GM_HEAP_BYTES (2u * 1024u * 1024u) /* per semispace */
#endif

#ifndef GM_ARRAY_MAX
#define GM_ARRAY_MAX 4096 /* live arrays */
#endif

#define GM_ARRAY_MAX_LENGTH 65536 /* elements per array */

/* Forgets every string and array (game restart, test isolation). */
void gm_heap_reset(void);

/* ---- Strings ------------------------------------------------------------------ */

/* Heap copy of `s` (NULL copies ""). */
gm_value_t gm_heap_string(const char *s);
gm_value_t gm_heap_string_n(const char *s, size_t n);
gm_value_t gm_heap_concat(const char *a, const char *b);

/* Reserves a writable string of `len` characters (NUL-terminated at [len]);
 * *out receives the value. Returns NULL (and *out = "") when out of space. */
char *gm_heap_string_alloc(size_t len, gm_value_t *out);

/* True if `v` is a heap-owned string. */
bool gm_heap_owns(gm_value_t v);

/* ---- Strings as bare pointers (static `s` slots of generated code) ------------ */

/* Heap copies; "" (a literal) when out of space. Never NULL. */
const char *gm_heap_str(const char *s);
const char *gm_heap_str_n(const char *s, size_t n);
const char *gm_heap_str_concat(const char *a, const char *b);

/* True if `s` points at a string in the current semispace. */
bool gm_heap_owns_str(const char *s);

/* ---- Arrays --------------------------------------------------------------------- */

/* New array of `length` undefined elements (undefined value on failure). */
gm_value_t gm_array_new(int length);

bool gm_array_valid(gm_value_t a);
int gm_array_length(gm_value_t a); /* 0 for non-arrays */

/* Element read; undefined for non-arrays and out-of-range indices (the
 * runner raises an error there). */
gm_value_t gm_array_get(gm_value_t a, int index);

/* Pointer to element `index` of the array stored in *slot, for writing. A
 * non-array *slot is replaced by a new array first; the array grows to
 * index + 1. Returns a pointer to a scratch value (writes are discarded) for
 * negative / too-large indices or when out of space. The pointer is valid
 * until the next allocation or collection. */
gm_value_t *gm_array_ref(gm_value_t *slot, int index);

void gm_array_set(gm_value_t *slot, int index, gm_value_t v);

/* ---- Collection ----------------------------------------------------------------- */

typedef void (*gm_heap_visit_fn)(gm_value_t *v);
/* Calls `visit` on every root value (each at most once is not required), and
 * gm_heap_visit_str on every root string slot. */
typedef void (*gm_heap_roots_fn)(gm_heap_visit_fn visit);

/* Root visitor for a `const char *` slot: relocates *p if it is heap owned.
 * Only meaningful inside a gm_heap_roots_fn during gm_heap_collect (a no-op
 * elsewhere). NULL and borrowed pointers are left unchanged. */
void gm_heap_visit_str(const char **p);

/* Copies everything reachable from `roots` into the other semispace. Must be
 * called at a safe point (see above). */
void gm_heap_collect(gm_heap_roots_fn roots);

/* ---- Diagnostics ---------------------------------------------------------------- */

size_t gm_heap_used(void);       /* bytes in use in the current semispace */
int gm_heap_array_count(void);   /* live array headers */
long gm_heap_failures(void);     /* failed allocations since reset */
unsigned gm_heap_collections(void);

#endif /* GM_HEAP_H */
