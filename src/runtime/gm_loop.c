/*
 * gm_loop - see gm_loop.h. File references are GameMaker-HTML5 scripts.
 */
#include "gm_loop.h"

#include "gm_collision.h"
#include "gm_draw.h"
#include "gm_input.h"
#include "gm_layer.h"
#include "gm_math.h"
#include "gm_object.h"
#include "gm_perf.h"
#include "gm_room.h"
#include "gm_sprite.h"
#include "gm_view.h"

#include <math.h>
#include <stddef.h>

typedef struct pair {
    int a, b;   /* g_pCollisionList[a][b] */
} pair_t;

static const gm_loop_hooks_t *s_hooks;
static pair_t s_pairs[GM_LOOP_MAX_PAIRS];
static int s_pair_count;
static bool s_draw_enabled = true;
static long s_frames;

/* Which key event types any object handles: lets the keyboard phase skip scans. */
static bool s_has_key_type[3];

/* Which draw event numbers any object handles (inherited): passes nobody handles
 * are skipped (except Draw, which also does the default draw and layers). */
static const int s_draw_numbers[8] = { GM_EV_DRAW_NORMAL,    GM_EV_DRAW_GUI,      GM_EV_DRAW_BEGIN,
                                       GM_EV_DRAW_END,       GM_EV_DRAW_GUI_BEGIN, GM_EV_DRAW_GUI_END,
                                       GM_EV_DRAW_PRE,       GM_EV_DRAW_POST };
static bool s_has_draw[8];

/* Scratch: GetRPool snapshots and the draw order. */
static uint16_t s_pool1[GM_INSTANCE_MAX];
static uint16_t s_pool2[GM_INSTANCE_MAX];
static gm_instance_t *s_draw[GM_INSTANCE_MAX];

/* ---- collision pairs (LoadGame.js CreateCollisionArrays L717 / AddCollision L636) ---- */

static bool s_done[GM_OBJECT_MAX];
static bool s_pairs_ok;

static bool listed(int a, int b)
{
    int i;

    for (i = 0; i < s_pair_count; ++i) {
        if (s_pairs[i].a == a && s_pairs[i].b == b) {
            return true;
        }
    }
    return false;
}

static void add_collision(int id1)
{
    int id2, count = gm_object_count();

    if (s_done[id1]) {
        return;
    }
    if (gm_object_get_parent(id1) != GM_OBJECT_NONE) {
        add_collision(gm_object_get_parent(id1));
    }
    /* for (ID2 in pObj.Collisions): ascending object index */
    for (id2 = 0; id2 < count; ++id2) {
        bool found = false;
        int check;

        if (s_hooks->find_own_event(id1, GM_EV_TYPE_COLLISION, id2) == NULL || listed(id2, id1)) {
            continue;
        }
        for (check = id1; check != GM_OBJECT_NONE && !found; check = gm_object_get_parent(check)) {
            int check2;
            for (check2 = id2; check2 != GM_OBJECT_NONE; check2 = gm_object_get_parent(check2)) {
                if (listed(check2, check)) {
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            if (s_pair_count >= GM_LOOP_MAX_PAIRS) {
                s_pairs_ok = false;
                return;
            }
            s_pairs[s_pair_count].a = id1;
            s_pairs[s_pair_count].b = id2;
            s_pair_count++;
        }
    }
    s_done[id1] = true;
}

/* for (id1 in list) / for (coll2 in list[id1]): both ascending (integer keys). */
static void sort_pairs(void)
{
    int i, j;

    for (i = 1; i < s_pair_count; ++i) {
        pair_t p = s_pairs[i];
        for (j = i - 1; j >= 0 && (s_pairs[j].a > p.a || (s_pairs[j].a == p.a && s_pairs[j].b > p.b)); --j) {
            s_pairs[j + 1] = s_pairs[j];
        }
        s_pairs[j + 1] = p;
    }
}

bool gm_loop_init(const gm_loop_hooks_t *hooks)
{
    static const int key_types[3] = { GM_EV_TYPE_KEYBOARD, GM_EV_TYPE_KEYPRESS, GM_EV_TYPE_KEYRELEASE };
    int o, t, k, count = gm_object_count();

    s_hooks = NULL;
    s_pair_count = 0;
    s_frames = 0;
    s_draw_enabled = true;
    if (hooks == NULL || hooks->find_event == NULL || hooks->find_own_event == NULL) {
        return false;
    }
    s_hooks = hooks;
    s_pairs_ok = true;
    for (o = 0; o < count; ++o) {
        s_done[o] = false;
    }
    for (o = 0; o < count && s_pairs_ok; ++o) {
        if (gm_object_get_parent(o) != GM_OBJECT_NONE) {
            add_collision(gm_object_get_parent(o));
        }
        add_collision(o);
    }
    if (!s_pairs_ok) {
        s_hooks = NULL;
        s_pair_count = 0;
        return false;
    }
    sort_pairs();
    for (t = 0; t < 8; ++t) {
        s_has_draw[t] = false;
        for (o = 0; o < count && !s_has_draw[t]; ++o) {
            s_has_draw[t] = hooks->find_event(o, GM_EV_TYPE_DRAW, s_draw_numbers[t]) != NULL;
        }
    }
    for (t = 0; t < 3; ++t) {
        s_has_key_type[t] = false;
        for (o = 0; o < count && !s_has_key_type[t]; ++o) {
            for (k = 0; k < GM_INPUT_MAX_KEYS; ++k) {
                if (hooks->find_own_event(o, key_types[t], k) != NULL) {
                    s_has_key_type[t] = true;
                    break;
                }
            }
        }
    }
    return true;
}

int gm_loop_pair_count(void)
{
    return s_pair_count;
}

bool gm_loop_pair(int index, int *object1, int *object2)
{
    if (index < 0 || index >= s_pair_count) {
        return false;
    }
    *object1 = s_pairs[index].a;
    *object2 = s_pairs[index].b;
    return true;
}

void gm_loop_set_draw_enabled(bool enabled)
{
    s_draw_enabled = enabled;
}

long gm_loop_frame_count(void)
{
    return s_frames;
}

/* ---- dispatch ------------------------------------------------------------------------ */

static bool pending(void)
{
    return gm_room_pending() != GM_ROOM_NONE;
}

static gm_event_fn handler(const gm_instance_t *inst, int type, int number)
{
    return s_hooks->find_event(inst->object_index, type, number);
}

/* yyInstance.PerformEvent with self == other == inst. */
static void perform(gm_instance_t *inst, int type, int number)
{
    gm_event_fn fn = handler(inst, type, number);

    if (fn != NULL && !gm_room_event_blocked(inst, type)) {
        fn(inst, inst);
    }
}

void gm_loop_event_perform(gm_instance_t *self, gm_instance_t *other, int type, int number)
{
    gm_event_fn fn;

    if (s_hooks == NULL || self == NULL) {
        return;
    }
    fn = handler(self, type, number);
    if (fn != NULL && !gm_room_event_blocked(self, type)) {
        fn(self, other);
    }
}

/* yyInstanceManager.PerformEvent (yyInstance.js L3718): from the end of the active
 * list, skipping marked instances and those created during the pass. */
static void perform_all(int type, int number)
{
    uint32_t pass = gm_instance_pass_begin();
    int i;

    for (i = gm_instance_active_count() - 1; i >= 0; --i) {
        gm_instance_t *inst = gm_instance_active_at(i);
        if (inst != NULL && !inst->marked_for_destroy && gm_instance_in_pass(inst, pass)) {
            perform(inst, type, number);
        }
    }
}

/* Collision PerformEvent (yyInstance.js L1297): walk the other object's chain, and
 * for each of its levels this object's chain, until an object defines the pair. */
static void perform_collision(gm_instance_t *inst, gm_instance_t *other)
{
    int child, obj;

    if (gm_room_event_blocked(inst, GM_EV_TYPE_COLLISION)) {
        return;
    }
    for (child = other->object_index; child != GM_OBJECT_NONE; child = gm_object_get_parent(child)) {
        for (obj = inst->object_index; obj != GM_OBJECT_NONE; obj = gm_object_get_parent(obj)) {
            gm_event_fn fn = s_hooks->find_own_event(obj, GM_EV_TYPE_COLLISION, child);
            if (fn != NULL) {
                fn(inst, other);
                return;
            }
        }
    }
}

/* ---- phases ---------------------------------------------------------------------------- */

/* RememberOldPositions + Animate (yyInstance.js L3443, L2203). Every sprite of the
 * game plays one frame per game frame (the packer checks), so Animate is
 * image_index += image_speed whether or not the sprite exists. */
static void remember_old_positions(void)
{
    int i;

    for (i = 0; i < gm_instance_active_count(); ++i) {
        gm_instance_t *inst = gm_instance_active_at(i);
        inst->xprevious = inst->x;
        inst->yprevious = inst->y;
        inst->image_index += inst->image_speed;
    }
}

/* UpdateImages (yyInstance.js L3602): forward, live length, no create-counter check.
 * A missing sprite counts as 0 frames (GetImageCount returns null). */
static void update_images(void)
{
    int i;

    for (i = 0; i < gm_instance_active_count(); ++i) {
        gm_instance_t *inst = gm_instance_active_at(i);
        const gm_sprite_def_t *spr;
        float num;

        if (inst->marked_for_destroy || !inst->active) {
            continue;
        }
        spr = gm_sprite_get(inst->sprite_index);
        num = spr != NULL ? (float)spr->frame_count : 0.0f;
        if (inst->image_index >= num) {
            inst->image_index -= num;
            perform(inst, GM_EV_TYPE_OTHER, GM_EV_OTHER_ANIMATION_END);
        } else if (inst->image_index < 0.0f) {
            inst->image_index += num;
            perform(inst, GM_EV_TYPE_OTHER, GM_EV_OTHER_ANIMATION_END);
        }
    }
}

/* HandleAlarm (Events.js L641): forward; an alarm ticks only if the object (or a
 * parent) handles it; it fires when the decrement reaches 0. */
static void handle_alarms(void)
{
    uint32_t pass = gm_instance_pass_begin();
    int i, a;

    for (i = 0; i < gm_instance_active_count(); ++i) {
        gm_instance_t *inst = gm_instance_active_at(i);

        if (inst->marked_for_destroy || !gm_instance_in_pass(inst, pass)) {
            continue;
        }
        for (a = 0; a < GM_ALARM_COUNT; ++a) {
            gm_event_fn fn;
            int32_t al;

            if (inst->alarm[a] < 0.0f) {
                continue;
            }
            fn = handler(inst, GM_EV_TYPE_ALARM, a);
            if (fn == NULL) {
                continue;
            }
            al = gm_to_int32(inst->alarm[a]);
            if (al >= 0) {
                al--;
                inst->alarm[a] = (float)al;
            }
            if (al == 0 && !gm_room_event_blocked(inst, GM_EV_TYPE_ALARM)) {
                fn(inst, inst);
            }
        }
    }
}

/* HandleKeyDown / EventHandleKeyPressed / HandleKeyReleased (yyIOManager.js L1921-2003):
 * forward over the list length taken at the start; only Key Press skips marked. */
static void key_event(int type, int key, bool skip_marked)
{
    int i, n = gm_instance_active_count();

    for (i = 0; i < n; ++i) {
        gm_instance_t *inst = gm_instance_active_at(i);
        if (inst == NULL || (skip_marked && inst->marked_for_destroy)) {
            continue;
        }
        perform(inst, type, key);
    }
}

/* IO_HandleKeyDown / _KeyPressed / _KeyReleased (yyIOManager.js L2013-2084): keys
 * 2..255 that are set, then NOKEY (0) or ANYKEY (1). */
static void handle_keyboard(void)
{
    static const int types[3] = { GM_EV_TYPE_KEYBOARD, GM_EV_TYPE_KEYPRESS, GM_EV_TYPE_KEYRELEASE };
    int t, key;

    for (t = 0; t < 3; ++t) {
        int any = GM_VK_NOKEY;

        if (!s_has_key_type[t]) {
            continue;
        }
        for (key = 2; key < GM_INPUT_MAX_KEYS; ++key) {
            bool set = t == 0 ? gm_input_check(key) : t == 1 ? gm_input_check_pressed(key)
                                                             : gm_input_check_released(key);
            if (set) {
                any = GM_VK_ANYKEY;
                key_event(types[t], key, t == 1);
            }
        }
        key_event(types[t], any, t == 1);
    }
}

/* UpdatePositions (yyInstance.js L3466): every active instance, forward. Paths are
 * not modelled (Adapt_Path returns false without a path). */
static void update_positions(void)
{
    int i;

    for (i = 0; i < gm_instance_active_count(); ++i) {
        gm_instance_t *inst = gm_instance_active_at(i);

        gm_instance_adapt_speed(inst);
        if (inst->hspeed != 0.0f || inst->vspeed != 0.0f) {
            inst->x += inst->hspeed;
            inst->y += inst->vspeed;
            gm_collision_touch(inst);
        }
    }
}

/* HandleOther (Events.js L94): Outside Room fires on the transition to outside;
 * Intersect Boundary fires every step the instance crosses the room edge. */
static void handle_other(void)
{
    uint32_t pass = gm_instance_pass_begin();
    float w = gm_room_width(), h = gm_room_height();
    int i;

    for (i = 0; i < gm_instance_active_count(); ++i) {
        gm_instance_t *inst = gm_instance_active_at(i);
        bool has_outside, has_boundary, boxed;

        if (inst->marked_for_destroy || !gm_instance_in_pass(inst, pass)) {
            continue;
        }
        has_outside = handler(inst, GM_EV_TYPE_OTHER, GM_EV_OTHER_OUTSIDE) != NULL;
        has_boundary = handler(inst, GM_EV_TYPE_OTHER, GM_EV_OTHER_BOUNDARY) != NULL;
        if (!has_outside && !has_boundary) {
            continue;
        }
        boxed = gm_sprite_valid(inst->sprite_index) || gm_sprite_valid(inst->mask_index);
        if (has_outside) {
            bool outside;
            if (boxed) {
                gm_collision_update_bbox(inst);
                outside = inst->bbox_right < 0.0f || inst->bbox_left > w || inst->bbox_bottom < 0.0f ||
                          inst->bbox_top > h;
            } else {
                outside = inst->x < 0.0f || inst->x > w || inst->y < 0.0f || inst->y > h;
            }
            if (outside && !inst->outside_room) {
                perform(inst, GM_EV_TYPE_OTHER, GM_EV_OTHER_OUTSIDE);
            }
            inst->outside_room = outside;
        }
        if (has_boundary) {
            bool crossing;
            if (boxed) {
                gm_collision_update_bbox(inst);
                crossing = inst->bbox_left < 0.0f || inst->bbox_right > w || inst->bbox_top < 0.0f ||
                           inst->bbox_bottom > h;
            } else {
                crossing = inst->x < 0.0f || inst->x > w || inst->y < 0.0f || inst->y > h;
            }
            if (crossing) {
                perform(inst, GM_EV_TYPE_OTHER, GM_EV_OTHER_BOUNDARY);
            }
        }
    }
}

static bool collidable(const gm_instance_t *inst, uint32_t pass)
{
    return inst != NULL && !inst->marked_for_destroy && inst->active && gm_instance_in_pass(inst, pass);
}

static void revert(gm_instance_t *inst)
{
    inst->x = inst->xprevious;
    inst->y = inst->yprevious;
    gm_collision_touch(inst);
}

/* HandleCollision (Events.js L266). The recursive pools are read as the runner reads
 * them: pool 1 once per first object, pool 2 afresh for every instance of pool 1.
 * gm_instance_object_pool cannot overflow here: a pool holds at most every pool
 * slot, GM_INSTANCE_MAX. */
static void handle_collisions(void)
{
    int p = 0;

    gm_collision_grid_sync();
    while (p < s_pair_count) {
        int id1 = s_pairs[p].a, end = p, n1, i1, q;
        bool any_second = false;
        uint32_t pass;

        while (end < s_pair_count && s_pairs[end].a == id1) {
            end++;
        }
        /* Empty pools produce no candidates (and so no events): skip them without
         * walking the other side's list. */
        for (q = p; q < end && !any_second; ++q) {
            any_second = gm_instance_object_list_count(s_pairs[q].b) > 0;
        }
        if (!any_second || gm_instance_object_list_count(id1) == 0) {
            p = end;
            continue;
        }
        pass = gm_instance_pass_begin();
        n1 = gm_instance_object_pool(id1, s_pool1, GM_INSTANCE_MAX);
        for (i1 = 0; i1 < n1; ++i1) {
            gm_instance_t *inst1 = gm_instance_at_slot(s_pool1[i1]);

            if (!collidable(inst1, pass)) {
                continue;
            }
            for (q = p; q < end; ++q) {
                int n2, i2;

                if (gm_instance_object_list_count(s_pairs[q].b) == 0) {
                    continue;
                }
                n2 = gm_instance_object_pool(s_pairs[q].b, s_pool2, GM_INSTANCE_MAX);

                for (i2 = 0; i2 < n2; ++i2) {
                    gm_instance_t *inst2 = gm_instance_at_slot(s_pool2[i2]);

                    if (!collidable(inst2, pass)) {
                        continue;
                    }
                    if (inst1->object_index == inst2->object_index && i2 < i1) {
                        continue;
                    }
                    gm_perf_count(GM_PERF_AUTO_COLLISION_CANDIDATES, 1);
                    if (gm_collision_test_instance(inst1, inst2, true)) {
                        gm_perf_count(GM_PERF_AUTO_COLLISION_HITS, 1);
                        if (inst1->solid || inst2->solid) {
                            revert(inst1);
                            revert(inst2);
                        }
                        perform_collision(inst1, inst2);
                        perform_collision(inst2, inst1);
                    }
                }
            }
        }
        p = end;
    }
}

/* yyRoom.ExecuteDrawEvent (yyRoom.js L3729) over the layer order (gm_instance_draw_order:
 * depth descending, newest first within a layer). The order is taken when the pass
 * starts; instances destroyed, deactivated or hidden meanwhile are skipped. With
 * draw_default, instances without a handler get the default draw
 * (DrawLayerInstanceElement, yyRoom.js L1092). */
static bool any_draw_handler(int number)
{
    int t;

    for (t = 0; t < 8; ++t) {
        if (s_draw_numbers[t] == number) {
            return s_has_draw[t];
        }
    }
    return true;
}

static void draw_event(int number, bool draw_default)
{
    int i, n, layer;
    uint64_t perf_started;

    if (!draw_default && !any_draw_handler(number)) {
        return;
    }
    n = gm_instance_draw_order(s_draw, GM_INSTANCE_MAX);
    layer = gm_layer_count() - 1;   /* DrawRoomLayers: highest depth first */

    gm_perf_count(GM_PERF_DRAW_PASSES, 1);
    gm_perf_count(GM_PERF_DRAW_INSTANCES, (uint64_t)n);

    for (i = 0; i < n; ++i) {
        gm_instance_t *inst = s_draw[i];
        gm_event_fn fn;

        /* The normal pass interleaves the room's background and tile layers by depth
         * (a layer goes before instances of equal depth). */
        if (draw_default && layer >= 0 && layer < gm_layer_count() &&
            (float)gm_layer_at(layer)->depth >= floorf(inst->depth)) {
            perf_started = gm_perf_timer_begin();
            while (layer >= 0 && layer < gm_layer_count() &&
                   (float)gm_layer_at(layer)->depth >= floorf(inst->depth)) {
                gm_draw_layer(gm_layer_at(layer--));
            }
            gm_perf_timer_end(GM_PERF_TIMER_DRAW_LAYERS, perf_started);
        }
        if (!inst->in_use || inst->marked_for_destroy || !inst->active || !inst->visible) {
            continue;
        }
        fn = handler(inst, GM_EV_TYPE_DRAW, number);
        if (fn != NULL) {
            if (!gm_room_event_blocked(inst, GM_EV_TYPE_DRAW)) {
                perf_started = gm_perf_timer_begin();
                fn(inst, inst);
                gm_perf_timer_end(GM_PERF_TIMER_DRAW_HANDLERS, perf_started);
                gm_perf_count(GM_PERF_DRAW_HANDLER_CALLS, 1);
            }
        } else if (draw_default && s_hooks->draw_self != NULL) {
            perf_started = gm_perf_timer_begin();
            s_hooks->draw_self(inst);
            gm_perf_timer_end(GM_PERF_TIMER_DRAW_DEFAULT, perf_started);
            gm_perf_count(GM_PERF_DRAW_DEFAULT_CALLS, 1);
        }
    }
    if (draw_default && layer >= 0) {
        perf_started = gm_perf_timer_begin();
        while (layer >= 0 && layer < gm_layer_count()) {
            gm_draw_layer(gm_layer_at(layer--));
        }
        gm_perf_timer_end(GM_PERF_TIMER_DRAW_LAYERS, perf_started);
    }
}

/* DrawTheRoom (yyRoom.js L3541). */
static void draw_room(void)
{
    draw_event(GM_EV_DRAW_BEGIN, false);
    draw_event(GM_EV_DRAW_NORMAL, true);
    draw_event(GM_EV_DRAW_END, false);
}

/* yyRoom.Draw (yyRoom.js L4183): PreDraw, DrawViews (UpdateViews first), PostDraw,
 * DrawGUI. The GUI-mask Draw Begin/End passes only cover GUI layers (none here). */
static void draw(void)
{
    gm_draw_begin_frame();
    draw_event(GM_EV_DRAW_PRE, false);
    gm_view_update();   /* DrawViews -> UpdateViews, after PreDraw */
    if (gm_view_enabled()) {
        int v;
        for (v = 0; v < 8; ++v) {
            if (gm_view_get(GM_VIEW_VISIBLE, v) != 0.0f) {
                gm_view_set_current(v);
                gm_draw_pass_view(v);
                draw_room();
                break;
            }
        }
        /* The desktop runner leaves view_current at the last view slot (7) after this
         * loop and Spelunky HD's scrDrawHUD relies on it in Draw GUI ("condition3 =
         * (view_current==7)"; its comment calls the HTML5 path a hack). The HTML5
         * runner (yyRoom.js L3999) would leave the last visible view. */
        gm_view_set_current(7);
    } else {
        gm_view_set_current(0);
        gm_draw_pass_room();
        draw_room();
    }
    draw_event(GM_EV_DRAW_POST, false);
    gm_draw_pass_gui();
    draw_event(GM_EV_DRAW_GUI_BEGIN, false);
    draw_event(GM_EV_DRAW_GUI, false);
    draw_event(GM_EV_DRAW_GUI_END, false);
    gm_draw_end_frame();
}

void gm_loop_step(void)
{
    uint64_t t;

    if (s_hooks == NULL) {
        return;
    }
    s_frames++;

    /* Phases cut short by a room change are not timed (as zig/runtime/loop.zig). */
    t = gm_perf_timer_begin();
    gm_input_start_step();
    remember_old_positions();
    update_images();
    if (pending()) {
        return;
    }
    gm_perf_timer_end(GM_PERF_TIMER_IMAGES, t);

    t = gm_perf_timer_begin();
    gm_layer_update();
    perform_all(GM_EV_TYPE_STEP, GM_EV_STEP_BEGIN);
    if (pending()) {
        return;
    }
    gm_perf_timer_end(GM_PERF_TIMER_STEP_BEGIN, t);

    t = gm_perf_timer_begin();
    handle_alarms();
    if (pending()) {
        return;
    }
    gm_perf_timer_end(GM_PERF_TIMER_ALARMS, t);

    t = gm_perf_timer_begin();
    handle_keyboard();
    if (pending()) {
        return;
    }
    gm_perf_timer_end(GM_PERF_TIMER_KEYBOARD, t);

    t = gm_perf_timer_begin();
    perform_all(GM_EV_TYPE_STEP, GM_EV_STEP_NORMAL);
    if (pending()) {
        return;
    }
    gm_perf_timer_end(GM_PERF_TIMER_STEP_NORMAL, t);

    t = gm_perf_timer_begin();
    update_positions();
    gm_perf_timer_end(GM_PERF_TIMER_POSITIONS, t);
    t = gm_perf_timer_begin();
    handle_other();
    if (pending()) {
        return;
    }
    gm_perf_timer_end(GM_PERF_TIMER_OTHER, t);

    t = gm_perf_timer_begin();
    handle_collisions();
    if (pending()) {
        return;
    }
    gm_perf_timer_end(GM_PERF_TIMER_COLLISION, t);

    t = gm_perf_timer_begin();
    perform_all(GM_EV_TYPE_STEP, GM_EV_STEP_END);
    if (pending()) {
        return;
    }
    gm_perf_timer_end(GM_PERF_TIMER_STEP_END, t);

    t = gm_perf_timer_begin();
    gm_instance_reclaim_marked();
    gm_perf_timer_end(GM_PERF_TIMER_RECLAIM, t);
    if (s_draw_enabled) {
        t = gm_perf_timer_begin();
        draw();
        gm_perf_timer_end(GM_PERF_TIMER_DRAW, t);
    }
}

/* GameMaker_Tick L2373-2436 */
gm_loop_status_t gm_loop_tick(void)
{
    int steps;

    if (s_hooks == NULL) {
        return GM_LOOP_RUNNING;
    }
    for (steps = 0; steps < GM_LOOP_MAX_STEPS_PER_TICK; ++steps) {
        int next;
        uint64_t perf_started = gm_perf_timer_begin();

        gm_loop_step();
        gm_perf_timer_end(GM_PERF_TIMER_STEP, perf_started);
        next = gm_room_pending();
        if (next == GM_ROOM_NONE) {
            break;
        }
        if (next == GM_ROOM_END_GAME) {
            return GM_LOOP_ENDED;
        }
        if (next == GM_ROOM_RESTART_GAME) {
            gm_room_restart_game();
            break;
        }
        gm_room_switch_pending(); /* done = false: step again in the new room */
    }
    if (s_hooks->safe_point != NULL) {
        s_hooks->safe_point();
    }
    return GM_LOOP_RUNNING;
}
