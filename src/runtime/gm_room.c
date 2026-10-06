/*
 * gm_room - see gm_room.h for the room start sequence and references.
 */
#include "gm_room.h"

#include "gm_collision.h"
#include "gm_layer.h"
#include "gm_view.h"

#include <stddef.h>
#include <string.h>

static const gm_room_def_t *s_rooms;
static int s_count;
static const int *s_order;
static int s_order_count;
static const gm_room_hooks_t *s_hooks;

static int s_current = GM_ROOM_NONE;
static int s_pending = GM_ROOM_NONE;
static long s_errors;
static long s_overflows;

/* Room built-ins (yyRoom.SetWidth ... copy them into g_pBuiltIn, yyRoom.js L111). */
static float s_width, s_height, s_speed;
static bool s_persistent;
static char s_caption[128];

/* StartRoom scratch: persistent instances being carried over, the instances created
 * for the new room (by storage index), and the pool slots to keep. */
static gm_instance_t *s_carry[GM_INSTANCE_MAX];
static int s_carry_count;
static gm_instance_t *s_created[GM_INSTANCE_MAX];
static bool s_keep[GM_INSTANCE_MAX];

/* Self/other of room creation code (_GameMaker.js L1503: a throwaway yyInstance). */
static gm_instance_t s_dummy;

/* ---- Registry ----------------------------------------------------------------------- */

static bool room_ok(const gm_room_def_t *r)
{
    int i;

    if (r->name == NULL || r->persistent || r->layer_count < 0 || r->tile_count < 0 ||
        r->instance_count < 0 || r->instance_count > GM_INSTANCE_MAX ||
        (r->layer_count > 0 && r->layers == NULL) || (r->tile_count > 0 && r->tiles == NULL) ||
        (r->instance_count > 0 && r->instances == NULL)) {
        return false;
    }
    for (i = 0; i < r->layer_count; ++i) {
        const gm_room_layer_def_t *l = &r->layers[i];
        if (l->first_tile < 0 || l->tile_count < 0 || l->first_tile + l->tile_count > r->tile_count) {
            return false;
        }
    }
    for (i = 0; i < r->instance_count; ++i) {
        const gm_room_inst_def_t *d = &r->instances[i];
        if (!gm_object_valid(d->object) || d->layer < 0 || d->layer >= r->layer_count ||
            d->id < GM_INSTANCE_ID_BASE || d->id >= GM_ROOM_FIRST_DYNAMIC_ID) {
            return false;
        }
    }
    return true;
}

bool gm_room_registry_init(const gm_room_def_t *rooms, int count, const int *order, int order_count)
{
    int i;

    s_rooms = NULL;
    s_count = 0;
    s_order = NULL;
    s_order_count = 0;
    gm_room_reset();
    if (count < 0 || count > GM_ROOM_MAX || (count > 0 && rooms == NULL) || order_count < 0 ||
        order_count > count || (order_count > 0 && order == NULL)) {
        return false;
    }
    for (i = 0; i < count; ++i) {
        if (!room_ok(&rooms[i])) {
            return false;
        }
    }
    for (i = 0; i < order_count; ++i) {
        if (order[i] < 0 || order[i] >= count) {
            return false;
        }
    }
    s_rooms = rooms;
    s_count = count;
    s_order = order;
    s_order_count = order_count;
    /* Load time: storage views become mutable, layer ids are numbered above the
     * highest storage id (BuildRoomLayers' SetLayerIndexWatermark, L2268). */
    gm_view_init(rooms, count);
    gm_layer_reset();
    {
        int r, l, top = 0;
        for (r = 0; r < count; ++r) {
            for (l = 0; l < rooms[r].layer_count; ++l) {
                top = rooms[r].layers[l].id > top ? rooms[r].layers[l].id : top;
            }
        }
        gm_layer_set_watermark(top);
    }
    return true;
}

void gm_room_set_hooks(const gm_room_hooks_t *hooks)
{
    s_hooks = hooks;
}

void gm_room_reset(void)
{
    s_current = GM_ROOM_NONE;
    s_pending = GM_ROOM_NONE;
    s_errors = 0;
    s_overflows = 0;
    s_width = 640.0f;
    s_height = 480.0f;
    s_speed = 30.0f;
    s_persistent = false;
    s_caption[0] = '\0';
}

int gm_room_count(void)
{
    return s_count;
}

bool gm_room_exists(int room)
{
    return room >= 0 && room < s_count;
}

const gm_room_def_t *gm_room_get(int room)
{
    return gm_room_exists(room) ? &s_rooms[room] : NULL;
}

const char *gm_room_get_name(int room)
{
    return gm_room_exists(room) ? s_rooms[room].name : "";
}

/* ---- Room order --------------------------------------------------------------------- */

int gm_room_first(void)
{
    return s_order_count > 0 ? s_order[0] : GM_ROOM_NONE;
}

int gm_room_last(void)
{
    return s_order_count > 0 ? s_order[s_order_count - 1] : GM_ROOM_NONE;
}

static int order_index(int room)
{
    int i;

    for (i = 0; i < s_order_count; ++i) {
        if (s_order[i] == room) {
            return i;
        }
    }
    return -1;
}

/* room_next / room_previous (Function_Room.js L835-866). */
int gm_room_next(int room)
{
    int i = order_index(room);
    return (i >= 0 && i + 1 < s_order_count) ? s_order[i + 1] : GM_ROOM_NONE;
}

int gm_room_previous(int room)
{
    int i = order_index(room);
    return i > 0 ? s_order[i - 1] : GM_ROOM_NONE;
}

/* ---- Switching ----------------------------------------------------------------------- */

int gm_room_current(void)
{
    return s_current;
}

int gm_room_pending(void)
{
    return s_pending;
}

bool gm_room_event_blocked(const gm_instance_t *inst, int type)
{
    if (s_pending == GM_ROOM_NONE || type == GM_EVENT_TYPE_CLEAN_UP) {
        return false;
    }
    if ((inst != NULL && inst->persistent) || s_persistent) {
        return !(type == GM_EVENT_TYPE_CREATE || type == GM_EVENT_TYPE_PRE_CREATE ||
                 type == GM_EVENT_TYPE_DESTROY || type == GM_EVENT_TYPE_ALARM ||
                 type == GM_EVENT_TYPE_OTHER);
    }
    return true;
}

/* room_goto (Function_Room.js L798): ErrorOnce for invalid rooms. */
bool gm_room_goto(int room)
{
    if (!gm_room_exists(room)) {
        s_errors++;
        return false;
    }
    s_pending = room;
    return true;
}

void gm_room_game_end(void)
{
    s_pending = GM_ROOM_END_GAME;
}

void gm_room_game_restart(void)
{
    s_pending = GM_ROOM_RESTART_GAME;
}

void gm_room_restart(void)
{
    if (s_current != GM_ROOM_NONE) {
        s_pending = s_current;
    }
}

/* room_goto_next / _previous use the current room's position in the room order. */
void gm_room_goto_next(void)
{
    int next = gm_room_next(s_current);

    if (next != GM_ROOM_NONE) {
        s_pending = next;
    }
}

void gm_room_goto_previous(void)
{
    int prev = gm_room_previous(s_current);

    if (prev != GM_ROOM_NONE) {
        s_pending = prev;
    }
}

void gm_room_perform_event_all(int type, int number)
{
    uint32_t pass;
    int i;

    if (s_hooks == NULL || s_hooks->perform == NULL) {
        return;
    }
    pass = gm_instance_pass_begin();
    for (i = gm_instance_active_count() - 1; i >= 0; --i) {
        gm_instance_t *inst;
        if (i >= gm_instance_active_count()) {
            continue; /* the list shrank under us */
        }
        inst = gm_instance_active_at(i);
        if (inst != NULL && !inst->marked_for_destroy && gm_instance_in_pass(inst, pass) &&
            !gm_room_event_blocked(inst, type)) {
            s_hooks->perform(inst, type, number);
        }
    }
}

static void perform(gm_instance_t *inst, int type, int number)
{
    if (s_hooks != NULL && s_hooks->perform != NULL && !gm_room_event_blocked(inst, type)) {
        s_hooks->perform(inst, type, number);
    }
}

/* Steps 1-2: room end, carry persistent instances, delete everything else. */
static void end_room(void)
{
    int i;

    gm_instance_reclaim_marked();
    gm_room_perform_event_all(GM_EVENT_TYPE_OTHER, GM_EVENT_OTHER_ROOM_END);

    s_carry_count = 0;
    memset(s_keep, 0, sizeof(s_keep));
    for (i = gm_instance_active_count() - 1; i >= 0; --i) {
        gm_instance_t *inst = gm_instance_active_at(i);
        if (inst->persistent) {
            s_carry[s_carry_count++] = inst;
            s_keep[gm_instance_slot(inst)] = true;
        }
    }
    for (i = 0; i < s_carry_count; ++i) {
        gm_instance_active_detach(s_carry[i]);
    }
    for (i = 0; i < GM_INSTANCE_MAX; ++i) {
        gm_instance_t *inst = gm_instance_at_slot(i);
        if (inst != NULL && !s_keep[i]) {
            gm_instance_remove_now(inst);
        }
    }
    gm_view_room_end();
    gm_layer_room_end();
}

static bool carried(int id)
{
    int i;

    for (i = 0; i < s_carry_count; ++i) {
        if (s_carry[i]->id == id) {
            return true;
        }
    }
    return false;
}

/* yyRoom.CreateInstance (yyRoom.js L739) + the layer element's depth (Function_Layers.js L734). */
static gm_instance_t *create_storage_instance(const gm_room_def_t *r, const gm_room_inst_def_t *d)
{
    gm_instance_t *inst = gm_instance_add_with_id(d->x, d->y, (float)r->layers[d->layer].depth, d->object, d->id);

    if (inst == NULL) {
        s_overflows++;
        return NULL;
    }
    inst->image_speed = d->image_speed;
    inst->image_index = d->image_index;
    inst->image_xscale = d->xscale;
    inst->image_yscale = d->yscale;
    inst->image_angle = d->angle;
    inst->image_blend = d->colour & 0xFFFFFFu;
    inst->image_alpha = (float)((d->colour >> 24) & 0xFFu) / 255.0f;
    gm_collision_touch(inst);
    return inst;
}

bool gm_room_start(int room, bool starting)
{
    const gm_room_def_t *r = gm_room_get(room);
    int i;

    if (r == NULL) {
        s_errors++;
        return false;
    }
    /* New_Room is cleared before Room End so that a room_goto there is kept. */
    s_pending = GM_ROOM_NONE;
    if (s_current != GM_ROOM_NONE) {
        end_room();
    } else {
        s_carry_count = 0;
    }

    /* Step 3: the new room (CreateRoomFromStorage, yyRoom.js L532). */
    s_current = room;
    s_width = (float)r->width;
    s_height = (float)r->height;
    s_speed = (float)r->speed;
    s_persistent = r->persistent;
    s_caption[0] = '\0';
    gm_collision_grid_configure(s_width, s_height, 32.0f);
    gm_view_room_start(room, r);
    gm_layer_room_start(r);
    if (s_hooks != NULL && s_hooks->room_begin != NULL) {
        s_hooks->room_begin(room);
    }

    for (i = 0; i < r->instance_count; ++i) {
        const gm_room_inst_def_t *d = &r->instances[i];
        s_created[i] = carried(d->id) ? NULL : create_storage_instance(r, d);
    }
    for (i = 0; i < r->instance_count; ++i) {
        const gm_room_inst_def_t *d = &r->instances[i];
        gm_instance_t *inst = s_created[i];
        gm_event_fn create;

        /* g_pInstanceManager.Get(id): still allocated (it may be marked by now). */
        if (inst == NULL || gm_instance_find_by_id(d->id) != inst) {
            continue;
        }
        perform(inst, GM_EVENT_TYPE_PRE_CREATE, 0);
        create = gm_object_find_event(inst->object_index, GM_EV_CREATE, 0);
        if (create != NULL && !gm_room_event_blocked(inst, GM_EVENT_TYPE_CREATE)) {
            create(inst, inst);
        }
        if (d->code != NULL) {
            d->code(inst, inst);
        }
    }

    /* Step 4 */
    for (i = 0; i < s_carry_count; ++i) {
        gm_instance_active_attach(s_carry[i]);
    }
    s_carry_count = 0;

    /* Step 5 */
    if (starting) {
        gm_room_perform_event_all(GM_EVENT_TYPE_OTHER, GM_EVENT_OTHER_GAME_START);
    }
    if (r->code != NULL) {
        gm_instance_init_detached(&s_dummy);
        r->code(&s_dummy, &s_dummy);
    }
    gm_room_perform_event_all(GM_EVENT_TYPE_OTHER, GM_EVENT_OTHER_ROOM_START);
    return true;
}

bool gm_room_start_game(void)
{
    int first = gm_room_first();

    if (first == GM_ROOM_NONE) {
        return false;
    }
    if (gm_instance_next_id() < GM_ROOM_FIRST_DYNAMIC_ID) {
        gm_instance_set_next_id(GM_ROOM_FIRST_DYNAMIC_ID);
    }
    return gm_room_start(first, true);
}

bool gm_room_switch_pending(void)
{
    if (!gm_room_exists(s_pending)) {
        return false;
    }
    return gm_room_start(s_pending, false);
}

bool gm_room_restart_game(void)
{
    int i;

    /* Run_EndGame (_GameMaker.js L1616): everything goes, no events. */
    for (i = 0; i < GM_INSTANCE_MAX; ++i) {
        gm_instance_remove_now(gm_instance_at_slot(i));
    }
    if (s_current != GM_ROOM_NONE) {
        gm_view_room_end();
        gm_layer_room_end();
    }
    gm_view_reset_all();
    s_current = GM_ROOM_NONE;
    s_pending = GM_ROOM_NONE;
    return gm_room_start_game();
}

/* ---- Room built-ins ------------------------------------------------------------------ */

float gm_room_width(void)
{
    return s_width;
}

float gm_room_height(void)
{
    return s_height;
}

float gm_room_speed(void)
{
    return s_speed;
}

bool gm_room_persistent(void)
{
    return s_persistent;
}

const char *gm_room_caption(void)
{
    return s_caption;
}

void gm_room_set_width(float w)
{
    s_width = w;
}

void gm_room_set_height(float h)
{
    s_height = h;
}

void gm_room_set_speed(float s)
{
    s_speed = s;
}

void gm_room_set_persistent(bool p)
{
    s_persistent = p;
}

void gm_room_set_caption(const char *caption)
{
    size_t n = caption != NULL ? strlen(caption) : 0;

    if (n >= sizeof(s_caption)) {
        n = sizeof(s_caption) - 1;
    }
    if (n > 0) {
        memcpy(s_caption, caption, n);
    }
    s_caption[n] = '\0';
}

long gm_room_errors(void)
{
    return s_errors;
}

long gm_room_overflows(void)
{
    return s_overflows;
}
