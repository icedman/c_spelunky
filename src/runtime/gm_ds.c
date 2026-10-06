/*
 * gm_ds - see gm_ds.h.
 *
 * Each map keeps its entries in an insertion-ordered array (JS Map order)
 * plus an open-addressing, linear-probe hash index over that array. Keys and
 * string values are copied into a per-map byte arena; when the arena fills
 * up, dead strings (from replaced values) are reclaimed by compaction.
 */
#include "gm_ds.h"

#include <stdint.h>
#include <string.h>

#define HASH_SLOTS 256 /* power of two, >= 2 * GM_DS_MAP_MAX_ENTRIES */
#define SLOT_EMPTY (-1)

typedef struct map_entry {
    uint32_t hash;
    size_t key_off;
    gm_value_kind_t kind;
    float real;
    size_t str_off; /* valid when kind == GM_VALUE_STRING */
} map_entry_t;

typedef struct ds_map {
    bool in_use;
    int count;
    map_entry_t entries[GM_DS_MAP_MAX_ENTRIES];
    int16_t slots[HASH_SLOTS];
    size_t arena_used;
    char arena[GM_DS_MAP_STRING_BYTES];
} ds_map_t;

typedef char gm_ds_slots_fit_check[(GM_DS_MAP_MAX_ENTRIES <= 32767 &&
                                    HASH_SLOTS >= 2 * GM_DS_MAP_MAX_ENTRIES) ? 1 : -1];

static ds_map_t s_maps[GM_DS_MAP_MAX];

/* Staging copies of incoming strings: callers may pass pointers into the
 * map's own arena (e.g. a key from find_first), which compaction moves. */
static char s_stage_key[GM_DS_MAP_STRING_BYTES];
static char s_stage_val[GM_DS_MAP_STRING_BYTES];
static char s_compact_buf[GM_DS_MAP_STRING_BYTES];

static uint32_t hash_key(const char *key)
{
    uint32_t h = 2166136261u; /* FNV-1a */
    const unsigned char *p = (const unsigned char *)key;

    while (*p != '\0') {
        h ^= *p++;
        h *= 16777619u;
    }
    return h;
}

static ds_map_t *get_map(int id)
{
    if (id < 0 || id >= GM_DS_MAP_MAX || !s_maps[id].in_use) {
        return NULL;
    }
    return &s_maps[id];
}

static void map_reset_storage(ds_map_t *m)
{
    int i;

    m->count = 0;
    m->arena_used = 0;
    for (i = 0; i < HASH_SLOTS; ++i) {
        m->slots[i] = SLOT_EMPTY;
    }
}

static void index_insert(ds_map_t *m, int entry_index)
{
    unsigned slot = (unsigned)(m->entries[entry_index].hash & (HASH_SLOTS - 1u));

    while (m->slots[slot] != SLOT_EMPTY) {
        slot = (slot + 1u) & (HASH_SLOTS - 1u);
    }
    m->slots[slot] = (int16_t)entry_index;
}

static void index_rebuild(ds_map_t *m)
{
    int i;

    for (i = 0; i < HASH_SLOTS; ++i) {
        m->slots[i] = SLOT_EMPTY;
    }
    for (i = 0; i < m->count; ++i) {
        index_insert(m, i);
    }
}

static int find_entry(const ds_map_t *m, const char *key, uint32_t hash)
{
    unsigned slot = (unsigned)(hash & (HASH_SLOTS - 1u));

    while (m->slots[slot] != SLOT_EMPTY) {
        const map_entry_t *e = &m->entries[m->slots[slot]];
        if (e->hash == hash && strcmp(m->arena + e->key_off, key) == 0) {
            return m->slots[slot];
        }
        slot = (slot + 1u) & (HASH_SLOTS - 1u);
    }
    return -1;
}

/* Bytes used by live strings, optionally ignoring one entry's string value. */
static size_t live_bytes(const ds_map_t *m, int skip_value_of)
{
    size_t total = 0;
    int i;

    for (i = 0; i < m->count; ++i) {
        const map_entry_t *e = &m->entries[i];
        total += strlen(m->arena + e->key_off) + 1u;
        if (e->kind == GM_VALUE_STRING && i != skip_value_of) {
            total += strlen(m->arena + e->str_off) + 1u;
        }
    }
    return total;
}

static size_t compact_copy(const ds_map_t *m, size_t off, size_t *used)
{
    size_t len = strlen(m->arena + off) + 1u;
    size_t new_off = *used;

    memcpy(s_compact_buf + new_off, m->arena + off, len);
    *used += len;
    return new_off;
}

/* Rewrites the arena with only live strings. The string value of entry
 * `drop_value_of` (if >= 0) is discarded; the caller overwrites it next. */
static void compact(ds_map_t *m, int drop_value_of)
{
    size_t used = 0;
    int i;

    for (i = 0; i < m->count; ++i) {
        map_entry_t *e = &m->entries[i];
        e->key_off = compact_copy(m, e->key_off, &used);
        if (e->kind == GM_VALUE_STRING) {
            if (i == drop_value_of) {
                e->kind = GM_VALUE_UNDEFINED;
            } else {
                e->str_off = compact_copy(m, e->str_off, &used);
            }
        }
    }
    memcpy(m->arena, s_compact_buf, used);
    m->arena_used = used;
}

static size_t arena_append(ds_map_t *m, const char *s, size_t len_with_nul)
{
    size_t off = m->arena_used;

    memcpy(m->arena + off, s, len_with_nul);
    m->arena_used += len_with_nul;
    return off;
}

int gm_ds_map_create(void)
{
    int i;

    for (i = 0; i < GM_DS_MAP_MAX; ++i) {
        if (!s_maps[i].in_use) {
            s_maps[i].in_use = true;
            map_reset_storage(&s_maps[i]);
            return i;
        }
    }
    return -1;
}

void gm_ds_map_destroy(int id)
{
    ds_map_t *m = get_map(id);

    if (m != NULL) {
        map_reset_storage(m);
        m->in_use = false;
    }
}

bool gm_ds_map_is_valid(int id)
{
    return get_map(id) != NULL;
}

void gm_ds_map_clear(int id)
{
    ds_map_t *m = get_map(id);

    if (m != NULL) {
        map_reset_storage(m);
    }
}

int gm_ds_map_size(int id)
{
    ds_map_t *m = get_map(id);
    return m != NULL ? m->count : 0;
}

bool gm_ds_map_exists(int id, const char *key)
{
    ds_map_t *m = get_map(id);

    if (m == NULL || key == NULL) {
        return false;
    }
    return find_entry(m, key, hash_key(key)) >= 0;
}

bool gm_ds_map_replace(int id, const char *key, gm_value_t value)
{
    ds_map_t *m = get_map(id);
    bool is_string = value.kind == GM_VALUE_STRING;
    size_t key_size, val_size = 0, need;
    uint32_t hash;
    int idx;
    map_entry_t *e;

    if (m == NULL || key == NULL) {
        return false;
    }
    key_size = strlen(key) + 1u;
    if (key_size > GM_DS_MAP_STRING_BYTES) {
        return false;
    }
    if (is_string) {
        const char *s = value.str != NULL ? value.str : "";
        val_size = strlen(s) + 1u;
        if (val_size > GM_DS_MAP_STRING_BYTES) {
            return false;
        }
        memcpy(s_stage_val, s, val_size);
    }
    memcpy(s_stage_key, key, key_size);

    hash = hash_key(s_stage_key);
    idx = find_entry(m, s_stage_key, hash);
    if (idx < 0 && m->count >= GM_DS_MAP_MAX_ENTRIES) {
        return false;
    }
    need = (idx < 0 ? key_size : 0u) + val_size;
    if (m->arena_used + need > GM_DS_MAP_STRING_BYTES) {
        /* Check before touching anything so failure leaves the map intact. */
        if (live_bytes(m, idx) + need > GM_DS_MAP_STRING_BYTES) {
            return false;
        }
        compact(m, idx);
    }

    if (idx < 0) {
        idx = m->count++;
        e = &m->entries[idx];
        e->hash = hash;
        e->key_off = arena_append(m, s_stage_key, key_size);
        index_insert(m, idx);
    } else {
        e = &m->entries[idx];
    }
    e->kind = value.kind;
    e->real = value.kind == GM_VALUE_REAL ? value.real : 0.0f;
    e->str_off = is_string ? arena_append(m, s_stage_val, val_size) : 0u;
    return true;
}

void gm_ds_map_delete(int id, const char *key)
{
    ds_map_t *m = get_map(id);
    int idx;

    if (m == NULL || key == NULL) {
        return;
    }
    idx = find_entry(m, key, hash_key(key));
    if (idx < 0) {
        return;
    }
    /* Keep insertion order; the dead strings are reclaimed by compaction. */
    memmove(&m->entries[idx], &m->entries[idx + 1],
            (size_t)(m->count - idx - 1) * sizeof(m->entries[0]));
    m->count--;
    index_rebuild(m);
}

static gm_value_t entry_value(const ds_map_t *m, const map_entry_t *e)
{
    switch (e->kind) {
    case GM_VALUE_REAL:
        return gm_value_real(e->real);
    case GM_VALUE_STRING:
        return gm_value_string(m->arena + e->str_off);
    case GM_VALUE_UNDEFINED:
    default:
        return gm_value_undefined();
    }
}

gm_value_t gm_ds_map_find_value(int id, const char *key)
{
    ds_map_t *m = get_map(id);
    int idx;

    if (m == NULL || key == NULL) {
        return gm_value_undefined();
    }
    idx = find_entry(m, key, hash_key(key));
    return idx >= 0 ? entry_value(m, &m->entries[idx]) : gm_value_undefined();
}

gm_value_t gm_ds_map_find_first(int id)
{
    ds_map_t *m = get_map(id);

    if (m == NULL || m->count == 0) {
        return gm_value_undefined();
    }
    return gm_value_string(m->arena + m->entries[0].key_off);
}

/* ds_map.js L609: the key after `key` in iteration order; undefined if
 * `key` is the last one or not present. */
gm_value_t gm_ds_map_find_next(int id, const char *key)
{
    ds_map_t *m = get_map(id);
    int idx;

    if (m == NULL || key == NULL) {
        return gm_value_undefined();
    }
    idx = find_entry(m, key, hash_key(key));
    if (idx < 0 || idx + 1 >= m->count) {
        return gm_value_undefined();
    }
    return gm_value_string(m->arena + m->entries[idx + 1].key_off);
}

void gm_ds_reset(void)
{
    int i;

    for (i = 0; i < GM_DS_MAP_MAX; ++i) {
        gm_ds_map_destroy(i);
    }
}
