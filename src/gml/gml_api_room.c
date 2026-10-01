/*
 * gml_api_room - bindings for the room, view, camera and layer built-ins (slice 8).
 * Prototypes come from the generated gml_api.h; see gml_api.c for the conventions.
 *
 * References: Function_Room.js (room_*), Function_Graphics.js L2876-2930 (view_*),
 * CameraManager.js L704-1148 (camera_*), Function_Layers.js (layer_*). Argument
 * conversions follow the runner (yyGetInt32 for ids, ports and flags; yyGetReal for
 * positions); "layer" arguments are an id or, for strings, a layer name.
 */
#include "gml_rt.h"

#include <string.h>

static double b2r(bool b)
{
    return b ? 1.0 : 0.0;
}

static int i32(double d)
{
    return (int)gm_to_int32(d);
}

/* layerGetObj (Function_Layers.js L2317). */
static gm_layer_t *layer_of(gm_value_t v)
{
    if (v.kind == GM_VALUE_STRING) {
        return gm_layer_find_name(v.str);
    }
    return gm_layer_find(i32(gm_value_to_real(v)));
}

/* A GML array of reals. */
static gm_value_t real_array(const int *items, int n)
{
    gm_value_t a = gm_array_new(n);
    int i;

    for (i = 0; i < n && gm_array_valid(a); ++i) {
        gm_array_set(&a, i, gm_value_real((double)items[i]));
    }
    return a;
}

/* ------------------------------------------------------------------ rooms */

void gml_fn_room_goto(gm_instance_t *self, gm_instance_t *other, double a0)
{
    (void)self;
    (void)other;
    (void)gm_room_goto(i32(a0));
}

void gml_fn_room_goto_next(gm_instance_t *self, gm_instance_t *other)
{
    (void)self;
    (void)other;
    gm_room_goto_next();
}

void gml_fn_room_restart(gm_instance_t *self, gm_instance_t *other)
{
    (void)self;
    (void)other;
    gm_room_restart();
}

double gml_fn_room_exists(gm_instance_t *self, gm_instance_t *other, double a0)
{
    (void)self;
    (void)other;
    return b2r(gm_room_exists(i32(a0)));
}

const char *gml_fn_room_get_name(gm_instance_t *self, gm_instance_t *other, double a0)
{
    (void)self;
    (void)other;
    return gm_room_get_name(i32(a0)); /* static table strings */
}

double gml_fn_room_get_camera(gm_instance_t *self, gm_instance_t *other, double a0, double a1)
{
    (void)self;
    (void)other;
    return (double)gm_view_room_camera(i32(a0), i32(a1));
}

void gml_fn_room_set_camera(gm_instance_t *self, gm_instance_t *other, double a0, double a1, double a2)
{
    (void)self;
    (void)other;
    gm_view_room_set_camera(i32(a0), i32(a1), i32(a2));
}

void gml_fn_room_set_viewport(gm_instance_t *self, gm_instance_t *other, double a0, double a1, double a2,
                              double a3, double a4, double a5, double a6)
{
    (void)self;
    (void)other;
    gm_view_room_set_viewport(i32(a0), i32(a1), gml_truthy(a2) != 0, i32(a3), i32(a4), i32(a5), i32(a6));
}

/* room (get_current_room / set_current_room = room_goto, yyBuiltIn.js L194). */
double gml_gget_room(void)
{
    return (double)gm_room_current();
}

void gml_gset_room(double v)
{
    (void)gm_room_goto(i32(v));
}

double gml_gget_room_first(void)
{
    return (double)gm_room_first();
}

double gml_gget_room_last(void)
{
    return (double)gm_room_last();
}

double gml_gget_room_width(void)
{
    return gm_room_width();
}

void gml_gset_room_width(double v)
{
    gm_room_set_width(v);
}

double gml_gget_room_height(void)
{
    return gm_room_height();
}

void gml_gset_room_height(double v)
{
    gm_room_set_height(v);
}

double gml_gget_room_speed(void)
{
    return gm_room_speed();
}

void gml_gset_room_speed(double v)
{
    gm_room_set_speed(v);
}

double gml_gget_room_persistent(void)
{
    return b2r(gm_room_persistent());
}

void gml_gset_room_persistent(double v)
{
    gm_room_set_persistent(gml_truthy(v) != 0);
}

const char *gml_gget_room_caption(void)
{
    return gm_room_caption();
}

void gml_gset_room_caption(const char *v)
{
    gm_room_set_caption(gml_s(v));
}

/* ------------------------------------------------------------------ views */

double gml_gget_view_enabled(void)
{
    return b2r(gm_view_enabled());
}

void gml_gset_view_enabled(double v)
{
    gm_view_set_enabled(gml_truthy(v) != 0);
}

double gml_gget_view_current(void)
{
    return (double)gm_view_current(); /* set while gm_loop draws each view */
}

#define VIEW_ARRAY(name, field)                                  \
    double gml_gget_##name(double i)                             \
    {                                                            \
        return gm_view_get(field, i32(i));                       \
    }                                                            \
    void gml_gset_##name(double i, double v)                     \
    {                                                            \
        gm_view_set(field, i32(i), v);                           \
    }

VIEW_ARRAY(view_visible, GM_VIEW_VISIBLE)
VIEW_ARRAY(view_xview, GM_VIEW_XVIEW)
VIEW_ARRAY(view_yview, GM_VIEW_YVIEW)
VIEW_ARRAY(view_wview, GM_VIEW_WVIEW)
VIEW_ARRAY(view_hview, GM_VIEW_HVIEW)
VIEW_ARRAY(view_angle, GM_VIEW_ANGLE)
VIEW_ARRAY(view_hborder, GM_VIEW_HBORDER)
VIEW_ARRAY(view_vborder, GM_VIEW_VBORDER)
VIEW_ARRAY(view_hspeed, GM_VIEW_HSPEED)
VIEW_ARRAY(view_vspeed, GM_VIEW_VSPEED)
VIEW_ARRAY(view_object, GM_VIEW_OBJECT)
VIEW_ARRAY(view_xport, GM_VIEW_XPORT)
VIEW_ARRAY(view_yport, GM_VIEW_YPORT)
VIEW_ARRAY(view_wport, GM_VIEW_WPORT)
VIEW_ARRAY(view_hport, GM_VIEW_HPORT)
VIEW_ARRAY(view_surface_id, GM_VIEW_SURFACE_ID)
VIEW_ARRAY(view_camera, GM_VIEW_CAMERA)

/* view_get_* / view_set_* read and write the arrays, with yyGetInt32 on writes. */
#define VIEW_GETSET(suffix, field)                                                         \
    double gml_fn_view_get_##suffix(gm_instance_t *self, gm_instance_t *other, double a0)  \
    {                                                                                      \
        (void)self;                                                                        \
        (void)other;                                                                       \
        return gm_view_get(field, i32(a0));                                                \
    }                                                                                      \
    void gml_fn_view_set_##suffix(gm_instance_t *self, gm_instance_t *other, double a0,    \
                                  double a1)                                               \
    {                                                                                      \
        (void)self;                                                                        \
        (void)other;                                                                       \
        gm_view_set(field, i32(a0), (double)i32(a1));                                      \
    }

VIEW_GETSET(camera, GM_VIEW_CAMERA)
VIEW_GETSET(visible, GM_VIEW_VISIBLE)
VIEW_GETSET(xport, GM_VIEW_XPORT)
VIEW_GETSET(yport, GM_VIEW_YPORT)
VIEW_GETSET(wport, GM_VIEW_WPORT)
VIEW_GETSET(hport, GM_VIEW_HPORT)
VIEW_GETSET(surface_id, GM_VIEW_SURFACE_ID)

/* ------------------------------------------------------------------ cameras */

/* camera_create_view(x, y, w, h [, angle, target, speedx, speedy, borderx, bordery]). */
double gml_fn_camera_create_view(gm_instance_t *self, gm_instance_t *other, int argc, const gm_value_t *argv)
{
    double a[10] = { 0, 0, 0, 0, 0, -1, -1, -1, 0, 0 };
    int i;

    (void)self;
    (void)other;
    for (i = 0; i < argc && i < 10; ++i) {
        a[i] = gm_value_to_real(argv[i]);
    }
    return (double)gm_camera_create_view(a[0], a[1], a[2], a[3], a[4], i32(a[5]), a[6], a[7], a[8], a[9]);
}

void gml_fn_camera_destroy(gm_instance_t *self, gm_instance_t *other, double a0)
{
    (void)self;
    (void)other;
    gm_camera_destroy(i32(a0));
}

#define CAMERA_GET(suffix, expr)                                                            \
    double gml_fn_camera_get_view_##suffix(gm_instance_t *self, gm_instance_t *other, double a0) \
    {                                                                                       \
        const gm_camera_t *c = gm_camera_get(i32(a0));                                      \
        (void)self;                                                                         \
        (void)other;                                                                        \
        return c != NULL ? (expr) : -1.0;                                                   \
    }

CAMERA_GET(x, c->x)
CAMERA_GET(y, c->y)
CAMERA_GET(width, c->w)
CAMERA_GET(height, c->h)
CAMERA_GET(angle, c->angle)
CAMERA_GET(border_x, c->border_x)
CAMERA_GET(border_y, c->border_y)
CAMERA_GET(speed_x, c->speed_x)
CAMERA_GET(speed_y, c->speed_y)
CAMERA_GET(target, (double)c->target)

#define CAMERA_SET2(suffix, fa, fb)                                                          \
    void gml_fn_camera_set_view_##suffix(gm_instance_t *self, gm_instance_t *other, double a0, \
                                         double a1, double a2)                               \
    {                                                                                        \
        gm_camera_t *c = gm_camera_get(i32(a0));                                             \
        (void)self;                                                                          \
        (void)other;                                                                         \
        if (c != NULL) {                                                                     \
            c->fa = a1;                                                                      \
            c->fb = a2;                                                                      \
        }                                                                                    \
    }

CAMERA_SET2(pos, x, y)
CAMERA_SET2(size, w, h)
CAMERA_SET2(border, border_x, border_y)
CAMERA_SET2(speed, speed_x, speed_y)

void gml_fn_camera_set_view_angle(gm_instance_t *self, gm_instance_t *other, double a0, double a1)
{
    gm_camera_t *c = gm_camera_get(i32(a0));

    (void)self;
    (void)other;
    if (c != NULL) {
        c->angle = a1;
    }
}

void gml_fn_camera_set_view_target(gm_instance_t *self, gm_instance_t *other, double a0, double a1)
{
    gm_camera_t *c = gm_camera_get(i32(a0));

    (void)self;
    (void)other;
    if (c != NULL) {
        c->target = i32(a1);
    }
}

/* ------------------------------------------------------------------ layers */

/* layer_create(depth [, name]) */
double gml_fn_layer_create(gm_instance_t *self, gm_instance_t *other, int argc, const gm_value_t *argv)
{
    (void)self;
    (void)other;
    if (argc < 1) {
        return -1.0;
    }
    return (double)gm_layer_create(i32(gm_value_to_real(argv[0])),
                                   argc >= 2 ? gml_as_str(argv[1]) : NULL);
}

void gml_fn_layer_destroy(gm_instance_t *self, gm_instance_t *other, gm_value_t a0)
{
    gm_layer_t *l = layer_of(a0);

    (void)self;
    (void)other;
    if (l != NULL) {
        gm_layer_destroy(l->id);
    }
}

void gml_fn_layer_depth(gm_instance_t *self, gm_instance_t *other, gm_value_t a0, double a1)
{
    gm_layer_t *l = layer_of(a0);

    (void)self;
    (void)other;
    if (l != NULL) {
        gm_layer_set_depth(l->id, i32(a1));
    }
}

double gml_fn_layer_get_depth(gm_instance_t *self, gm_instance_t *other, gm_value_t a0)
{
    gm_layer_t *l = layer_of(a0);

    (void)self;
    (void)other;
    return l != NULL ? (double)l->depth : -1.0;
}

const char *gml_fn_layer_get_name(gm_instance_t *self, gm_instance_t *other, gm_value_t a0)
{
    gm_layer_t *l = layer_of(a0);

    (void)self;
    (void)other;
    return l != NULL ? gm_heap_str(l->name) : ""; /* layer names can change: copy */
}

gm_value_t gml_fn_layer_get_all(gm_instance_t *self, gm_instance_t *other)
{
    int ids[GM_LAYER_MAX];

    (void)self;
    (void)other;
    return real_array(ids, gm_layer_get_all(ids, GM_LAYER_MAX));
}

gm_value_t gml_fn_layer_get_all_elements(gm_instance_t *self, gm_instance_t *other, gm_value_t a0)
{
    static int ids[GM_ELEMENT_MAX];
    gm_layer_t *l = layer_of(a0);

    (void)self;
    (void)other;
    return real_array(ids, l != NULL ? gm_layer_elements(l->id, ids, GM_ELEMENT_MAX) : 0);
}

double gml_fn_layer_get_element_type(gm_instance_t *self, gm_instance_t *other, double a0)
{
    (void)self;
    (void)other;
    return (double)gm_layer_element_type(i32(a0));
}

double gml_fn_layer_get_visible(gm_instance_t *self, gm_instance_t *other, gm_value_t a0)
{
    gm_layer_t *l = layer_of(a0);

    (void)self;
    (void)other;
    return l != NULL ? b2r(l->visible) : 0.0;
}

void gml_fn_layer_set_visible(gm_instance_t *self, gm_instance_t *other, gm_value_t a0, double a1)
{
    gm_layer_t *l = layer_of(a0);

    (void)self;
    (void)other;
    if (l != NULL) {
        l->visible = gml_truthy(a1) != 0;
    }
}

#define LAYER_FIELD(getter, setter, field)                                                  \
    double gml_fn_##getter(gm_instance_t *self, gm_instance_t *other, gm_value_t a0)        \
    {                                                                                       \
        gm_layer_t *l = layer_of(a0);                                                       \
        (void)self;                                                                         \
        (void)other;                                                                        \
        return l != NULL ? l->field : 0.0;                                                  \
    }                                                                                       \
    void gml_fn_##setter(gm_instance_t *self, gm_instance_t *other, gm_value_t a0, double a1) \
    {                                                                                       \
        gm_layer_t *l = layer_of(a0);                                                       \
        (void)self;                                                                         \
        (void)other;                                                                        \
        if (l != NULL) {                                                                    \
            l->field = a1;                                                                  \
        }                                                                                   \
    }

LAYER_FIELD(layer_get_x, layer_x, x)
LAYER_FIELD(layer_get_y, layer_y, y)
LAYER_FIELD(layer_get_hspeed, layer_hspeed, hspeed)
LAYER_FIELD(layer_get_vspeed, layer_vspeed, vspeed)

/* ------------------------------------------------------------------ backgrounds */

/* layerBackgroudGetElement (Function_Layers.js L2721). */
static gm_element_t *background(double id)
{
    gm_element_t *e = gm_layer_element(i32(id));
    return (e != NULL && e->type == GM_ELEMENT_BACKGROUND) ? e : NULL;
}

double gml_fn_layer_background_create(gm_instance_t *self, gm_instance_t *other, gm_value_t a0, double a1)
{
    gm_layer_t *l = layer_of(a0);

    (void)self;
    (void)other;
    return l != NULL ? (double)gm_layer_background_create(l->id, i32(a1)) : -1.0;
}

double gml_fn_layer_background_exists(gm_instance_t *self, gm_instance_t *other, gm_value_t a0, double a1)
{
    gm_layer_t *l = layer_of(a0);

    (void)self;
    (void)other;
    return b2r(l != NULL && gm_layer_background_exists(l->id, i32(a1)));
}

void gml_fn_layer_background_change(gm_instance_t *self, gm_instance_t *other, double a0, double a1)
{
    gm_element_t *e = background(a0);

    (void)self;
    (void)other;
    if (e != NULL) {
        e->sprite = i32(a1);
    }
}

#define BG_GET(suffix, expr, missing)                                                        \
    double gml_fn_layer_background_get_##suffix(gm_instance_t *self, gm_instance_t *other,   \
                                                 double a0)                                  \
    {                                                                                        \
        const gm_element_t *e = background(a0);                                              \
        (void)self;                                                                          \
        (void)other;                                                                         \
        return e != NULL ? (expr) : (missing);                                               \
    }

BG_GET(sprite, (double)e->sprite, -1.0)
BG_GET(index, e->image_index, -1.0)
BG_GET(alpha, e->alpha, 0.0)
BG_GET(blend, (double)e->blend, 0.0)
BG_GET(htiled, b2r(e->htiled), 0.0)
BG_GET(vtiled, b2r(e->vtiled), 0.0)
BG_GET(stretch, b2r(e->stretch), 0.0)
BG_GET(xscale, e->xscale, 1.0)
BG_GET(yscale, e->yscale, 1.0)

#define BG_SET(suffix, stmt)                                                                 \
    void gml_fn_layer_background_##suffix(gm_instance_t *self, gm_instance_t *other,         \
                                          double a0, double a1)                              \
    {                                                                                        \
        gm_element_t *e = background(a0);                                                    \
        (void)self;                                                                          \
        (void)other;                                                                         \
        if (e != NULL) {                                                                     \
            stmt;                                                                            \
        }                                                                                    \
    }

BG_SET(alpha, e->alpha = a1)
BG_SET(blend, e->blend = (uint32_t)i32(a1) & 0xFFFFFFu)
BG_SET(htiled, e->htiled = gml_truthy(a1) != 0)
BG_SET(vtiled, e->vtiled = gml_truthy(a1) != 0)
BG_SET(stretch, e->stretch = gml_truthy(a1) != 0)
BG_SET(visible, e->visible = gml_truthy(a1) != 0)
BG_SET(xscale, e->xscale = a1)
BG_SET(yscale, e->yscale = a1)

/* ------------------------------------------------------------------ tiles */

static gm_element_t *tile(double id)
{
    gm_element_t *e = gm_layer_element(i32(id));
    return (e != NULL && e->type == GM_ELEMENT_TILE) ? e : NULL;
}

/* layer_tile_create(layer, x, y, sprite, left, top, width, height) (L5057). */
double gml_fn_layer_tile_create(gm_instance_t *self, gm_instance_t *other, gm_value_t a0, double a1, double a2,
                                double a3, double a4, double a5, double a6, double a7)
{
    gm_layer_t *l = layer_of(a0);

    (void)self;
    (void)other;
    if (l == NULL) {
        return 0.0; /* the runner returns undefined */
    }
    return (double)gm_layer_tile_create(l->id, a1, a2, i32(a3), i32(a4), i32(a5), i32(a6), i32(a7));
}

void gml_fn_layer_tile_destroy(gm_instance_t *self, gm_instance_t *other, double a0)
{
    (void)self;
    (void)other;
    gm_layer_element_destroy(i32(a0));
}

gm_value_t gml_fn_layer_tile_get_region(gm_instance_t *self, gm_instance_t *other, double a0)
{
    const gm_element_t *e = tile(a0);
    int region[4];

    (void)self;
    (void)other;
    if (e == NULL) {
        return gm_value_real(-1.0);
    }
    region[0] = e->xo;
    region[1] = e->yo;
    region[2] = e->w;
    region[3] = e->h;
    return real_array(region, 4);
}

#define TILE_GET(suffix, field, missing)                                                     \
    double gml_fn_layer_tile_get_##suffix(gm_instance_t *self, gm_instance_t *other, double a0) \
    {                                                                                        \
        const gm_element_t *e = tile(a0);                                                    \
        (void)self;                                                                          \
        (void)other;                                                                         \
        return e != NULL ? e->field : (missing);                                             \
    }

TILE_GET(x, x, 0.0)
TILE_GET(y, y, 0.0)
TILE_GET(xscale, xscale, 1.0)
TILE_GET(yscale, yscale, 1.0)
