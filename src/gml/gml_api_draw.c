/*
 * gml_api_draw - bindings for the drawing, surface, window and audio built-ins
 * (slice 10; gm_draw.h, gm_audio.h). Prototypes come from the generated gml_api.h.
 *
 * References: Function_Texture.js (draw_sprite*, draw_self: a negative subimage means
 * the calling instance's image_index), Function_Graphics.js, Function_Font.js,
 * Function_Surface.js, Function_Audio.js. Integer arguments go through yyGetInt32.
 */
#include "gml_rt.h"

#include "gm_audio.h"
#include "gm_draw.h"

static int i32(double d)
{
    return (int)gm_to_int32(d);
}

static double b2r(bool b)
{
    return b ? 1.0 : 0.0;
}

static double subimage(gm_instance_t *self, double sub)
{
    return (sub < 0.0 && self != NULL) ? self->image_index : sub;
}

/* ------------------------------------------------------------------ sprites */

void gml_fn_draw_self(gm_instance_t *self, gm_instance_t *other)
{
    (void)other;
    gm_draw_self(self);
}

void gml_fn_draw_sprite(gm_instance_t *self, gm_instance_t *other, double spr, double sub, double x, double y)
{
    (void)other;
    gm_draw_sprite(i32(spr), subimage(self, sub), x, y);
}

void gml_fn_draw_sprite_ext(gm_instance_t *self, gm_instance_t *other, double spr, double sub, double x, double y,
                            double xs, double ys, double rot, double col, double alpha)
{
    (void)other;
    gm_draw_sprite_ext(i32(spr), subimage(self, sub), x, y, xs, ys, rot, col, alpha);
}

void gml_fn_draw_sprite_stretched(gm_instance_t *self, gm_instance_t *other, double spr, double sub, double x,
                                  double y, double w, double h)
{
    (void)other;
    gm_draw_sprite_stretched(i32(spr), subimage(self, sub), x, y, w, h);
}

double gml_fn_sprite_add(gm_instance_t *self, gm_instance_t *other, const char *path, double n, double removeback,
                         double smooth, double xorig, double yorig)
{
    (void)self;
    (void)other;
    (void)removeback;
    (void)smooth;
    return (double)gm_draw_sprite_add(path, i32(n), xorig, yorig);
}

double gml_fn_sprite_get_width(gm_instance_t *self, gm_instance_t *other, double spr)
{
    (void)self;
    (void)other;
    return (double)gm_draw_sprite_width(i32(spr));
}

double gml_fn_sprite_get_height(gm_instance_t *self, gm_instance_t *other, double spr)
{
    (void)self;
    (void)other;
    return (double)gm_draw_sprite_height(i32(spr));
}

/* ------------------------------------------------------------------ text, shapes, state */

double gml_fn_font_add(gm_instance_t *self, gm_instance_t *other, const char *path, double size, double bold,
                       double italic, double first, double last)
{
    (void)self;
    (void)other;
    (void)bold;
    (void)italic;
    (void)first;
    (void)last;
    return (double)gm_draw_font_add(path, i32(size));
}

void gml_fn_draw_text(gm_instance_t *self, gm_instance_t *other, double x, double y, const char *text)
{
    (void)self;
    (void)other;
    gm_draw_text(x, y, text);
}

void gml_fn_draw_rectangle(gm_instance_t *self, gm_instance_t *other, double x1, double y1, double x2, double y2,
                           double outline)
{
    (void)self;
    (void)other;
    gm_draw_rectangle(x1, y1, x2, y2, gml_truthy(outline) != 0);
}

void gml_fn_draw_circle(gm_instance_t *self, gm_instance_t *other, double x, double y, double r, double outline)
{
    (void)self;
    (void)other;
    gm_draw_circle(x, y, r, gml_truthy(outline) != 0);
}

void gml_fn_draw_clear(gm_instance_t *self, gm_instance_t *other, double col)
{
    (void)self;
    (void)other;
    gm_draw_clear(col);
}

void gml_fn_draw_set_alpha(gm_instance_t *self, gm_instance_t *other, double a)
{
    (void)self;
    (void)other;
    gm_draw_set_alpha(a);
}

void gml_fn_draw_set_color(gm_instance_t *self, gm_instance_t *other, double c)
{
    (void)self;
    (void)other;
    gm_draw_set_colour(c);
}

void gml_fn_draw_set_colour(gm_instance_t *self, gm_instance_t *other, double c)
{
    (void)self;
    (void)other;
    gm_draw_set_colour(c);
}

void gml_fn_draw_set_font(gm_instance_t *self, gm_instance_t *other, double f)
{
    (void)self;
    (void)other;
    gm_draw_set_font(i32(f));
}

/* ------------------------------------------------------------------ surfaces */

double gml_gget_application_surface(void)
{
    return (double)GM_DRAW_APP_SURFACE;
}

double gml_fn_surface_create(gm_instance_t *self, gm_instance_t *other, double w, double h)
{
    (void)self;
    (void)other;
    return (double)gm_draw_surface_create(i32(w), i32(h));
}

double gml_fn_surface_exists(gm_instance_t *self, gm_instance_t *other, double id)
{
    (void)self;
    (void)other;
    return b2r(gm_draw_surface_exists(i32(id)));
}

void gml_fn_surface_free(gm_instance_t *self, gm_instance_t *other, double id)
{
    (void)self;
    (void)other;
    gm_draw_surface_free(i32(id));
}

void gml_fn_surface_resize(gm_instance_t *self, gm_instance_t *other, double id, double w, double h)
{
    (void)self;
    (void)other;
    gm_draw_surface_resize(i32(id), i32(w), i32(h));
}

double gml_fn_surface_set_target(gm_instance_t *self, gm_instance_t *other, double id)
{
    (void)self;
    (void)other;
    return b2r(gm_draw_surface_set_target(i32(id)));
}

double gml_fn_surface_reset_target(gm_instance_t *self, gm_instance_t *other)
{
    (void)self;
    (void)other;
    return b2r(gm_draw_surface_reset_target());
}

void gml_fn_draw_surface(gm_instance_t *self, gm_instance_t *other, double id, double x, double y)
{
    (void)self;
    (void)other;
    gm_draw_surface(i32(id), x, y);
}

void gml_fn_draw_surface_stretched(gm_instance_t *self, gm_instance_t *other, double id, double x, double y,
                                   double w, double h)
{
    (void)self;
    (void)other;
    gm_draw_surface_stretched(i32(id), x, y, w, h);
}

/* ------------------------------------------------------------------ window */

double gml_fn_window_get_width(gm_instance_t *self, gm_instance_t *other)
{
    (void)self;
    (void)other;
    return (double)gm_window_width();
}

double gml_fn_window_get_height(gm_instance_t *self, gm_instance_t *other)
{
    (void)self;
    (void)other;
    return (double)gm_window_height();
}

double gml_fn_display_get_width(gm_instance_t *self, gm_instance_t *other)
{
    (void)self;
    (void)other;
    return (double)gm_display_width();
}

double gml_fn_display_get_height(gm_instance_t *self, gm_instance_t *other)
{
    (void)self;
    (void)other;
    return (double)gm_display_height();
}

void gml_fn_window_set_size(gm_instance_t *self, gm_instance_t *other, double w, double h)
{
    (void)self;
    (void)other;
    gm_window_set_size(i32(w), i32(h));
}

void gml_fn_window_set_fullscreen(gm_instance_t *self, gm_instance_t *other, double f)
{
    (void)self;
    (void)other;
    gm_window_set_fullscreen(gml_truthy(f) != 0);
}

void gml_fn_display_set_gui_size(gm_instance_t *self, gm_instance_t *other, double w, double h)
{
    (void)self;
    (void)other;
    gm_display_set_gui_size(i32(w), i32(h));
}

/* ------------------------------------------------------------------ audio */

double gml_fn_audio_play_sound(gm_instance_t *self, gm_instance_t *other, double snd, double prio, double loop)
{
    (void)self;
    (void)other;
    return gm_audio_play_sound(snd, prio, (double)gml_truthy(loop));
}

void gml_fn_audio_stop_sound(gm_instance_t *self, gm_instance_t *other, double id)
{
    (void)self;
    (void)other;
    gm_audio_stop_sound(id);
}

void gml_fn_audio_stop_all(gm_instance_t *self, gm_instance_t *other)
{
    (void)self;
    (void)other;
    gm_audio_stop_all();
}

double gml_fn_audio_is_playing(gm_instance_t *self, gm_instance_t *other, double id)
{
    (void)self;
    (void)other;
    return b2r(gm_audio_is_playing(id));
}

void gml_fn_audio_sound_gain(gm_instance_t *self, gm_instance_t *other, double id, double vol, double ms)
{
    (void)self;
    (void)other;
    gm_audio_sound_gain(id, vol, ms);
}

void gml_fn_audio_master_gain(gm_instance_t *self, gm_instance_t *other, double vol)
{
    (void)self;
    (void)other;
    gm_audio_master_gain(vol);
}

void gml_fn_audio_pause_all(gm_instance_t *self, gm_instance_t *other)
{
    (void)self;
    (void)other;
    gm_audio_pause_all();
}

void gml_fn_audio_resume_all(gm_instance_t *self, gm_instance_t *other)
{
    (void)self;
    (void)other;
    gm_audio_resume_all();
}
