/*
 * gm_heap - see gm_heap.h.
 *
 * Layout: each semispace is an array of 8-byte cells (a union, so blocks are
 * suitably aligned for gm_value_t). A block starts with a one-cell header:
 *
 *   string block: { kind = STR,  size = strlen, fwd } + chars + NUL
 *   items block:  { kind = ITEMS, size = capacity, fwd } + capacity values
 *
 * Once a block has been copied during a collection its header kind becomes
 * MOVED and its first payload cell holds the new cell offset. String values
 * point at the first character, so the header is the cell just before `str`.
 * Heap ownership of a string is decided by address (see gm_heap.h).
 */
#include "gm_heap.h"

#include <stdint.h>
#include <string.h>

typedef union heap_cell {
    double d;
    void *p;
    struct {
        uint32_t kind; /* BLOCK_* */
        uint32_t size; /* string length, or item capacity */
    } h;
} heap_cell_t;

typedef char gm_heap_cell_is_8_bytes[(sizeof(heap_cell_t) == 8) ? 1 : -1];
typedef char gm_heap_value_fits_cells[(sizeof(gm_value_t) % sizeof(heap_cell_t) == 0) ? 1 : -1];

enum { BLOCK_STR = 0x53545231u, BLOCK_ITEMS = 0x49544d31u, BLOCK_MOVED = 0x4d4f5644u };

#define HEAP_CELLS (GM_HEAP_BYTES / sizeof(heap_cell_t))
#define VALUE_CELLS (sizeof(gm_value_t) / sizeof(heap_cell_t))

static heap_cell_t s_space[2][HEAP_CELLS];
static int s_cur;          /* active semispace */
static size_t s_top;       /* cells used in the active semispace */
static long s_failures;
static unsigned s_collections;
static bool s_collecting;  /* inside gm_heap_collect */

typedef struct array_hdr {
    gm_value_t *items; /* NULL while empty */
    int32_t length;
    int32_t capacity;
    bool in_use;
    bool marked;
} array_hdr_t;

static array_hdr_t s_arrays[GM_ARRAY_MAX];
static int s_array_count;
static int s_array_hint; /* next slot to probe when allocating */

static gm_value_t s_scratch; /* sink for failed array writes */

/* ------------------------------------------------------------------ helpers */

static size_t cells_for_bytes(size_t bytes)
{
    return (bytes + sizeof(heap_cell_t) - 1) / sizeof(heap_cell_t);
}

/* Allocates 1 header cell + payload cells in the active space. */
static heap_cell_t *alloc_block(uint32_t kind, uint32_t size, size_t payload_cells)
{
    heap_cell_t *hdr;

    if (payload_cells >= HEAP_CELLS || HEAP_CELLS - s_top < payload_cells + 1) {
        s_failures++;
        return NULL;
    }
    hdr = &s_space[s_cur][s_top];
    s_top += payload_cells + 1;
    hdr->h.kind = kind;
    hdr->h.size = size;
    return hdr;
}

static heap_cell_t *string_header(const char *str)
{
    return (heap_cell_t *)(void *)str - 1;
}

/* Does `s` point at the characters of a string block of the active space?
 * (During a collection that is the from-space; blocks may already be MOVED.)
 * Addresses are compared as integers: the pointer may belong to an
 * unrelated object. */
static bool owned_str(const char *s)
{
    uintptr_t a = (uintptr_t)(const void *)s;
    uintptr_t lo = (uintptr_t)(const void *)&s_space[s_cur][0];
    uintptr_t hi = (uintptr_t)(const void *)&s_space[s_cur][s_top];
    uint32_t kind;

    if (s == NULL || a < lo + sizeof(heap_cell_t) || a >= hi || (a - lo) % sizeof(heap_cell_t) != 0) {
        return false;
    }
    kind = string_header(s)->h.kind;
    return kind == BLOCK_STR || (s_collecting && kind == BLOCK_MOVED);
}

static gm_value_t heap_string_value(const char *chars)
{
    gm_value_t v = gm_value_string(chars);
    v.ref = GM_VALUE_REF_HEAP;
    return v;
}

void gm_heap_reset(void)
{
    s_cur = 0;
    s_top = 0;
    s_failures = 0;
    s_collections = 0;
    s_collecting = false;
    memset(s_arrays, 0, sizeof(s_arrays));
    s_array_count = 0;
    s_array_hint = 0;
    s_scratch = gm_value_undefined();
}

/* ------------------------------------------------------------------ strings */

char *gm_heap_string_alloc(size_t len, gm_value_t *out)
{
    heap_cell_t *hdr;
    char *chars;

    if (len > (size_t)UINT32_MAX - 1u) {
        s_failures++;
        hdr = NULL;
    } else {
        hdr = alloc_block(BLOCK_STR, (uint32_t)len, cells_for_bytes(len + 1));
    }
    if (hdr == NULL) {
        *out = gm_value_string("");
        return NULL;
    }
    chars = (char *)(void *)(hdr + 1);
    chars[len] = '\0';
    *out = heap_string_value(chars);
    return chars;
}

gm_value_t gm_heap_string_n(const char *s, size_t n)
{
    gm_value_t v;
    char *buf = gm_heap_string_alloc(n, &v);

    if (buf != NULL && n > 0) {
        memcpy(buf, s, n);
    }
    return v;
}

gm_value_t gm_heap_string(const char *s)
{
    if (s == NULL) {
        s = "";
    }
    return gm_heap_string_n(s, strlen(s));
}

gm_value_t gm_heap_concat(const char *a, const char *b)
{
    size_t na, nb;
    gm_value_t v;
    char *buf;

    a = a != NULL ? a : "";
    b = b != NULL ? b : "";
    na = strlen(a);
    nb = strlen(b);
    buf = gm_heap_string_alloc(na + nb, &v);
    if (buf != NULL) {
        memcpy(buf, a, na);
        memcpy(buf + na, b, nb);
    }
    return v;
}

bool gm_heap_owns(gm_value_t v)
{
    return v.kind == GM_VALUE_STRING && owned_str(v.str);
}

bool gm_heap_owns_str(const char *s)
{
    return owned_str(s);
}

const char *gm_heap_str_n(const char *s, size_t n)
{
    return gm_heap_string_n(s, n).str;
}

const char *gm_heap_str(const char *s)
{
    return gm_heap_string(s).str;
}

const char *gm_heap_str_concat(const char *a, const char *b)
{
    return gm_heap_concat(a, b).str;
}

/* ------------------------------------------------------------------ arrays */

static array_hdr_t *array_of(gm_value_t a)
{
    if (a.kind != GM_VALUE_ARRAY || a.ref < 0 || a.ref >= GM_ARRAY_MAX || !s_arrays[a.ref].in_use) {
        return NULL;
    }
    return &s_arrays[a.ref];
}

static gm_value_t *alloc_items(int32_t capacity)
{
    heap_cell_t *hdr = alloc_block(BLOCK_ITEMS, (uint32_t)capacity, (size_t)capacity * VALUE_CELLS);
    gm_value_t *items;

    if (hdr == NULL) {
        return NULL;
    }
    items = (gm_value_t *)(void *)(hdr + 1);
    memset(items, 0, (size_t)capacity * sizeof(gm_value_t)); /* all undefined */
    return items;
}

static int alloc_header(void)
{
    int i;

    if (s_array_count >= GM_ARRAY_MAX) {
        s_failures++;
        return -1;
    }
    for (i = 0; i < GM_ARRAY_MAX; ++i) {
        int slot = (s_array_hint + i) % GM_ARRAY_MAX;
        if (!s_arrays[slot].in_use) {
            memset(&s_arrays[slot], 0, sizeof(s_arrays[slot]));
            s_arrays[slot].in_use = true;
            s_array_count++;
            s_array_hint = (slot + 1) % GM_ARRAY_MAX;
            return slot;
        }
    }
    return -1; /* unreachable: count says a slot is free */
}

gm_value_t gm_array_new(int length)
{
    gm_value_t v = gm_value_undefined();
    int slot;

    if (length < 0 || length > GM_ARRAY_MAX_LENGTH) {
        s_failures++;
        return v;
    }
    slot = alloc_header();
    if (slot < 0) {
        return v;
    }
    if (length > 0) {
        s_arrays[slot].items = alloc_items(length);
        if (s_arrays[slot].items == NULL) {
            s_arrays[slot].in_use = false;
            s_array_count--;
            return v;
        }
        s_arrays[slot].capacity = length;
        s_arrays[slot].length = length;
    }
    v.kind = GM_VALUE_ARRAY;
    v.ref = slot;
    return v;
}

bool gm_array_valid(gm_value_t a)
{
    return array_of(a) != NULL;
}

int gm_array_length(gm_value_t a)
{
    const array_hdr_t *arr = array_of(a);
    return arr != NULL ? arr->length : 0;
}

gm_value_t gm_array_get(gm_value_t a, int index)
{
    const array_hdr_t *arr = array_of(a);

    if (arr == NULL || index < 0 || index >= arr->length) {
        return gm_value_undefined();
    }
    return arr->items[index];
}

static gm_value_t *scratch(void)
{
    s_scratch = gm_value_undefined();
    return &s_scratch;
}

gm_value_t *gm_array_ref(gm_value_t *slot, int index)
{
    array_hdr_t *arr;

    if (slot == NULL || index < 0 || index >= GM_ARRAY_MAX_LENGTH) {
        s_failures++;
        return scratch();
    }
    arr = array_of(*slot);
    if (arr == NULL) {
        gm_value_t fresh = gm_array_new(0);
        if (fresh.kind != GM_VALUE_ARRAY) {
            return scratch();
        }
        *slot = fresh;
        arr = &s_arrays[fresh.ref];
    }
    if (index >= arr->capacity) {
        int32_t cap = arr->capacity < 4 ? 4 : arr->capacity;
        gm_value_t *items;

        while (cap <= index) {
            cap = cap > GM_ARRAY_MAX_LENGTH / 2 ? GM_ARRAY_MAX_LENGTH : cap * 2;
        }
        items = alloc_items(cap);
        if (items == NULL) {
            return scratch();
        }
        if (arr->length > 0) {
            memcpy(items, arr->items, (size_t)arr->length * sizeof(gm_value_t));
        }
        arr->items = items;
        arr->capacity = cap;
    }
    if (index >= arr->length) {
        arr->length = index + 1;
    }
    return &arr->items[index];
}

void gm_array_set(gm_value_t *slot, int index, gm_value_t v)
{
    *gm_array_ref(slot, index) = v;
}

/* ------------------------------------------------------------------ collection */

static int s_work[GM_ARRAY_MAX];
static int s_work_count;
static int s_to;           /* destination semispace during a collection */
static size_t s_to_top;

static void *copy_block(heap_cell_t *hdr, size_t payload_cells)
{
    heap_cell_t *dst;
    size_t offset;

    if (hdr->h.kind == BLOCK_MOVED) {
        return &s_space[s_to][hdr[1].h.size] + 1;
    }
    /* The live set always fits: it was allocated in a space of equal size. */
    offset = s_to_top;
    dst = &s_space[s_to][offset];
    memcpy(dst, hdr, (payload_cells + 1) * sizeof(heap_cell_t));
    s_to_top += payload_cells + 1;
    /* Leave a forwarding record: header kind MOVED, first payload cell keeps
     * the destination offset (payload of the old copy is dead). */
    hdr->h.kind = BLOCK_MOVED;
    if (payload_cells > 0) {
        hdr[1].h.size = (uint32_t)offset;
    }
    return dst + 1;
}

/* Moving a block keeps its size field, so this works for MOVED blocks too. */
static const char *relocate_str(const char *s)
{
    heap_cell_t *hdr = string_header(s);
    return (const char *)copy_block(hdr, cells_for_bytes((size_t)hdr->h.size + 1));
}

void gm_heap_visit_str(const char **p)
{
    if (s_collecting && p != NULL && owned_str(*p)) {
        *p = relocate_str(*p);
    }
}

static void visit_value(gm_value_t *v)
{
    if (v->kind == GM_VALUE_STRING) {
        if (owned_str(v->str)) {
            v->str = relocate_str(v->str);
            v->ref = GM_VALUE_REF_HEAP;
        }
    } else if (v->kind == GM_VALUE_ARRAY) {
        if (v->ref >= 0 && v->ref < GM_ARRAY_MAX && s_arrays[v->ref].in_use) {
            if (!s_arrays[v->ref].marked) {
                s_arrays[v->ref].marked = true;
                s_work[s_work_count++] = v->ref;
            }
        } else {
            *v = gm_value_undefined(); /* dangling handle */
        }
    }
}

void gm_heap_collect(gm_heap_roots_fn roots)
{
    int i;

    s_to = 1 - s_cur;
    s_to_top = 0;
    s_work_count = 0;
    s_collecting = true;
    for (i = 0; i < GM_ARRAY_MAX; ++i) {
        s_arrays[i].marked = false;
    }
    if (roots != NULL) {
        roots(visit_value);
    }
    while (s_work_count > 0) {
        array_hdr_t *arr = &s_arrays[s_work[--s_work_count]];
        int32_t n;

        if (arr->items == NULL) {
            continue;
        }
        arr->items = (gm_value_t *)copy_block((heap_cell_t *)(void *)arr->items - 1,
                                              (size_t)arr->capacity * VALUE_CELLS);
        for (n = 0; n < arr->length; ++n) {
            visit_value(&arr->items[n]);
        }
    }
    for (i = 0; i < GM_ARRAY_MAX; ++i) {
        if (s_arrays[i].in_use && !s_arrays[i].marked) {
            memset(&s_arrays[i], 0, sizeof(s_arrays[i]));
            s_array_count--;
        }
    }
    s_collecting = false;
    s_cur = s_to;
    s_top = s_to_top;
    s_collections++;
}

/* ------------------------------------------------------------------ diagnostics */

size_t gm_heap_used(void)
{
    return s_top * sizeof(heap_cell_t);
}

int gm_heap_array_count(void)
{
    return s_array_count;
}

long gm_heap_failures(void)
{
    return s_failures;
}

unsigned gm_heap_collections(void)
{
    return s_collections;
}
