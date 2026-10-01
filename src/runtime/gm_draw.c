/*
 * gm_draw - see gm_draw.h. File references are GameMaker-HTML5 scripts.
 */
#include "gm_draw.h"

#include "gm_math.h"
#include "gm_room.h"
#include "gm_sprite.h"
#include "gm_view.h"
#include "sp_platform.h"

#ifdef PLAYDATE
#include "pd_config.h"
#endif

#include <math.h>
#include <stddef.h>
#include <string.h>

#define P g_platform.user_data

typedef struct xform {
    double sx, sy, tx, ty;      /* target = world * s + t */
    double wx, wy, ww, wh;      /* the world rectangle the target shows */
} xform_t;

typedef struct surface {
    bool used;
    int handle;                 /* platform handle */
    int w, h;
} surface_t;

typedef struct added_sprite {
    bool used;
    int frames, w, h;
    double xorig, yorig;
} added_sprite_t;

#define TARGET_STACK 8

static uint32_t s_colour = 0xFFFFFFu;
static double s_alpha = 1.0;
static int s_font = -1;
static int s_app_w, s_app_h;
static int s_gui_w, s_gui_h;
static xform_t s_xf;
static xform_t s_stack[TARGET_STACK];
static int s_stack_target[TARGET_STACK];
static int s_depth;
static int s_target;            /* surface id; GM_DRAW_APP_SURFACE outside surface_set_target */
static surface_t s_surfaces[GM_DRAW_SURFACE_MAX];
static added_sprite_t s_added[GM_DRAW_ADDED_SPRITE_MAX];
static int s_fonts[GM_DRAW_FONT_MAX];
static int s_font_count;
static long s_overflows;

static uint32_t colour_of(double c)
{
    return (uint32_t)gm_to_int32(c) & 0xFFFFFFu;
}

static double clamp01(double a)
{
    return a < 0.0 ? 0.0 : (a > 1.0 ? 1.0 : a);
}

static void identity(double w, double h)
{
    s_xf.sx = s_xf.sy = 1.0;
    s_xf.tx = s_xf.ty = 0.0;
    s_xf.wx = s_xf.wy = 0.0;
    s_xf.ww = w;
    s_xf.wh = h;
}

void gm_draw_reset(void)
{
    s_colour = 0xFFFFFFu;
    s_alpha = 1.0;
    s_font = -1;
    s_app_w = s_app_h = 0;
    s_gui_w = s_gui_h = 0;
    s_depth = 0;
    s_target = GM_DRAW_APP_SURFACE;
    memset(s_surfaces, 0, sizeof(s_surfaces));
    memset(s_added, 0, sizeof(s_added));
    s_font_count = 0;
    s_overflows = 0;
    identity(0.0, 0.0);
}

long gm_draw_overflows(void)
{
    return s_overflows;
}

/* ---- state ------------------------------------------------------------------------------ */

void gm_draw_set_colour(double colour)
{
    s_colour = colour_of(colour);
}

/* draw_set_alpha (Function_Graphics.js): clamped to 0..1. */
void gm_draw_set_alpha(double alpha)
{
    s_alpha = clamp01(alpha);
}

void gm_draw_set_font(int font)
{
    s_font = font;
}

uint32_t gm_draw_colour(void)
{
    return s_colour;
}

double gm_draw_alpha(void)
{
    return s_alpha;
}

/* ---- frame ---------------------------------------------------------------------------------- */

/* Until surface_resize sizes it, the application surface is view 0's port, or the
 * room (the runner's default size). */
static void default_app_size(void)
{
    if (s_app_w > 0 && s_app_h > 0) {
        return;
    }
    if (gm_view_enabled() && gm_view_get(GM_VIEW_WPORT, 0) > 0.0 && gm_view_get(GM_VIEW_HPORT, 0) > 0.0) {
        s_app_w = (int)gm_view_get(GM_VIEW_WPORT, 0);
        s_app_h = (int)gm_view_get(GM_VIEW_HPORT, 0);
    } else {
        s_app_w = (int)gm_room_width();
        s_app_h = (int)gm_room_height();
    }
}

void gm_draw_begin_frame(void)
{
    default_app_size();
    s_depth = 0;
    s_target = GM_DRAW_APP_SURFACE;
    g_platform.begin_frame(P, s_app_w, s_app_h);
    g_platform.clear(P, 0x000000u, 1.0);
    identity((double)s_app_w, (double)s_app_h);
}

/* DrawViews (yyRoom.js L3890): the view's camera rectangle onto its port. */
void gm_draw_pass_view(int view)
{
    gm_camera_t *cam = gm_camera_get((int)gm_view_get(GM_VIEW_CAMERA, view));
    double vx = gm_view_get(GM_VIEW_XVIEW, view), vy = gm_view_get(GM_VIEW_YVIEW, view);
    double vw = gm_view_get(GM_VIEW_WVIEW, view), vh = gm_view_get(GM_VIEW_HVIEW, view);
    double px = gm_view_get(GM_VIEW_XPORT, view), py = gm_view_get(GM_VIEW_YPORT, view);
    double pw = gm_view_get(GM_VIEW_WPORT, view), ph = gm_view_get(GM_VIEW_HPORT, view);

    if (cam != NULL) {
        vx = cam->x;
        vy = cam->y;
        vw = cam->w;
        vh = cam->h;
    }
    s_xf.sx = vw != 0.0 ? pw / vw : 1.0;
    s_xf.sy = vh != 0.0 ? ph / vh : 1.0;
    s_xf.tx = px - vx * s_xf.sx;
    s_xf.ty = py - vy * s_xf.sy;
    s_xf.wx = vx;
    s_xf.wy = vy;
    s_xf.ww = vw;
    s_xf.wh = vh;
}

void gm_draw_pass_room(void)
{
    identity(gm_room_width(), gm_room_height());
}

/* DrawGUI (yyRoom.js L4102): GUI size (default: the application surface) onto the
 * application surface. */
void gm_draw_pass_gui(void)
{
    double gw = s_gui_w > 0 ? (double)s_gui_w : (double)s_app_w;
    double gh = s_gui_h > 0 ? (double)s_gui_h : (double)s_app_h;

    identity(gw, gh);
    s_xf.sx = gw > 0.0 ? (double)s_app_w / gw : 1.0;
    s_xf.sy = gh > 0.0 ? (double)s_app_h / gh : 1.0;
}

void gm_draw_end_frame(void)
{
    while (s_depth > 0) {
        gm_draw_surface_reset_target();
    }
    g_platform.present(P);
}

int gm_draw_app_width(void)
{
    return s_app_w;
}

int gm_draw_app_height(void)
{
    return s_app_h;
}

/* ---- sprites -------------------------------------------------------------------------------- */

typedef struct sprite_info {
    int frames, w, h;
    double xorig, yorig;
} sprite_info_t;

static bool sprite_info(int sprite, sprite_info_t *out)
{
    const gm_sprite_def_t *spr = gm_sprite_get(sprite);

    if (spr != NULL) {
        out->frames = spr->frame_count;
        out->w = spr->width;
        out->h = spr->height;
        out->xorig = (double)spr->xorigin;
        out->yorig = (double)spr->yorigin;
        return out->frames > 0;
    }
    if (sprite >= GM_SPRITE_MAX && sprite < GM_SPRITE_MAX + GM_DRAW_ADDED_SPRITE_MAX &&
        s_added[sprite - GM_SPRITE_MAX].used) {
        const added_sprite_t *a = &s_added[sprite - GM_SPRITE_MAX];
        out->frames = a->frames;
        out->w = a->w;
        out->h = a->h;
        out->xorig = a->xorig;
        out->yorig = a->yorig;
        return out->frames > 0;
    }
    return false;
}

/* GetIndexFromImageIndex: floor, wrapped into 0 .. frames-1. */
static int frame_of(double subimg, int frames)
{
    double f = floor(subimg);
    int i;

    if (!(f == f) || frames <= 0) {
        return 0;
    }
    f = fmod(f, (double)frames);
    i = (int)f;
    return i < 0 ? i + frames : i;
}

static void image(int sprite, int frame, int sx, int sy, int sw, int sh, double x, double y,
                  double xo, double yo, double xs, double ys, double angle, uint32_t colour, double alpha)
{
    g_platform.draw_image(P, sprite, frame, sx, sy, sw, sh, x * s_xf.sx + s_xf.tx, y * s_xf.sy + s_xf.ty,
                          xo, yo, xs * s_xf.sx, ys * s_xf.sy, angle, colour, alpha);
}

void gm_draw_sprite_ext(int sprite, double subimg, double x, double y, double xscale, double yscale,
                        double angle, double colour, double alpha)
{
    sprite_info_t s;

    if (!sprite_info(sprite, &s)) {
        return;
    }
    image(sprite, frame_of(subimg, s.frames), 0, 0, s.w, s.h, x, y, s.xorig, s.yorig, xscale, yscale, angle,
          colour_of(colour), alpha > 1.0 ? 1.0 : alpha);
}

void gm_draw_sprite(int sprite, double subimg, double x, double y)
{
    sprite_info_t s;

    if (!sprite_info(sprite, &s)) {
        return;
    }
    image(sprite, frame_of(subimg, s.frames), 0, 0, s.w, s.h, x, y, s.xorig, s.yorig, 1.0, 1.0, 0.0,
          0xFFFFFFu, s_alpha);
}

/* Graphics_DrawStretchedExt: the frame from (x, y), origin ignored, draw alpha. */
void gm_draw_sprite_stretched(int sprite, double subimg, double x, double y, double w, double h)
{
    sprite_info_t s;

    if (!sprite_info(sprite, &s) || s.w <= 0 || s.h <= 0) {
        return;
    }
    image(sprite, frame_of(subimg, s.frames), 0, 0, s.w, s.h, x, y, 0.0, 0.0, w / s.w, h / s.h, 0.0,
          0xFFFFFFu, s_alpha);
}

/* draw_self (Function_Texture.js L36) and the default draw (yyRoom.js L1092). */
void gm_draw_self(gm_instance_t *inst)
{
    if (inst == NULL) {
        return;
    }
    gm_draw_sprite_ext(inst->sprite_index, inst->image_index, inst->x, inst->y, inst->image_xscale,
                       inst->image_yscale, inst->image_angle, (double)inst->image_blend, inst->image_alpha);
}

int gm_draw_sprite_add(const char *path, int frames, double xorig, double yorig)
{
    int i, w = 0, h = 0;

    if (path == NULL || frames < 1) {
        return -1;
    }
    for (i = 0; i < GM_DRAW_ADDED_SPRITE_MAX; ++i) {
        if (!s_added[i].used) {
            break;
        }
    }
    if (i == GM_DRAW_ADDED_SPRITE_MAX) {
        s_overflows++;
        return -1;
    }
    if (!g_platform.sprite_load(P, GM_SPRITE_MAX + i, path, frames, &w, &h)) {
        return -1;
    }
    s_added[i].used = true;
    s_added[i].frames = frames;
    s_added[i].w = w;
    s_added[i].h = h;
    s_added[i].xorig = xorig;
    s_added[i].yorig = yorig;
    return GM_SPRITE_MAX + i;
}

bool gm_draw_sprite_exists(int sprite)
{
    sprite_info_t s;
    return sprite_info(sprite, &s);
}

int gm_draw_sprite_width(int sprite)
{
    sprite_info_t s;
    return sprite_info(sprite, &s) ? s.w : 0;
}

int gm_draw_sprite_height(int sprite)
{
    sprite_info_t s;
    return sprite_info(sprite, &s) ? s.h : 0;
}

/* ---- text and shapes ------------------------------------------------------------------------ */

int gm_draw_font_add(const char *path, int size)
{
    int handle;

    if (path == NULL) {
        return -1;
    }
    if (s_font_count >= GM_DRAW_FONT_MAX) {
        s_overflows++;
        return -1;
    }
    handle = g_platform.font_load(P, path, size);
    if (handle <= 0) {
        return -1;
    }
    s_fonts[s_font_count] = handle;
    return s_font_count++;
}

/* draw_text (Function_Font.js L59): draw colour and alpha, left/top aligned. */
void gm_draw_text(double x, double y, const char *text)
{
    int handle = (s_font >= 0 && s_font < s_font_count) ? s_fonts[s_font] : 0;

    if (text == NULL || text[0] == '\0') {
        return;
    }
    g_platform.draw_text(P, handle, text, x * s_xf.sx + s_xf.tx, y * s_xf.sy + s_xf.ty, s_xf.sx, s_colour,
                         s_alpha);
}

void gm_draw_rectangle(double x1, double y1, double x2, double y2, bool outline)
{
    g_platform.draw_rect(P, x1 * s_xf.sx + s_xf.tx, y1 * s_xf.sy + s_xf.ty, x2 * s_xf.sx + s_xf.tx,
                         y2 * s_xf.sy + s_xf.ty, s_colour, s_alpha, outline);
}

void gm_draw_circle(double x, double y, double r, bool outline)
{
    g_platform.draw_circle(P, x * s_xf.sx + s_xf.tx, y * s_xf.sy + s_xf.ty, r * s_xf.sx, s_colour, s_alpha,
                           outline);
}

/* draw_clear (Function_Graphics.js L205): the whole target, opaque. */
void gm_draw_clear(double colour)
{
    g_platform.clear(P, colour_of(colour), 1.0);
}

/* ---- layers ------------------------------------------------------------------------------------ */

/* DrawLayerBackgroundElement (yyRoom.js L1213): a colour fills the view; a sprite is
 * drawn at the layer offset, stretched or repeated across the view when tiled. */
static void draw_background(const gm_layer_t *l, const gm_element_t *e)
{
    sprite_info_t s;
    double x0, y0, x, y, w, h, xs, ys;
    int frame;

#if defined(DISABLE_BACKGROUND) && DISABLE_BACKGROUND
    return;
#endif
    if (!e->visible) {
        return;
    }
    if (!sprite_info(e->sprite, &s)) {
        g_platform.draw_rect(P, s_xf.wx * s_xf.sx + s_xf.tx, s_xf.wy * s_xf.sy + s_xf.ty,
                             (s_xf.wx + s_xf.ww) * s_xf.sx + s_xf.tx, (s_xf.wy + s_xf.wh) * s_xf.sy + s_xf.ty,
                             e->blend, e->alpha, false);
        return;
    }
    xs = e->stretch ? (e->xscale != 0.0 ? e->xscale : 1.0) : 1.0;
    ys = e->stretch ? (e->yscale != 0.0 ? e->yscale : 1.0) : 1.0;
    w = s.w * xs;
    h = s.h * ys;
    if (w <= 0.0 || h <= 0.0) {
        return;
    }
    frame = frame_of(e->image_index, s.frames);
    x0 = l->x;
    y0 = l->y;
    if (e->htiled) {
        x0 -= ceil((x0 - s_xf.wx) / w) * w;
    }
    if (e->vtiled) {
        y0 -= ceil((y0 - s_xf.wy) / h) * h;
    }
    for (y = y0; y < s_xf.wy + s_xf.wh || y == y0; y += h) {
        for (x = x0; x < s_xf.wx + s_xf.ww || x == x0; x += w) {
            image(e->sprite, frame, 0, 0, s.w, s.h, x, y, 0.0, 0.0, xs, ys, 0.0, e->blend, e->alpha);
            if (!e->htiled) {
                break;
            }
        }
        if (!e->vtiled) {
            break;
        }
    }
}

/* DrawLayerTileElement (yyRoom.js L2243): a region of frame 0. Tiles outside the
 * world rectangle are skipped (the runner culls too). */
static void draw_tile(const gm_layer_t *l, const gm_element_t *e)
{
    double x = e->x + l->x, y = e->y + l->y;

    if (!e->visible || x > s_xf.wx + s_xf.ww || y > s_xf.wy + s_xf.wh || x + e->w * e->xscale < s_xf.wx ||
        y + e->h * e->yscale < s_xf.wy || !gm_draw_sprite_exists(e->sprite)) {
        return;
    }
    image(e->sprite, 0, e->xo, e->yo, e->w, e->h, x, y, 0.0, 0.0, e->xscale, e->yscale, 0.0, e->blend, e->alpha);
}

void gm_draw_layer(const gm_layer_t *layer)
{
    int slot;

    if (layer == NULL || !layer->visible) {
        return;
    }
    for (slot = layer->head; slot >= 0;) {
        const gm_element_t *e = gm_layer_element_slot(slot);
        if (e == NULL) {
            break;
        }
        if (e->type == GM_ELEMENT_BACKGROUND) {
            draw_background(layer, e);
        } else if (e->type == GM_ELEMENT_TILE) {
            draw_tile(layer, e);
        }
        slot = e->next;
    }
}

/* ---- surfaces ----------------------------------------------------------------------------------- */

static surface_t *surface(int id)
{
    return (id >= 1 && id <= GM_DRAW_SURFACE_MAX && s_surfaces[id - 1].used) ? &s_surfaces[id - 1] : NULL;
}

int gm_draw_surface_create(int w, int h)
{
    int i, handle;

    if (w < 1 || h < 1) {
        return -1;
    }
    for (i = 0; i < GM_DRAW_SURFACE_MAX && s_surfaces[i].used; ++i) {
    }
    if (i == GM_DRAW_SURFACE_MAX) {
        s_overflows++;
        return -1;
    }
    handle = g_platform.surface_create(P, w, h);
    if (handle <= 0) {
        return -1;
    }
    s_surfaces[i].used = true;
    s_surfaces[i].handle = handle;
    s_surfaces[i].w = w;
    s_surfaces[i].h = h;
    return i + 1;
}

bool gm_draw_surface_exists(int id)
{
    return id == GM_DRAW_APP_SURFACE || surface(id) != NULL;
}

void gm_draw_surface_free(int id)
{
    surface_t *s = surface(id);

    if (s != NULL) {
        g_platform.surface_free(P, s->handle);
        s->used = false;
    }
}

/* surface_resize: the application surface takes the size from the next frame; other
 * surfaces are recreated (their contents are lost, as on the runner). */
void gm_draw_surface_resize(int id, int w, int h)
{
    surface_t *s = surface(id);

    if (w < 1 || h < 1) {
        return;
    }
    if (id == GM_DRAW_APP_SURFACE) {
        s_app_w = w;
        s_app_h = h;
    } else if (s != NULL) {
        int handle = g_platform.surface_create(P, w, h);
        if (handle > 0) {
            g_platform.surface_free(P, s->handle);
            s->handle = handle;
            s->w = w;
            s->h = h;
        }
    }
}

/* surface_set_target (Function_Surface.js): the surface's own pixels become the
 * world until surface_reset_target. */
bool gm_draw_surface_set_target(int id)
{
    surface_t *s = surface(id);

    if ((s == NULL && id != GM_DRAW_APP_SURFACE) || s_depth >= TARGET_STACK) {
        return false;
    }
    s_stack[s_depth] = s_xf;
    s_stack_target[s_depth] = s_target;
    s_depth++;
    s_target = id;
    g_platform.surface_target(P, s != NULL ? s->handle : 0);
    if (s != NULL) {
        identity((double)s->w, (double)s->h);
    } else {
        identity((double)s_app_w, (double)s_app_h);
    }
    return true;
}

bool gm_draw_surface_reset_target(void)
{
    surface_t *s;

    if (s_depth == 0) {
        return false;
    }
    s_depth--;
    s_xf = s_stack[s_depth];
    s_target = s_stack_target[s_depth];
    s = surface(s_target);
    g_platform.surface_target(P, s != NULL ? s->handle : 0);
    return true;
}

void gm_draw_surface(int id, double x, double y)
{
    surface_t *s = surface(id);

    if (s != NULL) {
        gm_draw_surface_stretched(id, x, y, (double)s->w, (double)s->h);
    }
}

/* draw_surface_stretched (Function_Surface.js L878): white, alpha 1. */
void gm_draw_surface_stretched(int id, double x, double y, double w, double h)
{
    surface_t *s = surface(id);

    if (s == NULL || id == s_target) {
        return;
    }
    g_platform.draw_surface(P, s->handle, x * s_xf.sx + s_xf.tx, y * s_xf.sy + s_xf.ty, w * s_xf.sx,
                            h * s_xf.sy, 1.0);
}

/* ---- window ---------------------------------------------------------------------------------------- */

int gm_window_width(void)
{
    int w = 0, h = 0;
    g_platform.window_size(P, &w, &h);
    return w;
}

int gm_window_height(void)
{
    int w = 0, h = 0;
    g_platform.window_size(P, &w, &h);
    return h;
}

int gm_display_width(void)
{
    int w = 0, h = 0;
    g_platform.display_size(P, &w, &h);
    return w;
}

int gm_display_height(void)
{
    int w = 0, h = 0;
    g_platform.display_size(P, &w, &h);
    return h;
}

void gm_window_set_size(int w, int h)
{
    if (w > 0 && h > 0) {
        g_platform.set_window_size(P, w, h);
    }
}

void gm_window_set_fullscreen(bool fullscreen)
{
    g_platform.set_fullscreen(P, fullscreen);
}

void gm_display_set_gui_size(int w, int h)
{
    s_gui_w = w;
    s_gui_h = h;
}
