/*
 * gml_api - hand-written bindings for the GML built-ins that the C runtime
 * implements (impl `runtime` in tools/gml2c/gml_builtins.py that are not
 * called directly). Prototypes come from the generated gml_api.h, so any
 * signature drift is a compile error.
 *
 * Conventions: instance results are returned as ids (noone when absent),
 * booleans as 0/1, strings borrowed from runtime storage (ds_map entries,
 * file read buffers) are copied into gm_heap before they reach GML code.
 * `const char *` parameters are already converted (yyGetString) by the
 * caller; they are passed through gml_s() so NULL reads as "".
 */
#include "gml_rt.h"

#include <math.h>
#include <string.h>

/* ------------------------------------------------------------------ helpers */

static float b2r(bool b)
{
    return b ? 1.0f : 0.0f;
}

/* Detaches a value from runtime-owned string storage. */
static gm_value_t own(gm_value_t v)
{
    if (v.kind == GM_VALUE_STRING && !gm_heap_owns(v)) {
        return gm_heap_string(v.str);
    }
    return v;
}

/* ------------------------------------------------------------------ maths */

float gml_fn_randomize(gm_instance_t *self, gm_instance_t *other)
{
    (void)self;
    (void)other;
    return (float)gm_randomize();
}

void gml_fn_move_snap(gm_instance_t *self, gm_instance_t *other, float a0, float a1)
{
    (void)other;
    if (self == NULL) {
        return;
    }
    if (a0 > 0.0f) {
        self->x = roundf(self->x / a0) * a0;
    }
    if (a1 > 0.0f) {
        self->y = roundf(self->y / a1) * a1;
    }
}

/* ------------------------------------------------------------------ instances */

float gml_fn_instance_create_depth(gm_instance_t *self, gm_instance_t *other, float a0, float a1,
                                    float a2, float a3)
{
    (void)self;
    (void)other;
    return gml_id(gm_instance_create_depth(a0, a1, a2, gml_target(a3)));
}

/* instance_destroy([id [, execute_event]]) (Function_Instance.js L1370): the
 * GetWithArray snapshot, like with(id). */
void gml_fn_instance_destroy(gm_instance_t *self, gm_instance_t *other, int argc, const gm_value_t *argv)
{
    gm_instance_t *p;

    if (argc >= 2 && !gm_value_to_bool(argv[1])) {
        /* Destroy without the Destroy event is not supported by gm_instance. */
        gml_pending_hit("instance_destroy(id, false)");
    }
    if (argc == 0) {
        gm_instance_destroy(self);
        return;
    }
    gm_with_begin(gml_target(gm_value_to_real(argv[0])), self, other);
    while ((p = gm_with_next()) != NULL) {
        gm_instance_destroy(p);
    }
    gm_with_end();
}

float gml_fn_instance_exists(gm_instance_t *self, gm_instance_t *other, float a0)
{
    return b2r(gm_instance_exists(gml_target(a0), self, other));
}

float gml_fn_instance_number(gm_instance_t *self, gm_instance_t *other, float a0)
{
    return (float)gm_instance_number(gml_target(a0), self, other);
}

float gml_fn_instance_find(gm_instance_t *self, gm_instance_t *other, float a0, float a1)
{
    return gml_id(gm_instance_find(gml_target(a0), (int)gm_to_int32(a1), self, other));
}

float gml_fn_instance_nearest(gm_instance_t *self, gm_instance_t *other, float a0, float a1, float a2)
{
    (void)self;
    (void)other;
    return gml_id(gm_instance_nearest(a0, a1, gml_target(a2)));
}

void gml_fn_instance_activate_all(gm_instance_t *self, gm_instance_t *other)
{
    (void)self;
    (void)other;
    gm_instance_activate_all();
}

void gml_fn_instance_activate_object(gm_instance_t *self, gm_instance_t *other, float a0)
{
    (void)self;
    (void)other;
    gm_instance_activate_object(gml_target(a0));
}

void gml_fn_instance_deactivate_all(gm_instance_t *self, gm_instance_t *other, float a0)
{
    (void)other;
    gm_instance_deactivate_all(gml_truthy(a0) != 0, self);
}

void gml_fn_instance_deactivate_object(gm_instance_t *self, gm_instance_t *other, float a0)
{
    (void)self;
    (void)other;
    gm_instance_deactivate_object(gml_target(a0));
}

float gml_fn_object_get_parent(gm_instance_t *self, gm_instance_t *other, float a0)
{
    (void)self;
    (void)other;
    return (float)gm_object_get_parent(gml_target(a0));
}

/* asset_get_index: objects, then sprites, then rooms; -1 when no asset has that name. */
float gml_fn_asset_get_index(gm_instance_t *self, gm_instance_t *other, const char *a0)
{
    const char *name = gml_s(a0);
    int i, n;

    (void)self;
    (void)other;
    for (i = 0, n = gm_object_count(); i < n; ++i) {
        const gm_object_def_t *def = gm_object_get(i);

        if (def != NULL && def->name != NULL && strcmp(def->name, name) == 0) {
            return (float)i;
        }
    }
    for (i = 0, n = gm_sprite_count(); i < n; ++i) {
        const gm_sprite_def_t *def = gm_sprite_get(i);

        if (def != NULL && def->name != NULL && strcmp(def->name, name) == 0) {
            return (float)i;
        }
    }
    for (i = 0, n = gm_room_count(); i < n; ++i) {
        if (strcmp(gm_room_get_name(i), name) == 0) {
            return (float)i;
        }
    }
    return -1.0f;
}

/* ------------------------------------------------------------------ collisions */

float gml_fn_place_meeting(gm_instance_t *self, gm_instance_t *other, float a0, float a1, float a2)
{
    (void)other;
    return b2r(gm_collision_place_meeting(self, a0, a1, gml_target(a2)));
}

float gml_fn_instance_place(gm_instance_t *self, gm_instance_t *other, float a0, float a1, float a2)
{
    (void)other;
    return gml_id(gm_collision_instance_place(self, a0, a1, gml_target(a2)));
}

float gml_fn_instance_position(gm_instance_t *self, gm_instance_t *other, float a0, float a1, float a2)
{
    (void)self;
    (void)other;
    return gml_id(gm_collision_instance_position(a0, a1, gml_target(a2)));
}

float gml_fn_collision_point(gm_instance_t *self, gm_instance_t *other, float a0, float a1, float a2,
                              float a3, float a4)
{
    (void)other;
    return gml_id(gm_collision_point(self, a0, a1, gml_target(a2), gml_truthy(a3) != 0, gml_truthy(a4) != 0));
}

float gml_fn_collision_rectangle(gm_instance_t *self, gm_instance_t *other, float a0, float a1, float a2,
                                  float a3, float a4, float a5, float a6)
{
    (void)other;
    return gml_id(gm_collision_rectangle(self, a0, a1, a2, a3, gml_target(a4), gml_truthy(a5) != 0,
                                         gml_truthy(a6) != 0));
}

float gml_fn_collision_line(gm_instance_t *self, gm_instance_t *other, float a0, float a1, float a2,
                             float a3, float a4, float a5, float a6)
{
    (void)other;
    return gml_id(gm_collision_line(self, a0, a1, a2, a3, gml_target(a4), gml_truthy(a5) != 0,
                                    gml_truthy(a6) != 0));
}

float gml_fn_distance_to_object(gm_instance_t *self, gm_instance_t *other, float a0)
{
    (void)other;
    return gm_collision_distance_to_object(self, gml_target(a0));
}

float gml_fn_distance_to_point(gm_instance_t *self, gm_instance_t *other, float a0, float a1)
{
    (void)other;
    return gm_collision_distance_to_point(self, a0, a1);
}

/* ------------------------------------------------------------------ ds_map */

float gml_fn_ds_map_create(gm_instance_t *self, gm_instance_t *other)
{
    (void)self;
    (void)other;
    return (float)gm_ds_map_create();
}

void gml_fn_ds_map_destroy(gm_instance_t *self, gm_instance_t *other, float a0)
{
    (void)self;
    (void)other;
    gm_ds_map_destroy(gml_target(a0));
}

float gml_fn_ds_map_exists(gm_instance_t *self, gm_instance_t *other, float a0, const char *a1)
{
    (void)self;
    (void)other;
    return b2r(gm_ds_map_exists(gml_target(a0), gml_s(a1)));
}

gm_value_t gml_fn_ds_map_find_first(gm_instance_t *self, gm_instance_t *other, float a0)
{
    (void)self;
    (void)other;
    return own(gm_ds_map_find_first(gml_target(a0)));
}

gm_value_t gml_fn_ds_map_find_next(gm_instance_t *self, gm_instance_t *other, float a0, const char *a1)
{
    (void)self;
    (void)other;
    return own(gm_ds_map_find_next(gml_target(a0), gml_s(a1)));
}

gm_value_t gml_fn_ds_map_find_value(gm_instance_t *self, gm_instance_t *other, float a0, const char *a1)
{
    (void)self;
    (void)other;
    return own(gm_ds_map_find_value(gml_target(a0), gml_s(a1)));
}

void gml_fn_ds_map_replace(gm_instance_t *self, gm_instance_t *other, float a0, const char *a1, gm_value_t a2)
{
    (void)self;
    (void)other;
    if (a2.kind == GM_VALUE_ARRAY) {
        gml_pending_hit("ds_map_replace(<array value>)");
        a2 = gm_value_undefined();
    }
    (void)gm_ds_map_replace(gml_target(a0), gml_s(a1), a2);
}

float gml_fn_ds_map_size(gm_instance_t *self, gm_instance_t *other, float a0)
{
    (void)self;
    (void)other;
    return (float)gm_ds_map_size(gml_target(a0));
}

/* ------------------------------------------------------------------ files */

float gml_fn_file_exists(gm_instance_t *self, gm_instance_t *other, const char *a0)
{
    (void)self;
    (void)other;
    return b2r(gm_file_exists(gml_s(a0)));
}

float gml_fn_file_delete(gm_instance_t *self, gm_instance_t *other, const char *a0)
{
    (void)self;
    (void)other;
    return b2r(gm_file_delete(gml_s(a0)));
}

float gml_fn_file_text_open_read(gm_instance_t *self, gm_instance_t *other, const char *a0)
{
    (void)self;
    (void)other;
    return (float)gm_file_text_open_read(gml_s(a0));
}

float gml_fn_file_text_open_write(gm_instance_t *self, gm_instance_t *other, const char *a0)
{
    (void)self;
    (void)other;
    return (float)gm_file_text_open_write(gml_s(a0));
}

void gml_fn_file_text_close(gm_instance_t *self, gm_instance_t *other, float a0)
{
    (void)self;
    (void)other;
    gm_file_text_close(gml_target(a0));
}

float gml_fn_file_text_eof(gm_instance_t *self, gm_instance_t *other, float a0)
{
    (void)self;
    (void)other;
    return b2r(gm_file_text_eof(gml_target(a0)));
}

const char *gml_fn_file_text_read_string(gm_instance_t *self, gm_instance_t *other, float a0)
{
    (void)self;
    (void)other;
    return gm_heap_str(gm_file_text_read_string(gml_target(a0)));
}

const char *gml_fn_file_text_readln(gm_instance_t *self, gm_instance_t *other, float a0)
{
    (void)self;
    (void)other;
    return gm_heap_str(gm_file_text_readln(gml_target(a0)));
}

void gml_fn_file_text_write_string(gm_instance_t *self, gm_instance_t *other, float a0, const char *a1)
{
    (void)self;
    (void)other;
    gm_file_text_write_string(gml_target(a0), gml_s(a1));
}

void gml_fn_file_text_writeln(gm_instance_t *self, gm_instance_t *other, float a0)
{
    (void)self;
    (void)other;
    gm_file_text_writeln(gml_target(a0));
}

void gml_fn_ini_open(gm_instance_t *self, gm_instance_t *other, const char *a0)
{
    (void)self;
    (void)other;
    gm_ini_open(gml_s(a0));
}

const char *gml_fn_ini_close(gm_instance_t *self, gm_instance_t *other)
{
    (void)self;
    (void)other;
    return gm_heap_str(gm_ini_close());
}

float gml_fn_ini_read_real(gm_instance_t *self, gm_instance_t *other, const char *a0, const char *a1, float a2)
{
    (void)self;
    (void)other;
    return gm_ini_read_real(gml_s(a0), gml_s(a1), a2);
}

void gml_fn_ini_write_real(gm_instance_t *self, gm_instance_t *other, const char *a0, const char *a1, float a2)
{
    (void)self;
    (void)other;
    (void)gm_ini_write_real(gml_s(a0), gml_s(a1), a2);
}

void gml_fn_ini_write_string(gm_instance_t *self, gm_instance_t *other, const char *a0, const char *a1,
                             const char *a2)
{
    (void)self;
    (void)other;
    (void)gm_ini_write_string(gml_s(a0), gml_s(a1), gml_s(a2));
}

/* ------------------------------------------------------------------ strings / sprites */

const char *gml_fn_string(gm_instance_t *self, gm_instance_t *other, gm_value_t a0)
{
    (void)self;
    (void)other;
    return gml_as_str(a0);
}

float gml_fn_sprite_exists(gm_instance_t *self, gm_instance_t *other, float a0)
{
    (void)self;
    (void)other;
    return b2r(gm_sprite_valid(gml_target(a0)));
}

/* ------------------------------------------------------------------ built-in variables */

const char *gml_gget_working_directory(void)
{
    return gml_s(gm_working_directory());
}

/* instance_count: g_RunRoom.m_Active.length. */
float gml_gget_instance_count(void)
{
    return (float)gm_instance_active_count();
}

/* yyInstance.js L509-561: sprite metrics scaled by the instance. */
float gml_iget_image_number(gm_instance_t *p)
{
    const gm_sprite_def_t *spr = p != NULL ? gm_sprite_get(p->sprite_index) : NULL;
    return spr != NULL ? (float)spr->frame_count : 0.0f;
}

float gml_iget_sprite_width(gm_instance_t *p)
{
    const gm_sprite_def_t *spr = p != NULL ? gm_sprite_get(p->sprite_index) : NULL;
    return spr != NULL ? (float)spr->width * p->image_xscale : 0.0f;
}

float gml_iget_sprite_height(gm_instance_t *p)
{
    const gm_sprite_def_t *spr = p != NULL ? gm_sprite_get(p->sprite_index) : NULL;
    return spr != NULL ? (float)spr->height * p->image_yscale : 0.0f;
}

float gml_iget_sprite_xoffset(gm_instance_t *p)
{
    const gm_sprite_def_t *spr = p != NULL ? gm_sprite_get(p->sprite_index) : NULL;
    return spr != NULL ? (float)spr->xorigin * p->image_xscale : 0.0f;
}

float gml_iget_sprite_yoffset(gm_instance_t *p)
{
    const gm_sprite_def_t *spr = p != NULL ? gm_sprite_get(p->sprite_index) : NULL;
    return spr != NULL ? (float)spr->yorigin * p->image_yscale : 0.0f;
}
