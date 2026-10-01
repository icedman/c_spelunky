/*
 * gm_object - static object definitions (agents/SPELUNKY_C.md section 1.2) and the
 * registry used for parent/ancestor queries and event lookup.
 *
 * The generated game code provides the definition table; the runtime only
 * borrows it (no copies, no allocation).
 */
#ifndef GM_OBJECT_H
#define GM_OBJECT_H

#include <stdbool.h>

#define GM_ALARM_COUNT 12

/* Deepest parent chain supported (object + ancestors). Spelunky Classic HD's
 * deepest chain is 4 levels (e.g. oThwompTrap -> ... 3 parents). */
#define GM_OBJECT_MAX_LEVELS 8

#define GM_OBJECT_NONE (-1)

/* Upper bound on the registry size. Spelunky Classic HD has 464 objects. */
#define GM_OBJECT_MAX 1024

struct gm_instance;
typedef void (*gm_event_fn)(struct gm_instance *self, struct gm_instance *other);

typedef struct gm_object_def {
    const char *name;
    int parent_index; /* GM_OBJECT_NONE for root objects */
    int default_sprite;
    int default_mask;
    int default_depth;
    bool default_solid;
    bool default_visible;
    bool default_persistent;

    /* Event handlers (NULL = not defined on this object; inherited from the
     * parent chain at dispatch time). */
    gm_event_fn ev_create;
    gm_event_fn ev_destroy;
    gm_event_fn ev_step_begin;
    gm_event_fn ev_step;
    gm_event_fn ev_step_end;
    gm_event_fn ev_draw;
    gm_event_fn ev_alarms[GM_ALARM_COUNT];

    /* Collision dispatch table indexed by target object_index (may be NULL). */
    const gm_event_fn *collision_events;
} gm_object_def_t;

typedef enum gm_event_kind {
    GM_EV_CREATE = 0,
    GM_EV_DESTROY,
    GM_EV_STEP_BEGIN,
    GM_EV_STEP,
    GM_EV_STEP_END,
    GM_EV_DRAW,
    GM_EV_ALARM /* subtype = alarm number */
} gm_event_kind_t;

/* Installs the definition table (borrowed, must outlive its use). Validates
 * parent indices, rejects cycles and chains deeper than GM_OBJECT_MAX_LEVELS.
 * On failure the registry is left empty and false is returned. */
bool gm_object_registry_init(const gm_object_def_t *defs, int count);

int gm_object_count(void);
bool gm_object_valid(int object_index);

/* NULL for invalid indices. */
const gm_object_def_t *gm_object_get(int object_index);

/* object_get_parent: parent index, or -1 (Function_Object.js L171). */
int gm_object_get_parent(int object_index);

/* object_get_depth: default depth, or 0 for invalid objects (L117). */
int gm_object_get_depth(int object_index);

/* Number of ancestors (root objects are level 0). -1 if invalid. */
int gm_object_level(int object_index);

/* True if object_index == query or query is an ancestor of object_index. */
bool gm_object_is_a(int object_index, int query);

/* Nearest handler for `kind` on the object or its ancestors (GameMaker
 * event inheritance). Returns NULL if none. */
gm_event_fn gm_object_find_event(int object_index, gm_event_kind_t kind, int subtype);

#endif /* GM_OBJECT_H */
