/*
 * gm_object - see gm_object.h.
 */
#include "gm_object.h"

#include <stddef.h>



static const gm_object_def_t *s_defs;
static int s_count;
static signed char s_level[GM_OBJECT_MAX];

bool gm_object_registry_init(const gm_object_def_t *defs, int count)
{
    int i;

    s_defs = NULL;
    s_count = 0;
    if (count < 0 || count > GM_OBJECT_MAX || (count > 0 && defs == NULL)) {
        return false;
    }
    for (i = 0; i < count; ++i) {
        int p = defs[i].parent_index;
        int level = 0;

        while (p != GM_OBJECT_NONE) {
            if (p < 0 || p >= count) {
                return false;
            }
            if (++level >= GM_OBJECT_MAX_LEVELS) {
                return false; /* too deep, or a cycle */
            }
            p = defs[p].parent_index;
        }
        s_level[i] = (signed char)level;
    }
    s_defs = defs;
    s_count = count;
    return true;
}

int gm_object_count(void)
{
    return s_count;
}

bool gm_object_valid(int object_index)
{
    return object_index >= 0 && object_index < s_count;
}

const gm_object_def_t *gm_object_get(int object_index)
{
    return gm_object_valid(object_index) ? &s_defs[object_index] : NULL;
}

int gm_object_get_parent(int object_index)
{
    return gm_object_valid(object_index) ? s_defs[object_index].parent_index : -1;
}

int gm_object_get_depth(int object_index)
{
    return gm_object_valid(object_index) ? s_defs[object_index].default_depth : 0;
}

int gm_object_level(int object_index)
{
    return gm_object_valid(object_index) ? s_level[object_index] : -1;
}

bool gm_object_is_a(int object_index, int query)
{
    int o = object_index;

    if (!gm_object_valid(o) || !gm_object_valid(query)) {
        return false;
    }
    /* Ancestors always sit at a lower level, so bail out early. */
    if (s_level[query] > s_level[o]) {
        return false;
    }
    while (o != GM_OBJECT_NONE) {
        if (o == query) {
            return true;
        }
        o = s_defs[o].parent_index;
    }
    return false;
}

static gm_event_fn event_slot(const gm_object_def_t *d, gm_event_kind_t kind, int subtype)
{
    switch (kind) {
    case GM_EV_CREATE:
        return d->ev_create;
    case GM_EV_DESTROY:
        return d->ev_destroy;
    case GM_EV_STEP_BEGIN:
        return d->ev_step_begin;
    case GM_EV_STEP:
        return d->ev_step;
    case GM_EV_STEP_END:
        return d->ev_step_end;
    case GM_EV_DRAW:
        return d->ev_draw;
    case GM_EV_ALARM:
        return (subtype >= 0 && subtype < GM_ALARM_COUNT) ? d->ev_alarms[subtype] : NULL;
    default:
        return NULL;
    }
}

gm_event_fn gm_object_find_event(int object_index, gm_event_kind_t kind, int subtype)
{
    int o = object_index;

    if (!gm_object_valid(o)) {
        return NULL;
    }
    while (o != GM_OBJECT_NONE) {
        gm_event_fn fn = event_slot(&s_defs[o], kind, subtype);
        if (fn != NULL) {
            return fn;
        }
        o = s_defs[o].parent_index;
    }
    return NULL;
}
