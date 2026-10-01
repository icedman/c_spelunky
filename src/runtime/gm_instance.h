/*
 * gm_instance - instance pool, lifecycle, lookup and iteration.
 *
 * Semantics follow the GameMaker-HTML5 runner (yyInstance.js, yyRoom.js,
 * yyObject.js, Function_Instance.js) so that iteration order - which decides
 * RNG consumption and therefore level generation - matches the JS oracle:
 *
 *  - The ACTIVE list (yyRoom.m_Active) is kept in creation/activation order
 *    (a packing yyList: append on add, order-preserving removal). Despite the
 *    reference comment it is not depth sorted. The step pipeline walks it by
 *    index from the end (yyInstanceManager.PerformEvent, yyInstance.js L3718),
 *    which is why it is exposed as an indexable array.
 *  - Instances created during an event pass are skipped by that pass
 *    (createCounter, yyInstance.js L137 / L3726): see gm_instance_pass_begin.
 *  - Each object keeps an order-preserving list of its own and descendants'
 *    instances (yyObject.Instances_Recursive); with(obj), instance_exists,
 *    instance_number, instance_find and instance_nearest walk it.
 *  - instance_destroy runs the Destroy event immediately and only marks the
 *    instance; gm_instance_reclaim_marked removes marked instances later
 *    (Command_Destroy yyInstance.js L3807, yyRoom.RemoveMarked L4376).
 *  - Deactivated instances leave the active and per-object lists and join
 *    the DEACTIVE list; reactivation appends them again (yyRoom L4422-4450).
 *
 * Memory: a static pool of GM_INSTANCE_MAX instances; no allocation.
 *
 * Deviations from the agents/SPELUNKY_C.md section 1.1 sketch:
 *  - The active/deactive/all lists are ordered arrays (JS splice semantics)
 *    instead of prev/next links; per-object lists use one intrusive link per
 *    ancestor level so with(parent) sees child instances.
 *  - depth and alarms are doubles (GML reals; the depth setter takes reals).
 *  - speed/direction/hspeed/vspeed/depth must be written through the setters
 *    below, which reproduce the runner's coupled property setters.
 */
#ifndef GM_INSTANCE_H
#define GM_INSTANCE_H

#include "gm_object.h"
#include "gm_value.h"

#include <stdbool.h>
#include <stdint.h>

/* Game builds define GM_USE_GAME_CONFIG so that every translation unit sees
 * the variable counts the transpiler generated (gml_game_config.h, found on
 * the include path). All objects linked together must agree on them. */
#ifdef GM_USE_GAME_CONFIG
#include "gml_game_config.h"
#endif

#ifndef GM_INSTANCE_MAX
#define GM_INSTANCE_MAX 2048   /* hosts with little RAM (Playdate) may lower it */
#endif

/* First instance id handed out (GameMaker instance ids start at 100000). */
#define GM_INSTANCE_ID_BASE 100000

/* Built-in instance selectors (GameMaker constants). */
#define GM_SELF  (-1)
#define GM_OTHER (-2)
#define GM_ALL   (-3)
#define GM_NOONE (-4)

/* Per-instance user variable slots: `vars` holds the variables the
 * transpiler typed as real, `strs` those typed string, `vals` the rest
 * (arrays, mixed). The game build supplies the counts through
 * gml_game_config.h. */
#ifndef GM_VAR_COUNT
#define GM_VAR_COUNT 64
#endif
#ifndef GM_SVAR_COUNT
#define GM_SVAR_COUNT 8
#endif
#ifndef GM_VVAR_COUNT
#define GM_VVAR_COUNT 8
#endif

typedef struct gm_instance_link {
    struct gm_instance *prev;
    struct gm_instance *next;
} gm_instance_link_t;

typedef struct gm_instance {
    int id;           /* unique instance id (>= GM_INSTANCE_ID_BASE) */
    int object_index; /* index into the object registry */
    bool in_use;      /* pool slot allocated */
    bool active;      /* in the active list (false = deactivated) */
    bool marked_for_destroy;
    bool being_destroyed;

    /* Standard GameMaker built-in properties */
    double x, y;
    double xprevious, yprevious;
    double xstart, ystart;
    double hspeed, vspeed; /* write via gm_instance_set_* */
    double speed, direction;
    double friction, gravity;
    double gravity_direction;

    /* Animation & appearance */
    int sprite_index;
    double image_index;
    double image_speed;
    double image_xscale, image_yscale;
    double image_angle;
    double image_alpha;
    uint32_t image_blend;
    bool visible;
    bool solid;
    bool persistent;
    bool outside_room; /* fOutsideRoom: last Outside Room test (Events.js L134) */
    double depth; /* write via gm_instance_set_depth */

    /* Collision bounding box (maintained by the collision module) */
    double bbox_left, bbox_top, bbox_right, bbox_bottom;
    int mask_index;

    double alarm[GM_ALARM_COUNT];

    /* Runtime bookkeeping */
    uint32_t create_counter; /* event pass the instance was created in */
    uint32_t layer_serial;   /* draw order within a depth: higher = drawn first */
    /* Set whenever the instance is appended to its per-object lists (add or
     * activate), so ascending link_serial == per-object list order. */
    uint32_t link_serial;
    gm_instance_link_t type_links[GM_OBJECT_MAX_LEVELS];

    double vars[GM_VAR_COUNT];
    const char *strs[GM_SVAR_COUNT]; /* gm_heap roots; "" after add, never NULL */
    gm_value_t vals[GM_VVAR_COUNT];  /* gm_heap roots; zeroed = undefined */
} gm_instance_t;

/* ---- Lifecycle ------------------------------------------------------------ */

/* Destroys everything (no events) and restarts ids at GM_INSTANCE_ID_BASE.
 * Call after gm_object_registry_init. */
void gm_instance_system_reset(void);

/* Next id to hand out (room loading may reserve ids). */
void gm_instance_set_next_id(int id);

/* Adds an instance without running events (room-editor placement, copies).
 * Depth is truncated with ToInt32 as in GML_AddInstanceDepth. Returns NULL if
 * the object is invalid or the pool is full. */
gm_instance_t *gm_instance_add(double x, double y, double depth, int object_index);

/* gm_instance_add with a caller-chosen id (room instances carry fixed ids). Fails
 * (NULL) if the id is below GM_INSTANCE_ID_BASE or already in use. Does not change
 * the next id. */
gm_instance_t *gm_instance_add_with_id(double x, double y, double depth, int object_index, int id);

int gm_instance_next_id(void);

/* Removes an instance at once, without events, whether active or deactivated
 * (yyRoom.ClearInstances -> DeleteInstance, yyRoom.js L642). */
void gm_instance_remove_now(gm_instance_t *inst);

/* Room switching (_GameMaker.js StartRoom L1226-1256, L1451-1453): persistent
 * instances leave the active list (they stay in their object lists and keep their
 * pool position) while the new room's instances are created, then are appended
 * again. attach is a no-op for instances already in the list or deactivated. */
void gm_instance_active_detach(gm_instance_t *inst);
void gm_instance_active_attach(gm_instance_t *inst);

/* Initialises an instance that is not part of the pool (the dummy self of room
 * creation code, _GameMaker.js L1503): constructor defaults, id 0, not in use, so
 * no query or with() ever returns it. */
void gm_instance_init_detached(gm_instance_t *inst);

/* instance_create_depth: add, then run the Create event (inherited through
 * the parent chain) with self == other == the new instance
 * (Function_Layers.js L4817). */
gm_instance_t *gm_instance_create_depth(double x, double y, double depth, int object_index);

/* Spelunky's instance_create compatibility script:
 * instance_create_depth(x, y, object_get_depth(obj), obj). */
gm_instance_t *gm_instance_create(double x, double y, int object_index);

/* instance_destroy(): if the instance is active and not yet marked, run its
 * Destroy event once and mark it. Removal happens in reclaim. */
void gm_instance_destroy(gm_instance_t *inst);

/* Removes all marked instances (active or deactivated) and frees their
 * slots. Returns the number removed. */
int gm_instance_reclaim_marked(void);

/* ---- Lookup ---------------------------------------------------------------- */

int gm_instance_count(void); /* allocated instances, including marked/deactivated */

/* Raw id lookup (O(1)); returns marked/deactivated instances too. */
gm_instance_t *gm_instance_find_by_id(int id);

/* Resolves a GML instance-or-object value to an instance "usable" in the
 * GetWithArray sense (active and not marked): an instance id, GM_SELF,
 * GM_OTHER, or the first instance of an object. NULL if none. */
gm_instance_t *gm_instance_resolve(int target, gm_instance_t *self, gm_instance_t *other);

bool gm_instance_exists(int target, gm_instance_t *self, gm_instance_t *other);
int gm_instance_number(int target, gm_instance_t *self, gm_instance_t *other);
gm_instance_t *gm_instance_find(int target, int n, gm_instance_t *self, gm_instance_t *other);

/* instance_nearest (Function_Instance.js L235): the first instance with the
 * strictly smallest distance, in Instance_SearchLoop2 order. */
gm_instance_t *gm_instance_nearest(double x, double y, int target);

/* Pool slot of an instance (0 .. GM_INSTANCE_MAX-1) and the reverse map.
 * gm_instance_at_slot returns NULL for free slots. */
int gm_instance_slot(const gm_instance_t *inst);
gm_instance_t *gm_instance_at_slot(int slot);

/* ---- Instance_SearchLoop order (Globals.js L1464) ---------------------------
 * `all` walks the instance manager pool (creation order), an object walks its
 * recursive list, an id yields that instance. Only active, unmarked
 * instances are returned. Other selectors (self, other, noone) yield nothing,
 * as in the runner (object lookup of a negative index fails). The cursor is
 * live: callers must not add/remove instances while iterating (collision
 * queries never run user code). */
typedef struct gm_instance_cursor {
    int mode;
    gm_instance_t *node;
    int index;
    int list_level;
} gm_instance_cursor_t;

void gm_instance_search_begin(gm_instance_cursor_t *c, int target);
gm_instance_t *gm_instance_search_next(gm_instance_cursor_t *c);

/* ---- Lifecycle hooks -----------------------------------------------------------
 * Lets dependent modules (the collision grid) track the active set without
 * gm_instance depending on them. on_enter runs after an instance joins the
 * active lists (add - before its Create event - and activate); on_leave runs
 * before it leaves them (deactivate, and reclaim of active instances);
 * on_reset runs at the end of gm_instance_system_reset. */
typedef struct gm_instance_hooks {
    void (*on_reset)(void);
    void (*on_enter)(gm_instance_t *inst);
    void (*on_leave)(gm_instance_t *inst);
} gm_instance_hooks_t;

/* Borrowed; NULL removes the hooks. */
void gm_instance_set_hooks(const gm_instance_hooks_t *hooks);

/* ---- with() snapshots -------------------------------------------------------
 * Building blocks of gm_with (the with() iterator and context stack).
 *
 * gm_instance_with_snapshot writes the pool slots of the GetWithArray set
 * (yyObject.js L1599): `all` in creation order, an object's recursive list,
 * an id, self or other - active, unmarked instances only. Returns the count,
 * or -1 if more than `max` instances match (nothing usable is written then).
 *
 * gm_instance_with_slot returns the instance in a snapshot slot if it is
 * still usable (it may have been destroyed or deactivated since), else NULL. */
int gm_instance_with_snapshot(int target, gm_instance_t *self, gm_instance_t *other,
                              uint16_t *slots, int max);
gm_instance_t *gm_instance_with_slot(uint16_t slot);

/* yyObject.GetRPool: the pool slots of an object's recursive instance list in list
 * order, marked instances included (they stay listed until reclaim). Returns the
 * count, -1 if more than `max`, 0 for invalid objects. */
int gm_instance_object_pool(int object_index, uint16_t *slots, int max);

/* ---- Event-pass support ------------------------------------------------------
 * The step pipeline iterates the active list by index (from the end) exactly
 * like the runner; the list can shrink while iterating (deactivation), so
 * callers re-check the bound each step. */
int gm_instance_active_count(void);
gm_instance_t *gm_instance_active_at(int index);

/* Starts an event pass and returns its counter (g_currentCreateCounter++). */
uint32_t gm_instance_pass_begin(void);

/* True if the instance takes part in the pass started with `pass`. */
bool gm_instance_in_pass(const gm_instance_t *inst, uint32_t pass);

/* ---- Activation ---------------------------------------------------------------- */

void gm_instance_deactivate(gm_instance_t *inst);
void gm_instance_activate(gm_instance_t *inst);
void gm_instance_deactivate_object(int target);
/* Note: activate_object(all) is a no-op, matching the HTML5 runner
 * (Function_Instance.js L1678 compares an index against the pool array). */
void gm_instance_activate_object(int target);
void gm_instance_deactivate_all(bool notme, gm_instance_t *self);
void gm_instance_activate_all(void);

/* ---- Built-in property setters (yyInstance.js property setters) ---------------- */

void gm_instance_set_hspeed(gm_instance_t *inst, double v);
void gm_instance_set_vspeed(gm_instance_t *inst, double v);
void gm_instance_set_speed(gm_instance_t *inst, double v);
void gm_instance_set_direction(gm_instance_t *inst, double v);
void gm_instance_set_depth(gm_instance_t *inst, double depth);

/* yyInstance.AdaptSpeed (yyInstance.js L1210): friction, then gravity through
 * AddTo_Speed (L1197). The game loop applies it before moving by hspeed/vspeed. */
void gm_instance_adapt_speed(gm_instance_t *inst);

/* ---- Draw order ------------------------------------------------------------------- */

/* Fills `out` with active, unmarked instances in draw order: depth descending
 * (layers keyed by floor(depth)), and within a layer the most recently added
 * first (LayerManager.AddNewElement inserts at the front). Returns the count
 * (at most `max`). */
int gm_instance_draw_order(gm_instance_t **out, int max);

#endif /* GM_INSTANCE_H */
