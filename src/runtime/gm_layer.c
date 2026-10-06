/*
 * gm_layer - see gm_layer.h for semantics and references.
 */
#include "gm_layer.h"

#include "gm_sprite.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static gm_layer_t s_layers[GM_LAYER_MAX];
static bool s_layer_used[GM_LAYER_MAX];
static int s_order[GM_LAYER_MAX];        /* layer slots, depth ascending */
static int s_order_count;

static gm_element_t s_elems[GM_ELEMENT_MAX];
static bool s_elem_used[GM_ELEMENT_MAX];
static int s_elem_free[GM_ELEMENT_MAX];
static int s_elem_free_count;

/* element id -> slot, open addressing with backward-shift deletion. */
#define EL_HASH_BITS 13
#define EL_HASH_SIZE (1 << EL_HASH_BITS)
static int16_t s_hash[EL_HASH_SIZE];
typedef char gm_layer_hash_fits[(EL_HASH_SIZE >= 2 * GM_ELEMENT_MAX && GM_ELEMENT_MAX <= 32767) ? 1 : -1];

static int s_watermark;
static int s_current_layer_id;
static int s_next_element_id;
static long s_overflows;

/* ---- element id hash ------------------------------------------------------------------ */

static unsigned home(int id)
{
    return (unsigned)(((uint32_t)id * 2654435761u) >> (32 - EL_HASH_BITS));
}

static void hash_insert(int slot)
{
    unsigned i = home(s_elems[slot].id);

    while (s_hash[i] >= 0) {
        i = (i + 1u) & (EL_HASH_SIZE - 1u);
    }
    s_hash[i] = (int16_t)slot;
}

static int hash_index(int id)
{
    unsigned i = home(id);

    while (s_hash[i] >= 0) {
        if (s_elems[s_hash[i]].id == id) {
            return (int)i;
        }
        i = (i + 1u) & (EL_HASH_SIZE - 1u);
    }
    return -1;
}

static void hash_remove(int id)
{
    int found = hash_index(id);
    unsigned i, j;

    if (found < 0) {
        return;
    }
    i = j = (unsigned)found;
    for (;;) {
        unsigned k;
        j = (j + 1u) & (EL_HASH_SIZE - 1u);
        if (s_hash[j] < 0) {
            break;
        }
        k = home(s_elems[s_hash[j]].id);
        if ((i <= j) ? (k <= i || k > j) : (k <= i && k > j)) {
            s_hash[i] = s_hash[j];
            i = j;
        }
    }
    s_hash[i] = -1;
}

/* ---- setup ------------------------------------------------------------------------------- */

static void clear_pools(void)
{
    int i;

    memset(s_layer_used, 0, sizeof(s_layer_used));
    s_order_count = 0;
    memset(s_elem_used, 0, sizeof(s_elem_used));
    for (i = 0; i < GM_ELEMENT_MAX; ++i) {
        s_elem_free[i] = GM_ELEMENT_MAX - 1 - i;
    }
    s_elem_free_count = GM_ELEMENT_MAX;
    for (i = 0; i < EL_HASH_SIZE; ++i) {
        s_hash[i] = -1;
    }
}

void gm_layer_reset(void)
{
    clear_pools();
    s_watermark = 0;
    s_current_layer_id = 0;
    s_next_element_id = 0;
    s_overflows = 0;
}

void gm_layer_set_watermark(int id)
{
    s_watermark = id;
}

long gm_layer_overflows(void)
{
    return s_overflows;
}

/* ---- layers ------------------------------------------------------------------------------- */

static int layer_slot(const gm_layer_t *l)
{
    return (int)(l - s_layers);
}

/* yyOList.Add: before the first layer of greater depth. */
static void order_insert(int slot)
{
    int i, at = s_order_count;

    for (i = 0; i < s_order_count; ++i) {
        if (s_layers[slot].depth < s_layers[s_order[i]].depth) {
            at = i;
            break;
        }
    }
    memmove(&s_order[at + 1], &s_order[at], (size_t)(s_order_count - at) * sizeof(s_order[0]));
    s_order[at] = slot;
    s_order_count++;
}

static void order_remove(int slot)
{
    int i;

    for (i = 0; i < s_order_count; ++i) {
        if (s_order[i] == slot) {
            memmove(&s_order[i], &s_order[i + 1], (size_t)(s_order_count - i - 1) * sizeof(s_order[0]));
            s_order_count--;
            return;
        }
    }
}

static gm_layer_t *layer_add(int id, int depth, const char *name, int kind, bool dynamic)
{
    int slot;
    gm_layer_t *l;

    for (slot = 0; slot < GM_LAYER_MAX && s_layer_used[slot]; ++slot) {
    }
    if (slot == GM_LAYER_MAX) {
        s_overflows++;
        return NULL;
    }
    s_layer_used[slot] = true;
    l = &s_layers[slot];
    memset(l, 0, sizeof(*l));
    l->id = id;
    l->depth = depth;
    l->kind = kind;
    l->dynamic = dynamic;
    l->visible = true;
    l->head = l->tail = -1;
    if (name != NULL) {
        size_t n = strlen(name);
        if (n >= sizeof(l->name)) {
            n = sizeof(l->name) - 1;
        }
        memcpy(l->name, name, n);
        l->name[n] = '\0';
    }
    order_insert(slot);
    return l;
}

/* GetNextLayerID (Function_Layers.js L1494). */
static int next_layer_id(void)
{
    if (s_current_layer_id < s_watermark) {
        s_current_layer_id = s_watermark;
    }
    return ++s_current_layer_id;
}

int gm_layer_count(void)
{
    return s_order_count;
}

gm_layer_t *gm_layer_at(int index)
{
    return (index >= 0 && index < s_order_count) ? &s_layers[s_order[index]] : NULL;
}

gm_layer_t *gm_layer_find(int id)
{
    int i;

    for (i = 0; i < s_order_count; ++i) {
        if (s_layers[s_order[i]].id == id) {
            return &s_layers[s_order[i]];
        }
    }
    return NULL;
}

static bool name_eq(const char *a, const char *b)
{
    for (; *a != '\0' && *b != '\0'; ++a, ++b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return false;
        }
    }
    return *a == *b;
}

/* GetLayerFromName (L943): case-insensitive, empty names never match. */
gm_layer_t *gm_layer_find_name(const char *name)
{
    int i;

    if (name == NULL || name[0] == '\0') {
        return NULL;
    }
    for (i = 0; i < s_order_count; ++i) {
        gm_layer_t *l = &s_layers[s_order[i]];
        if (l->name[0] != '\0' && name_eq(l->name, name)) {
            return l;
        }
    }
    return NULL;
}

int gm_layer_get_all(int *out, int max)
{
    int i, n = 0;

    for (i = 0; i < s_order_count && n < max; ++i) {
        if (!s_layers[s_order[i]].dynamic) {
            out[n++] = s_layers[s_order[i]].id;
        }
    }
    return n;
}

/* layer_create (L2394). */
int gm_layer_create(int depth, const char *name)
{
    int id = next_layer_id();
    char buf[32];
    gm_layer_t *l;

    if (name == NULL) {
        sprintf(buf, "_layer_%x", (unsigned)id);
        name = buf;
    }
    l = layer_add(id, depth, name, 0, false);
    return l != NULL ? l->id : -1;
}

/* ---- elements ------------------------------------------------------------------------------ */

static void element_free(int slot)
{
    gm_element_t *e = &s_elems[slot];
    gm_layer_t *l = &s_layers[e->layer];

    if (e->prev >= 0) {
        s_elems[e->prev].next = e->next;
    } else {
        l->head = e->next;
    }
    if (e->next >= 0) {
        s_elems[e->next].prev = e->prev;
    } else {
        l->tail = e->prev;
    }
    l->count--;
    hash_remove(e->id);
    s_elem_used[slot] = false;
    s_elem_free[s_elem_free_count++] = slot;
}

/* AddNewElement (L856): a new id, at the front of the layer's list. */
static gm_element_t *element_add(gm_layer_t *l, int type)
{
    int slot;
    gm_element_t *e;

    if (s_elem_free_count == 0) {
        s_overflows++;
        return NULL;
    }
    slot = s_elem_free[--s_elem_free_count];
    s_elem_used[slot] = true;
    e = &s_elems[slot];
    memset(e, 0, sizeof(*e));
    e->id = s_next_element_id++;
    e->type = type;
    e->layer = layer_slot(l);
    e->visible = true;
    e->sprite = -1;
    e->xscale = e->yscale = 1.0f;
    e->blend = 0xFFFFFFu;
    e->alpha = 1.0f;
    e->prev = -1;
    e->next = l->head;
    if (l->head >= 0) {
        s_elems[l->head].prev = slot;
    } else {
        l->tail = slot;
    }
    l->head = slot;
    l->count++;
    hash_insert(slot);
    return e;
}

gm_element_t *gm_layer_element_slot(int slot)
{
    return (slot >= 0 && slot < GM_ELEMENT_MAX && s_elem_used[slot]) ? &s_elems[slot] : NULL;
}

gm_element_t *gm_layer_element(int element_id)
{
    int i = hash_index(element_id);
    return i >= 0 ? &s_elems[s_hash[i]] : NULL;
}

int gm_layer_element_type(int element_id)
{
    gm_element_t *e = gm_layer_element(element_id);
    return e != NULL ? e->type : -1;
}

void gm_layer_element_destroy(int element_id)
{
    int i = hash_index(element_id);

    if (i >= 0) {
        element_free(s_hash[i]);
    }
}

int gm_layer_elements(int layer_id, int *out, int max)
{
    gm_layer_t *l = gm_layer_find(layer_id);
    int slot, n = 0;

    if (l == NULL) {
        return 0;
    }
    for (slot = l->head; slot >= 0 && n < max; slot = s_elems[slot].next) {
        out[n++] = s_elems[slot].id;
    }
    return n;
}

/* layer_tile_create (L5057). */
int gm_layer_tile_create(int layer_id, float x, float y, int sprite, int left, int top, int w, int h)
{
    gm_layer_t *l = gm_layer_find(layer_id);
    gm_element_t *e = l != NULL ? element_add(l, GM_ELEMENT_TILE) : NULL;

    if (e == NULL) {
        return -1;
    }
    e->sprite = sprite;
    e->x = x;
    e->y = y;
    e->xo = left;
    e->yo = top;
    e->w = w;
    e->h = h;
    return e->id;
}

/* layer_background_create (L2756). */
int gm_layer_background_create(int layer_id, int sprite)
{
    gm_layer_t *l = gm_layer_find(layer_id);
    gm_element_t *e = l != NULL ? element_add(l, GM_ELEMENT_BACKGROUND) : NULL;

    if (e == NULL) {
        return -1;
    }
    e->sprite = sprite;
    e->image_speed = 1.0f;
    return e->id;
}

bool gm_layer_background_exists(int layer_id, int element_id)
{
    gm_element_t *e = gm_layer_element(element_id);
    gm_layer_t *l = gm_layer_find(layer_id);

    return e != NULL && l != NULL && e->layer == layer_slot(l) && e->type == GM_ELEMENT_BACKGROUND;
}

/* RemoveLayer (L1337). */
void gm_layer_destroy(int id)
{
    gm_layer_t *l = gm_layer_find(id);
    int slot;

    if (l == NULL) {
        return;
    }
    while (l->head >= 0) {
        element_free(l->head);
    }
    slot = layer_slot(l);
    order_remove(slot);
    s_layer_used[slot] = false;
}

/* layer_depth -> ChangeLayerDepth(..., false) (L1361): re-sort, no merging. */
void gm_layer_set_depth(int id, int depth)
{
    gm_layer_t *l = gm_layer_find(id);

    if (l == NULL || l->depth == depth) {
        return;
    }
    l->depth = depth;
    order_remove(layer_slot(l));
    order_insert(layer_slot(l));
}

/* ---- rooms ----------------------------------------------------------------------------------- */

/* BuildRoomLayers (L1861-2088). */
void gm_layer_room_start(const gm_room_def_t *room)
{
    int j, i;

    for (j = room->layer_count - 1; j >= 0; --j) {
        const gm_room_layer_def_t *d = &room->layers[j];
        gm_layer_t *l = layer_add(d->id, d->depth, d->name, d->kind, false);

        if (l == NULL) {
            continue;
        }
        l->visible = d->visible;
        l->x = d->x;
        l->y = d->y;
        l->hspeed = d->hspeed;
        l->vspeed = d->vspeed;
        if (d->kind == GM_LAYER_BACKGROUND) {
            gm_element_t *e = element_add(l, GM_ELEMENT_BACKGROUND);
            const gm_sprite_def_t *spr = gm_sprite_get(d->bg_sprite);
            if (e == NULL) {
                continue;
            }
            e->sprite = d->bg_sprite;
            e->visible = d->visible;
            e->htiled = d->bg_htiled;
            e->vtiled = d->bg_vtiled;
            e->stretch = d->bg_stretch;
            e->blend = d->bg_colour & 0xFFFFFFu;
            e->alpha = (float)((d->bg_colour >> 24) & 0xFFu) / 255.0f;
            e->image_speed = 1.0f;
            if (d->bg_stretch && spr != NULL && spr->width > 0 && spr->height > 0) {
                e->xscale = (float)room->width / (float)spr->width;
                e->yscale = (float)room->height / (float)spr->height;
            }
        } else if (d->kind == GM_LAYER_ASSETS) {
            for (i = d->tile_count - 1; i >= 0; --i) {
                const gm_room_tile_def_t *t = &room->tiles[d->first_tile + i];
                gm_element_t *e = element_add(l, GM_ELEMENT_TILE);
                if (e == NULL) {
                    break;
                }
                e->sprite = t->sprite;
                e->x = t->x;
                e->y = t->y;
                e->xo = t->u;
                e->yo = t->v;
                e->w = t->w;
                e->h = t->h;
                e->xscale = t->xscale;
                e->yscale = t->yscale;
                e->blend = t->colour & 0xFFFFFFu;
                e->alpha = (float)((t->colour >> 24) & 0xFFu) / 255.0f;
            }
        }
    }
}

/* CleanRoomLayers (L1778): every layer and element goes; ids keep counting. */
void gm_layer_room_end(void)
{
    clear_pools();
}

void gm_layer_update(void)
{
    int i;

    for (i = 0; i < s_order_count; ++i) {
        gm_layer_t *l = &s_layers[s_order[i]];
        l->x += l->hspeed;
        l->y += l->vspeed;
    }
}
