/*
 * gm_draw - GameMaker drawing on top of sp_platform (slice 10).
 *
 * Models the parts of the HTML5 runner's render pipeline Spelunky uses
 * (yyRoom.Draw / DrawViews / DrawRoomLayers, Function_Texture.js, Function_Graphics.js,
 * Function_Surface.js, Function_Font.js):
 *
 *  - Draw state: colour, alpha, font (draw_set_*).
 *  - A world -> target transform. gm_loop selects it per pass: each visible view maps
 *    its camera rectangle onto its port in the application surface; without views
 *    the room maps 1:1; the GUI pass maps display_set_gui_size onto the application
 *    surface. surface_set_target switches to the surface's own pixels until
 *    surface_reset_target (a small stack, as in the runner).
 *  - The application surface (id 0 = application_surface) is app_w x app_h;
 *    surface_resize(application_surface, ...) sets it (oScreen sizes it to the view).
 *    The platform scales it into the window on present.
 *  - Surfaces (surface_create ...) map to platform render targets.
 *  - sprite_add sprites live above GM_SPRITE_MAX (static sprites keep their indices).
 *  - font_add fonts map to platform fonts; -1 (or a failed load) is the default font.
 *  - Room layers: background colour / sprite (tiled, stretched) and legacy tiles.
 *
 * Only English is supported: locale data that would come from JSON (font sizes) is
 * not read, and the default font stands in for a font that fails to load.
 *
 * Memory: static tables only.
 */
#ifndef GM_DRAW_H
#define GM_DRAW_H

#include "gm_instance.h"
#include "gm_layer.h"

#include <stdbool.h>
#include <stdint.h>

#define GM_DRAW_APP_SURFACE 0          /* application_surface */
#ifndef GM_DRAW_SURFACE_MAX
#define GM_DRAW_SURFACE_MAX 32
#endif
#ifndef GM_DRAW_ADDED_SPRITE_MAX
#define GM_DRAW_ADDED_SPRITE_MAX 64
#endif
#ifndef GM_DRAW_FONT_MAX
#define GM_DRAW_FONT_MAX 16
#endif

/* Forgets state, surfaces, added sprites and fonts (no platform calls). */
void gm_draw_reset(void);

/* ---- state ---- */
void gm_draw_set_colour(float colour);
void gm_draw_set_alpha(float alpha);
void gm_draw_set_font(int font);
uint32_t gm_draw_colour(void);
float gm_draw_alpha(void);

/* ---- frame and passes (gm_loop) ---- */
void gm_draw_begin_frame(void);
void gm_draw_pass_view(int view);   /* a visible view */
void gm_draw_pass_room(void);       /* views disabled */
void gm_draw_pass_gui(void);
void gm_draw_end_frame(void);
int gm_draw_app_width(void);
int gm_draw_app_height(void);

/* ---- drawing (world coordinates of the current pass) ---- */
void gm_draw_sprite_ext(int sprite, float subimg, float x, float y, float xscale, float yscale,
                        float angle, float colour, float alpha);
void gm_draw_sprite(int sprite, float subimg, float x, float y);   /* draw alpha */
void gm_draw_sprite_stretched(int sprite, float subimg, float x, float y, float w, float h);
void gm_draw_self(gm_instance_t *inst);
void gm_draw_text(float x, float y, const char *text);
void gm_draw_rectangle(float x1, float y1, float x2, float y2, bool outline);
void gm_draw_circle(float x, float y, float r, bool outline);
void gm_draw_clear(float colour);
void gm_draw_layer(const gm_layer_t *layer);

/* ---- sprites and fonts loaded at run time ---- */
int gm_draw_sprite_add(const char *path, int frames, float xorig, float yorig);   /* -1 on failure */
bool gm_draw_sprite_exists(int sprite);
int gm_draw_sprite_width(int sprite);
int gm_draw_sprite_height(int sprite);
int gm_draw_font_add(const char *path, int size);   /* -1 on failure */

/* ---- surfaces ---- */
int gm_draw_surface_create(int w, int h);   /* -1 on failure */
bool gm_draw_surface_exists(int id);
void gm_draw_surface_free(int id);
void gm_draw_surface_resize(int id, int w, int h);
bool gm_draw_surface_set_target(int id);
bool gm_draw_surface_reset_target(void);
void gm_draw_surface(int id, float x, float y);
void gm_draw_surface_stretched(int id, float x, float y, float w, float h);

/* ---- window ---- */
int gm_window_width(void);
int gm_window_height(void);
int gm_display_width(void);
int gm_display_height(void);
void gm_window_set_size(int w, int h);
void gm_window_set_fullscreen(bool fullscreen);
void gm_display_set_gui_size(int w, int h);

long gm_draw_overflows(void);   /* surfaces / sprites / fonts not created: table full */

#endif /* GM_DRAW_H */
