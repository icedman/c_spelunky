/*
 * gm_instance - see gm_instance.h for the semantics and references.
 */
#include "gm_instance.h"

#include "gm_math.h"
#include "gm_room.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

/* ---- Storage ---------------------------------------------------------------- */

static gm_instance_t s_pool[GM_INSTANCE_MAX];
static uint16_t s_free[GM_INSTANCE_MAX];
static int s_free_count;

/* Ordered instance lists with JS packing-yyList semantics. */
typedef struct inst_array {
    gm_instance_t *items[GM_INSTANCE_MAX];
    int count;
} inst_array_t;

static inst_array_t s_active;   /* yyRoom.m_Active */
static inst_array_t s_deactive; /* yyRoom.m_Deactive */
static inst_array_t s_all;      /* yyInstanceManager.m_Instances (creation order) */

/* Per-object recursive instance lists (yyObject.Instances_Recursive). */
typedef struct type_list {
    gm_instance_t *head;
    gm_instance_t *tail;
    int count;
} type_list_t;

static type_list_t s_types[GM_OBJECT_MAX];

/* id -> pool slot, open addressing with linear probing. */
#define ID_HASH_BITS 12
#define ID_HASH_SIZE (1 << ID_HASH_BITS)
#define ID_EMPTY (-1)
static int16_t s_id_table[ID_HASH_SIZE];

typedef char gm_instance_hash_fits[(ID_HASH_SIZE >= 2 * GM_INSTANCE_MAX &&
                                    GM_INSTANCE_MAX <= 32767) ? 1 : -1];

static int s_next_id = GM_INSTANCE_ID_BASE;
static uint32_t s_create_counter;
static uint32_t s_layer_serial;
static uint32_t s_link_serial;

static const gm_instance_hooks_t *s_hooks;

/* Draw sorting disabled for now: draw order falls back to active-list order. */
// #define DISABLE_DRAW_SORT

/* Scratch for bulk operations that never call user code. */
static gm_instance_t *s_scratch[GM_INSTANCE_MAX];
static gm_instance_t *s_sort_tmp[GM_INSTANCE_MAX];

/* Draw order cache: the active list (marked instances included) sorted by
 * draws_before. The key (floor(depth), layer_serial) is unique per instance, so
 * the order is total and filtering marked instances afterwards equals sorting
 * the filtered list. Invalidated by any active-list change or layer change. */
static gm_instance_t *s_draw_sorted[GM_INSTANCE_MAX];
static int s_draw_sorted_count;
static bool s_draw_dirty = true;

/* ---- Small helpers ---------------------------------------------------------- */

static bool usable(const gm_instance_t *inst)
{
    return inst != NULL && inst->in_use && inst->active && !inst->marked_for_destroy;
}

static int slot_of(const gm_instance_t *inst)
{
    return (int)(inst - s_pool);
}

static void array_append(inst_array_t *a, gm_instance_t *inst)
{
    a->items[a->count++] = inst;
    s_draw_dirty = true;
}

/* yyList.DeleteItem with packing: splice out the first occurrence. */
static void array_remove(inst_array_t *a, const gm_instance_t *inst)
{
    int i;

    for (i = 0; i < a->count; ++i) {
        if (a->items[i] == inst) {
            memmove(&a->items[i], &a->items[i + 1],
                    (size_t)(a->count - i - 1) * sizeof(a->items[0]));
            a->count--;
            s_draw_dirty = true;
            return;
        }
    }
}

/* Drops marked instances, preserving order. */
static void array_remove_marked(inst_array_t *a)
{
    int i, n = 0;

    for (i = 0; i < a->count; ++i) {
        if (!a->items[i]->marked_for_destroy) {
            a->items[n++] = a->items[i];
        }
    }
    if (n != a->count) {
        s_draw_dirty = true;
    }
    a->count = n;
}

/* ---- id map ------------------------------------------------------------------ */

static unsigned id_home(int id)
{
    return (unsigned)(((uint32_t)id * 2654435761u) >> (32 - ID_HASH_BITS));
}

static void id_insert(gm_instance_t *inst)
{
    unsigned i = id_home(inst->id);

    while (s_id_table[i] != ID_EMPTY) {
        i = (i + 1u) & (ID_HASH_SIZE - 1u);
    }
    s_id_table[i] = (int16_t)slot_of(inst);
}

static int id_find_index(int id)
{
    unsigned i = id_home(id);

    while (s_id_table[i] != ID_EMPTY) {
        if (s_pool[s_id_table[i]].id == id) {
            return (int)i;
        }
        i = (i + 1u) & (ID_HASH_SIZE - 1u);
    }
    return -1;
}

/* Linear-probing deletion with backward shift (no tombstones). */
static void id_remove(int id)
{
    int found = id_find_index(id);
    unsigned i, j;

    if (found < 0) {
        return;
    }
    i = (unsigned)found;
    j = i;
    for (;;) {
        unsigned k;
        j = (j + 1u) & (ID_HASH_SIZE - 1u);
        if (s_id_table[j] == ID_EMPTY) {
            break;
        }
        k = id_home(s_pool[s_id_table[j]].id);
        /* Move entry j into the hole at i if its home is not in (i, j]. */
        if ((i <= j) ? (k <= i || k > j) : (k <= i && k > j)) {
            s_id_table[i] = s_id_table[j];
            i = j;
        }
    }
    s_id_table[i] = ID_EMPTY;
}

/* ---- Per-object recursive lists --------------------------------------------- */

static gm_instance_link_t *link_for(gm_instance_t *inst, int list_level)
{
    return &inst->type_links[gm_object_level(inst->object_index) - list_level];
}

static void type_link_all(gm_instance_t *inst)
{
    int o = inst->object_index;

    inst->link_serial = ++s_link_serial;
    while (o != GM_OBJECT_NONE) {
        type_list_t *list = &s_types[o];
        int lvl = gm_object_level(o);
        gm_instance_link_t *ln = link_for(inst, lvl);

        ln->prev = list->tail;
        ln->next = NULL;
        if (list->tail != NULL) {
            link_for(list->tail, lvl)->next = inst;
        } else {
            list->head = inst;
        }
        list->tail = inst;
        list->count++;
        o = gm_object_get_parent(o);
    }
}

static void type_unlink_all(gm_instance_t *inst)
{
    int o = inst->object_index;

    while (o != GM_OBJECT_NONE) {
        type_list_t *list = &s_types[o];
        int lvl = gm_object_level(o);
        gm_instance_link_t *ln = link_for(inst, lvl);

        if (ln->prev != NULL) {
            link_for(ln->prev, lvl)->next = ln->next;
        } else {
            list->head = ln->next;
        }
        if (ln->next != NULL) {
            link_for(ln->next, lvl)->prev = ln->prev;
        } else {
            list->tail = ln->prev;
        }
        ln->prev = ln->next = NULL;
        list->count--;
        o = gm_object_get_parent(o);
    }
}

/* ---- Target cursor (GetWithArray / Instance_SearchLoop order, unfiltered) --- */

/* CUR_POOL walks the instance manager pool (creation order). */
enum { CUR_NONE, CUR_SINGLE, CUR_ACTIVE, CUR_TYPE, CUR_POOL };

typedef gm_instance_cursor_t cursor_t;

static void cursor_init(cursor_t *c, int target, gm_instance_t *self, gm_instance_t *other)
{
    c->mode = CUR_NONE;
    c->node = NULL;
    c->index = 0;
    c->list_level = 0;

    if (target == GM_ALL) {
        c->mode = CUR_ACTIVE;
    } else if (target == GM_SELF || target == GM_OTHER) {
        c->mode = CUR_SINGLE;
        c->node = target == GM_SELF ? self : other;
    } else if (gm_object_valid(target)) {
        c->mode = CUR_TYPE;
        c->node = s_types[target].head;
        c->list_level = gm_object_level(target);
    } else if (target >= GM_INSTANCE_ID_BASE) {
        c->mode = CUR_SINGLE;
        c->node = gm_instance_find_by_id(target);
    }
}

static gm_instance_t *cursor_next(cursor_t *c)
{
    gm_instance_t *inst;

    switch (c->mode) {
    case CUR_SINGLE:
        inst = c->node;
        c->node = NULL;
        c->mode = CUR_NONE;
        return inst;
    case CUR_ACTIVE:
        return c->index < s_active.count ? s_active.items[c->index++] : NULL;
    case CUR_POOL:
        return c->index < s_all.count ? s_all.items[c->index++] : NULL;
    case CUR_TYPE:
        inst = c->node;
        if (inst != NULL) {
            c->node = link_for(inst, c->list_level)->next;
        }
        return inst;
    case CUR_NONE:
    default:
        return NULL;
    }
}

/* Next instance passing the GetWithArray filter (active, not marked). */
static gm_instance_t *cursor_next_usable(cursor_t *c)
{
    gm_instance_t *inst;

    while ((inst = cursor_next(c)) != NULL) {
        if (usable(inst)) {
            return inst;
        }
    }
    return NULL;
}

/* ---- Lifecycle ------------------------------------------------------------------ */

void gm_instance_system_reset(void)
{
    int i;

    memset(s_pool, 0, sizeof(s_pool));
    for (i = 0; i < GM_INSTANCE_MAX; ++i) {
        /* Pop order hands out slot 0 first. */
        s_free[i] = (uint16_t)(GM_INSTANCE_MAX - 1 - i);
    }
    s_free_count = GM_INSTANCE_MAX;
    s_active.count = 0;
    s_deactive.count = 0;
    s_all.count = 0;
    memset(s_types, 0, sizeof(s_types));
    for (i = 0; i < ID_HASH_SIZE; ++i) {
        s_id_table[i] = ID_EMPTY;
    }
    s_next_id = GM_INSTANCE_ID_BASE;
    s_create_counter = 0;
    s_layer_serial = 0;
    s_link_serial = 0;
    s_draw_sorted_count = 0;
    s_draw_dirty = true;
    if (s_hooks != NULL && s_hooks->on_reset != NULL) {
        s_hooks->on_reset();
    }
}

void gm_instance_set_hooks(const gm_instance_hooks_t *hooks)
{
    s_hooks = hooks;
}

static void hook_enter(gm_instance_t *inst)
{
    if (s_hooks != NULL && s_hooks->on_enter != NULL) {
        s_hooks->on_enter(inst);
    }
}

static void hook_leave(gm_instance_t *inst)
{
    if (s_hooks != NULL && s_hooks->on_leave != NULL) {
        s_hooks->on_leave(inst);
    }
}

void gm_instance_set_next_id(int id)
{
    s_next_id = id;
}

/* yyInstance constructor defaults (yyInstance.js L41-150). */
static void init_defaults(gm_instance_t *inst, double x, double y, int object_index)
{
    int i;

    memset(inst, 0, sizeof(*inst));
    inst->object_index = object_index;
    inst->x = inst->xprevious = inst->xstart = x;
    inst->y = inst->yprevious = inst->ystart = y;
    inst->gravity_direction = 270.0;
    for (i = 0; i < GM_ALARM_COUNT; ++i) {
        inst->alarm[i] = -1.0;
    }
    for (i = 0; i < GM_SVAR_COUNT; ++i) {
        inst->strs[i] = ""; /* string slots are never NULL */
    }
    inst->image_xscale = 1.0;
    inst->image_yscale = 1.0;
    inst->image_alpha = 1.0;
    inst->image_blend = 0xFFFFFFu;
    inst->image_speed = 1.0;
    inst->sprite_index = -1;
    inst->mask_index = -1;
    inst->visible = true;
}

void gm_instance_init_detached(gm_instance_t *inst)
{
    init_defaults(inst, 0.0, 0.0, 0);
    inst->id = 0;
}

static gm_instance_t *add_instance(double x, double y, double depth, int object_index, int id)
{
    const gm_object_def_t *def = gm_object_get(object_index);
    gm_instance_t *inst;

    if (def == NULL || s_free_count == 0) {
        return NULL;
    }
    inst = &s_pool[s_free[--s_free_count]];
    init_defaults(inst, x, y, object_index);
    inst->id = id;
    inst->in_use = true;
    inst->active = true;
    /* SetObjectIndex / UpdateSpriteIndex (L938-990) */
    inst->sprite_index = def->default_sprite;
    inst->mask_index = def->default_mask;
    inst->solid = def->default_solid;
    inst->visible = def->default_visible;
    inst->persistent = def->default_persistent;
    /* GML_AddInstanceDepth takes yyGetInt32(depth). */
    inst->depth = (double)gm_to_int32(depth);
    inst->create_counter = s_create_counter;
    inst->layer_serial = ++s_layer_serial;

    array_append(&s_active, inst);
    array_append(&s_all, inst);
    type_link_all(inst);
    id_insert(inst);
    hook_enter(inst);
    return inst;
}

gm_instance_t *gm_instance_add(double x, double y, double depth, int object_index)
{
    gm_instance_t *inst = add_instance(x, y, depth, object_index, s_next_id);

    if (inst != NULL) {
        s_next_id++;
    }
    return inst;
}

gm_instance_t *gm_instance_add_with_id(double x, double y, double depth, int object_index, int id)
{
    if (id < GM_INSTANCE_ID_BASE || id_find_index(id) >= 0) {
        return NULL;
    }
    return add_instance(x, y, depth, object_index, id);
}

int gm_instance_next_id(void)
{
    return s_next_id;
}

/* yyRoom.DeleteInstance as used by ClearInstances (yyRoom.js L642): gone at once,
 * no events. */
void gm_instance_remove_now(gm_instance_t *inst)
{
    if (inst == NULL || !inst->in_use) {
        return;
    }
    if (inst->active) {
        hook_leave(inst);
        type_unlink_all(inst);
        array_remove(&s_active, inst);
    } else {
        array_remove(&s_deactive, inst);
    }
    array_remove(&s_all, inst);
    id_remove(inst->id);
    inst->in_use = false;
    inst->active = false;
    s_free[s_free_count++] = (uint16_t)slot_of(inst);
}

void gm_instance_active_detach(gm_instance_t *inst)
{
    if (inst != NULL && inst->in_use && inst->active) {
        array_remove(&s_active, inst);
    }
}

void gm_instance_active_attach(gm_instance_t *inst)
{
    int i;

    if (inst == NULL || !inst->in_use || !inst->active) {
        return;
    }
    for (i = 0; i < s_active.count; ++i) {
        if (s_active.items[i] == inst) {
            return;
        }
    }
    array_append(&s_active, inst);
}

gm_instance_t *gm_instance_create_depth(double x, double y, double depth, int object_index)
{
    gm_instance_t *inst = gm_instance_add(x, y, depth, object_index);
    gm_event_fn create;

    if (inst == NULL) {
        return NULL;
    }
    create = gm_object_find_event(object_index, GM_EV_CREATE, 0);
    if (create != NULL && !gm_room_event_blocked(inst, GM_EVENT_TYPE_CREATE)) {
        create(inst, inst);
    }
    return inst;
}

gm_instance_t *gm_instance_create(double x, double y, int object_index)
{
    return gm_instance_create_depth(x, y, (double)gm_object_get_depth(object_index),
                                    object_index);
}

/* DoDestroy + Command_Destroy (Function_Instance.js L1348, yyInstance.js L3807) */
void gm_instance_destroy(gm_instance_t *inst)
{
    gm_event_fn destroy;

    if (inst == NULL || !inst->in_use || inst->marked_for_destroy || !inst->active ||
        inst->being_destroyed) {
        return;
    }
    inst->being_destroyed = true;
    destroy = gm_object_find_event(inst->object_index, GM_EV_DESTROY, 0);
    if (destroy != NULL && !gm_room_event_blocked(inst, GM_EVENT_TYPE_DESTROY)) {
        destroy(inst, inst);
    }
    inst->marked_for_destroy = true;
}

int gm_instance_reclaim_marked(void)
{
    int i, n = 0, removed = 0;

    for (i = 0; i < s_all.count; ++i) {
        gm_instance_t *inst = s_all.items[i];
        if (inst->marked_for_destroy) {
            if (inst->active) {
                hook_leave(inst);
                type_unlink_all(inst);
            }
            id_remove(inst->id);
        }
    }
    array_remove_marked(&s_active);
    array_remove_marked(&s_deactive);
    for (i = 0; i < s_all.count; ++i) {
        gm_instance_t *inst = s_all.items[i];
        if (inst->marked_for_destroy) {
            inst->in_use = false;
            s_free[s_free_count++] = (uint16_t)slot_of(inst);
            removed++;
        } else {
            s_all.items[n++] = inst;
        }
    }
    s_all.count = n;
    return removed;
}

/* ---- Lookup ----------------------------------------------------------------------- */

int gm_instance_count(void)
{
    return s_all.count;
}

gm_instance_t *gm_instance_find_by_id(int id)
{
    int idx = id_find_index(id);
    return idx >= 0 ? &s_pool[s_id_table[idx]] : NULL;
}

gm_instance_t *gm_instance_resolve(int target, gm_instance_t *self, gm_instance_t *other)
{
    cursor_t c;

    cursor_init(&c, target, self, other);
    return cursor_next_usable(&c);
}

bool gm_instance_exists(int target, gm_instance_t *self, gm_instance_t *other)
{
    return gm_instance_resolve(target, self, other) != NULL;
}

int gm_instance_number(int target, gm_instance_t *self, gm_instance_t *other)
{
    cursor_t c;
    int n = 0;

    cursor_init(&c, target, self, other);
    while (cursor_next_usable(&c) != NULL) {
        n++;
    }
    return n;
}

gm_instance_t *gm_instance_find(int target, int n, gm_instance_t *self, gm_instance_t *other)
{
    cursor_t c;
    gm_instance_t *inst;

    if (n < 0) {
        return NULL;
    }
    cursor_init(&c, target, self, other);
    while ((inst = cursor_next_usable(&c)) != NULL) {
        if (n-- == 0) {
            return inst;
        }
    }
    return NULL;
}

static void nearest_consider(gm_instance_t *inst, double x, double y,
                             double *best, gm_instance_t **found)
{
    double xx = x - inst->x;
    double yy = y - inst->y;
    double d = sqrt(xx * xx + yy * yy);

    if (d < *best) {
        *best = d;
        *found = inst;
    }
}

/* Instance_SearchLoop2 (Globals.js L1549): `all` walks the instance manager
 * list (creation order); objects walk their recursive list. */
gm_instance_t *gm_instance_nearest(double x, double y, int target)
{
    double best = 10000000000.0;
    gm_instance_t *found = NULL;
    int i;

    if (target == GM_ALL) {
        for (i = 0; i < s_all.count; ++i) {
            if (usable(s_all.items[i])) {
                nearest_consider(s_all.items[i], x, y, &best, &found);
            }
        }
    } else if (gm_object_valid(target) || target >= GM_INSTANCE_ID_BASE) {
        cursor_t c;
        gm_instance_t *inst;

        cursor_init(&c, target, NULL, NULL);
        while ((inst = cursor_next_usable(&c)) != NULL) {
            nearest_consider(inst, x, y, &best, &found);
        }
    }
    return found;
}

int gm_instance_slot(const gm_instance_t *inst)
{
    return slot_of(inst);
}

gm_instance_t *gm_instance_at_slot(int slot)
{
    if (slot < 0 || slot >= GM_INSTANCE_MAX || !s_pool[slot].in_use) {
        return NULL;
    }
    return &s_pool[slot];
}

/* ---- Instance_SearchLoop order -------------------------------------------------------- */

void gm_instance_search_begin(gm_instance_cursor_t *c, int target)
{
    if (target == GM_ALL) {
        c->mode = CUR_POOL;
        c->node = NULL;
        c->index = 0;
        c->list_level = 0;
    } else if (target < 0) {
        c->mode = CUR_NONE; /* self/other/noone: object lookup fails */
        c->node = NULL;
    } else {
        cursor_init(c, target, NULL, NULL);
    }
}

gm_instance_t *gm_instance_search_next(gm_instance_cursor_t *c)
{
    return cursor_next_usable(c);
}

/* ---- with() snapshots (gm_with) ----------------------------------------------------- */

int gm_instance_with_snapshot(int target, gm_instance_t *self, gm_instance_t *other,
                              uint16_t *slots, int max)
{
    cursor_t c;
    gm_instance_t *inst;
    int count = 0;

    cursor_init(&c, target, self, other);
    while ((inst = cursor_next_usable(&c)) != NULL) {
        if (count >= max) {
            return -1;
        }
        slots[count++] = (uint16_t)slot_of(inst);
    }
    return count;
}

int gm_instance_object_pool(int object_index, uint16_t *slots, int max)
{
    cursor_t c;
    gm_instance_t *inst;
    int count = 0;

    if (!gm_object_valid(object_index)) {
        return 0;
    }
    cursor_init(&c, object_index, NULL, NULL);
    while ((inst = cursor_next(&c)) != NULL) {
        if (count >= max) {
            return -1;
        }
        slots[count++] = (uint16_t)slot_of(inst);
    }
    return count;
}

gm_instance_t *gm_instance_with_slot(uint16_t slot)
{
    /* Snapshot slots cannot be recycled mid-iteration: slots are only freed
     * by gm_instance_reclaim_marked, which runs between event passes. */
    gm_instance_t *inst;

    if (slot >= GM_INSTANCE_MAX) {
        return NULL;
    }
    inst = &s_pool[slot];
    return usable(inst) ? inst : NULL;
}

/* ---- Event-pass support ------------------------------------------------------------- */

int gm_instance_active_count(void)
{
    return s_active.count;
}

gm_instance_t *gm_instance_active_at(int index)
{
    return (index >= 0 && index < s_active.count) ? s_active.items[index] : NULL;
}

uint32_t gm_instance_pass_begin(void)
{
    return s_create_counter++;
}

bool gm_instance_in_pass(const gm_instance_t *inst, uint32_t pass)
{
    return inst != NULL && inst->create_counter <= pass;
}

/* ---- Activation --------------------------------------------------------------------- */

void gm_instance_deactivate(gm_instance_t *inst)
{
    if (inst == NULL || !inst->in_use || !inst->active) {
        return;
    }
    hook_leave(inst);
    array_remove(&s_active, inst);
    type_unlink_all(inst);
    array_append(&s_deactive, inst);
    inst->active = false;
}

void gm_instance_activate(gm_instance_t *inst)
{
    if (inst == NULL || !inst->in_use || inst->active) {
        return;
    }
    array_remove(&s_deactive, inst);
    array_append(&s_active, inst);
    type_link_all(inst);
    inst->active = true;
    hook_enter(inst);
}

/* Matches instance_(de)activate_object's filter (Function_Instance.js L1685):
 * same object, same id, or the object inherits from target. */
static bool matches_object_or_id(const gm_instance_t *inst, int target)
{
    return inst->id == target || gm_object_is_a(inst->object_index, target);
}

void gm_instance_deactivate_object(int target)
{
    int i, n = 0;

    for (i = 0; i < s_active.count; ++i) {
        gm_instance_t *inst = s_active.items[i];
        if (target == GM_ALL || matches_object_or_id(inst, target)) {
            s_scratch[n++] = inst;
        }
    }
    for (i = 0; i < n; ++i) {
        gm_instance_deactivate(s_scratch[i]);
    }
}

void gm_instance_activate_object(int target)
{
    int i, n = 0;

    if (target == GM_ALL) {
        return; /* HTML5 runner quirk, see header */
    }
    for (i = 0; i < s_deactive.count; ++i) {
        gm_instance_t *inst = s_deactive.items[i];
        if (matches_object_or_id(inst, target)) {
            s_scratch[n++] = inst;
        }
    }
    for (i = 0; i < n; ++i) {
        gm_instance_activate(s_scratch[i]);
    }
}

/* Function_Instance.js L1586 */
void gm_instance_deactivate_all(bool notme, gm_instance_t *self)
{
    int i, n = s_active.count;

    memcpy(s_scratch, s_active.items, (size_t)n * sizeof(s_scratch[0]));
    for (i = 0; i < n; ++i) {
        if (!(notme && s_scratch[i] == self)) {
            gm_instance_deactivate(s_scratch[i]);
        }
    }
}

/* Function_Instance.js L1620 */
void gm_instance_activate_all(void)
{
    int i, n = s_deactive.count;

    memcpy(s_scratch, s_deactive.items, (size_t)n * sizeof(s_scratch[0]));
    for (i = 0; i < n; ++i) {
        gm_instance_activate(s_scratch[i]);
    }
}

/* ---- Property setters ---------------------------------------------------------------- */

/* Globals.js L1341 */
static double html5_fmod(double x, double y)
{
    if (x == 0.0) {
        return 0.0;
    }
    return fmod(x * 16777216.0, y * 16777216.0) / 16777216.0;
}

/* yyInstance.js L1184: ClampFloat = (~~(f * 1000000)) / 1000000 */
static double clamp_float(double f)
{
    return (double)gm_to_int32(f * 1000000.0) / 1000000.0;
}

static double snap_near_int(double v)
{
    double r = gm_round(v);
    return fabs(v - r) < 0.0001 ? r : v;
}

/* Compute_Speed1 (yyInstance.js L1131): direction/speed from hspeed/vspeed. */
static void compute_speed1(gm_instance_t *inst)
{
    if (inst->hspeed == 0.0) {
        if (inst->vspeed > 0.0) {
            inst->direction = 270.0;
        } else if (inst->vspeed < 0.0) {
            inst->direction = 90.0;
        }
    } else {
        double dd = clamp_float(180.0 * atan2(inst->vspeed, inst->hspeed) / GM_PI);
        inst->direction = dd <= 0.0 ? -dd : 360.0 - dd;
    }
    inst->direction = snap_near_int(inst->direction);
    inst->direction = html5_fmod(inst->direction, 360.0);

    inst->speed = sqrt(inst->hspeed * inst->hspeed + inst->vspeed * inst->vspeed);
    inst->speed = snap_near_int(inst->speed);
}

/* Compute_Speed2 (yyInstance.js L1175): hspeed/vspeed from speed/direction. */
static void compute_speed2(gm_instance_t *inst)
{
    inst->hspeed = inst->speed * clamp_float(cos(inst->direction * 0.0174532925));
    inst->vspeed = -inst->speed * clamp_float(sin(inst->direction * 0.0174532925));
    inst->hspeed = snap_near_int(inst->hspeed);
    inst->vspeed = snap_near_int(inst->vspeed);
}

void gm_instance_set_hspeed(gm_instance_t *inst, double v)
{
    if (inst->hspeed == v) {
        return;
    }
    inst->hspeed = v;
    compute_speed1(inst);
}

void gm_instance_set_vspeed(gm_instance_t *inst, double v)
{
    if (inst->vspeed == v) {
        return;
    }
    inst->vspeed = v;
    compute_speed1(inst);
}

void gm_instance_set_speed(gm_instance_t *inst, double v)
{
    if (inst->speed == v) {
        return;
    }
    inst->speed = v;
    compute_speed2(inst);
}

/* direction setter (yyInstance.js L227) */
void gm_instance_set_direction(gm_instance_t *inst, double v)
{
    while (v < 0.0) {
        v += 360.0;
    }
    while (v > 360.0) {
        v -= 360.0;
    }
    inst->direction = html5_fmod(v, 360.0);
    compute_speed2(inst);
}

void gm_instance_adapt_speed(gm_instance_t *inst)
{
    if (inst->friction != 0.0) {
        double ns = inst->speed > 0.0 ? inst->speed - inst->friction : inst->speed + inst->friction;

        if ((inst->speed > 0.0 && ns < 0.0) || (inst->speed < 0.0 && ns > 0.0)) {
            gm_instance_set_speed(inst, 0.0);
        } else if (inst->speed != 0.0) {
            gm_instance_set_speed(inst, ns);
        }
    }
    if (inst->gravity != 0.0) {
        double dir = inst->gravity_direction * 0.0174532925;

        /* AddTo_Speed: both property setters, then Compute_Speed1 once more. */
        gm_instance_set_hspeed(inst, inst->hspeed + inst->gravity * clamp_float(cos(dir)));
        gm_instance_set_vspeed(inst, inst->vspeed - inst->gravity * clamp_float(sin(dir)));
        compute_speed1(inst);
    }
}

/* Changing depth moves the instance to the front of the layer for the new
 * floor(depth) (yyRoom.ProcessDepthList2 re-adds it via AddNewElement). */
void gm_instance_set_depth(gm_instance_t *inst, double depth)
{
    if (inst->depth == depth) {
        return;
    }
    if (floor(inst->depth) != floor(depth)) {
        inst->layer_serial = ++s_layer_serial;
        if (inst->active) {
            s_draw_dirty = true;
        }
    }
    inst->depth = depth;
}

/* ---- Draw order ------------------------------------------------------------------------ */

static bool draws_before(const gm_instance_t *a, const gm_instance_t *b)
{
    double da = floor(a->depth);
    double db = floor(b->depth);

    if (da != db) {
        return da > db;
    }
    return a->layer_serial > b->layer_serial;
}

/* Bottom-up merge sort (stable, allocation-free). */
static void sort_draw_order(gm_instance_t **items, int n)
{
    gm_instance_t **src = items;
    gm_instance_t **dst = s_sort_tmp;
    int width, i;

    for (width = 1; width < n; width *= 2) {
        for (i = 0; i < n; i += 2 * width) {
            int lo = i;
            int mid = (i + width < n) ? i + width : n;
            int hi = (i + 2 * width < n) ? i + 2 * width : n;
            int a = lo, b = mid, k = lo;

            while (a < mid && b < hi) {
                dst[k++] = draws_before(src[b], src[a]) ? src[b++] : src[a++];
            }
            while (a < mid) {
                dst[k++] = src[a++];
            }
            while (b < hi) {
                dst[k++] = src[b++];
            }
        }
        {
            gm_instance_t **t = src;
            src = dst;
            dst = t;
        }
    }
    if (src != items) {
        memcpy(items, src, (size_t)n * sizeof(items[0]));
    }
}

int gm_instance_draw_order(gm_instance_t **out, int max)
{
    int i, n = 0;

    if (s_draw_dirty) {
        memcpy(s_draw_sorted, s_active.items, (size_t)s_active.count * sizeof(s_draw_sorted[0]));
        s_draw_sorted_count = s_active.count;
#ifndef DISABLE_DRAW_SORT
        sort_draw_order(s_draw_sorted, s_draw_sorted_count);
#else
        (void)sort_draw_order;
#endif
        s_draw_dirty = false;
    }
    for (i = 0; i < s_draw_sorted_count && n < max && n < GM_INSTANCE_MAX; ++i) {
        if (!s_draw_sorted[i]->marked_for_destroy) {
            out[n++] = s_draw_sorted[i];
        }
    }
    return n;
}
