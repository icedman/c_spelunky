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

static int i32(float d)
{
    return (int)gm_to_int32(d);
}

static float b2r(bool b)
{
    return b ? 1.0f : 0.0f;
}

static float subimage(gm_instance_t *self, float sub)
{
    return (sub < 0.0f && self != NULL) ? self->image_index : sub;
}

/* ------------------------------------------------------------------ sprites */

void gml_fn_draw_self(gm_instance_t *self, gm_instance_t *other)
{
    (void)other;
    gm_draw_self(self);
}

void gml_fn_draw_sprite(gm_instance_t *self, gm_instance_t *other, float spr, float sub, float x, float y)
{
    (void)other;
    gm_draw_sprite(i32(spr), subimage(self, sub), x, y);
}

void gml_fn_draw_sprite_ext(gm_instance_t *self, gm_instance_t *other, float spr, float sub, float x, float y,
                            float xs, float ys, float rot, float col, float alpha)
{
    (void)other;
    gm_draw_sprite_ext(i32(spr), subimage(self, sub), x, y, xs, ys, rot, col, alpha);
}

void gml_fn_draw_sprite_stretched(gm_instance_t *self, gm_instance_t *other, float spr, float sub, float x,
                                  float y, float w, float h)
{
    (void)other;
    gm_draw_sprite_stretched(i32(spr), subimage(self, sub), x, y, w, h);
}

float gml_fn_sprite_add(gm_instance_t *self, gm_instance_t *other, const char *path, float n, float removeback,
                         float smooth, float xorig, float yorig)
{
    (void)self;
    (void)other;
    (void)removeback;
    (void)smooth;
    return (float)gm_draw_sprite_add(path, i32(n), xorig, yorig);
}

float gml_fn_sprite_get_width(gm_instance_t *self, gm_instance_t *other, float spr)
{
    (void)self;
    (void)other;
    return (float)gm_draw_sprite_width(i32(spr));
}

float gml_fn_sprite_get_height(gm_instance_t *self, gm_instance_t *other, float spr)
{
    (void)self;
    (void)other;
    return (float)gm_draw_sprite_height(i32(spr));
}

/* ------------------------------------------------------------------ text, shapes, state */

float gml_fn_font_add(gm_instance_t *self, gm_instance_t *other, const char *path, float size, float bold,
                       float italic, float first, float last)
{
    (void)self;
    (void)other;
    (void)bold;
    (void)italic;
    (void)first;
    (void)last;
    return (float)gm_draw_font_add(path, i32(size));
}

/* The host registers the sprite fonts at start-up: font 0 = sFont (16 px glyphs),
 * font 1 = sFontSmall (8 px). setLocale passes sprite_add copies of the locale charset
 * (charset.png / charset_small.png), so the glyph width picks the font. A host that
 * cannot load them (width unknown) gets setLocale's order: large, then small. */
float gml_fn_font_add_sprite_ext(gm_instance_t *self, gm_instance_t *other, float spr, const char *map,
                                  float prop, float sep)
{
    static int calls;
    int w = gm_draw_sprite_width(i32(spr));

    if (w <= 0) {
        return (float)(calls++ % 2);
    }
    (void)self;
    (void)other;
    (void)map;
    (void)prop;
    (void)sep;
    return w >= 16 ? 0.0f : 1.0f;
}

void gml_fn_draw_text(gm_instance_t *self, gm_instance_t *other, float x, float y, const char *text)
{
    (void)self;
    (void)other;
    gm_draw_text(x, y, text);
}

void gml_fn_draw_rectangle(gm_instance_t *self, gm_instance_t *other, float x1, float y1, float x2, float y2,
                           float outline)
{
    (void)self;
    (void)other;
    gm_draw_rectangle(x1, y1, x2, y2, gml_truthy(outline) != 0);
}

void gml_fn_draw_circle(gm_instance_t *self, gm_instance_t *other, float x, float y, float r, float outline)
{
    (void)self;
    (void)other;
    gm_draw_circle(x, y, r, gml_truthy(outline) != 0);
}

void gml_fn_draw_clear(gm_instance_t *self, gm_instance_t *other, float col)
{
    (void)self;
    (void)other;
    gm_draw_clear(col);
}

void gml_fn_draw_set_alpha(gm_instance_t *self, gm_instance_t *other, float a)
{
    (void)self;
    (void)other;
    gm_draw_set_alpha(a);
}

void gml_fn_draw_set_color(gm_instance_t *self, gm_instance_t *other, float c)
{
    (void)self;
    (void)other;
    gm_draw_set_colour(c);
}

void gml_fn_draw_set_colour(gm_instance_t *self, gm_instance_t *other, float c)
{
    (void)self;
    (void)other;
    gm_draw_set_colour(c);
}

void gml_fn_draw_set_font(gm_instance_t *self, gm_instance_t *other, float f)
{
    (void)self;
    (void)other;
    gm_draw_set_font(i32(f));
}

/* ------------------------------------------------------------------ surfaces */

float gml_gget_application_surface(void)
{
    return (float)GM_DRAW_APP_SURFACE;
}

float gml_fn_surface_create(gm_instance_t *self, gm_instance_t *other, float w, float h)
{
    (void)self;
    (void)other;
    return (float)gm_draw_surface_create(i32(w), i32(h));
}

float gml_fn_surface_exists(gm_instance_t *self, gm_instance_t *other, float id)
{
    (void)self;
    (void)other;
    return b2r(gm_draw_surface_exists(i32(id)));
}

void gml_fn_surface_free(gm_instance_t *self, gm_instance_t *other, float id)
{
    (void)self;
    (void)other;
    gm_draw_surface_free(i32(id));
}

void gml_fn_surface_resize(gm_instance_t *self, gm_instance_t *other, float id, float w, float h)
{
    (void)self;
    (void)other;
    gm_draw_surface_resize(i32(id), i32(w), i32(h));
}

float gml_fn_surface_set_target(gm_instance_t *self, gm_instance_t *other, float id)
{
    (void)self;
    (void)other;
    return b2r(gm_draw_surface_set_target(i32(id)));
}

float gml_fn_surface_reset_target(gm_instance_t *self, gm_instance_t *other)
{
    (void)self;
    (void)other;
    return b2r(gm_draw_surface_reset_target());
}

void gml_fn_draw_surface(gm_instance_t *self, gm_instance_t *other, float id, float x, float y)
{
    (void)self;
    (void)other;
    gm_draw_surface(i32(id), x, y);
}

void gml_fn_draw_surface_stretched(gm_instance_t *self, gm_instance_t *other, float id, float x, float y,
                                   float w, float h)
{
    (void)self;
    (void)other;
    gm_draw_surface_stretched(i32(id), x, y, w, h);
}

/* ------------------------------------------------------------------ window */

float gml_fn_window_get_width(gm_instance_t *self, gm_instance_t *other)
{
    (void)self;
    (void)other;
    return (float)gm_window_width();
}

float gml_fn_window_get_height(gm_instance_t *self, gm_instance_t *other)
{
    (void)self;
    (void)other;
    return (float)gm_window_height();
}

float gml_fn_display_get_width(gm_instance_t *self, gm_instance_t *other)
{
    (void)self;
    (void)other;
    return (float)gm_display_width();
}

float gml_fn_display_get_height(gm_instance_t *self, gm_instance_t *other)
{
    (void)self;
    (void)other;
    return (float)gm_display_height();
}

void gml_fn_window_set_size(gm_instance_t *self, gm_instance_t *other, float w, float h)
{
    (void)self;
    (void)other;
    gm_window_set_size(i32(w), i32(h));
}

void gml_fn_window_set_fullscreen(gm_instance_t *self, gm_instance_t *other, float f)
{
    (void)self;
    (void)other;
    gm_window_set_fullscreen(gml_truthy(f) != 0);
}

void gml_fn_display_set_gui_size(gm_instance_t *self, gm_instance_t *other, float w, float h)
{
    (void)self;
    (void)other;
    gm_display_set_gui_size(i32(w), i32(h));
}

/* ------------------------------------------------------------------ audio */

float gml_fn_audio_play_sound(gm_instance_t *self, gm_instance_t *other, float snd, float prio, float loop)
{
    (void)self;
    (void)other;
    return gm_audio_play_sound(snd, prio, (float)gml_truthy(loop));
}

void gml_fn_audio_stop_sound(gm_instance_t *self, gm_instance_t *other, float id)
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

float gml_fn_audio_is_playing(gm_instance_t *self, gm_instance_t *other, float id)
{
    (void)self;
    (void)other;
    return b2r(gm_audio_is_playing(id));
}

void gml_fn_audio_sound_gain(gm_instance_t *self, gm_instance_t *other, float id, float vol, float ms)
{
    (void)self;
    (void)other;
    gm_audio_sound_gain(id, vol, ms);
}

void gml_fn_audio_master_gain(gm_instance_t *self, gm_instance_t *other, float vol)
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
