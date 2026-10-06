/*
 * sp_platform.h - single-header platform abstraction (video, input, system).
 *
 * The runtime core only ever talks to the host through `g_platform`. Hosts
 * (SDL2, Playdate, WASM, ...) assign their own function table at startup; by
 * default `g_platform` holds headless no-op stubs so game logic and level
 * generation run without any display.
 *
 * Exactly one translation unit must define SP_PLATFORM_IMPLEMENTATION before
 * including this header (see src/runtime/sp_stubs.c).
 *
 * Rendering (slice 10) is shaped by what gm_draw needs to reproduce GameMaker's
 * pipeline: everything is drawn into the "application surface" (or a surface made
 * with surface_create) in that target's pixels - gm_draw applies views and the GUI
 * transform - and present() scales the application surface into the window.
 * Colours are GameMaker colours (0x00BBGGRR), alpha is 0..1.
 *
 * Deviations from the sketch in agents/SPELUNKY_C.md section 3.1, all driven by what
 * Spelunky Classic HD actually calls:
 *   - `user_data` lives in the interface so callers can forward it.
 *   - sp_input_state_t also carries raw key state indexed by GameMaker vk_*
 *     codes and gamepad state (hosts may instead feed gm_input directly).
 *   - `directory_exists` hook: portable C99 cannot query directories, so
 *     gm_file delegates that one check to the host.
 *   - One sprite-region primitive (draw_image) serves sprites, tiles and
 *     backgrounds; text is TTF (font_load / draw_text); surfaces are render targets.
 */
#ifndef SP_PLATFORM_H
#define SP_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Logical game buttons (convenience bitmask; hosts may leave it zero). */
typedef enum {
    SP_KEY_LEFT   = 1 << 0,
    SP_KEY_RIGHT  = 1 << 1,
    SP_KEY_UP     = 1 << 2,
    SP_KEY_DOWN   = 1 << 3,
    SP_KEY_JUMP   = 1 << 4, /* 'Z' / Button A */
    SP_KEY_ACTION = 1 << 5, /* 'X' / Button B (whip / throw) */
    SP_KEY_BOMB   = 1 << 6, /* 'C' */
    SP_KEY_ROPE   = 1 << 7, /* 'V' */
    SP_KEY_PAY    = 1 << 8, /* 'P' */
    SP_KEY_START  = 1 << 9  /* Escape / Start */
} sp_input_button_t;

/* Raw keys are indexed by GameMaker virtual key code (vk_left = 37, 'A' = 65). */
#define SP_KEY_CODE_COUNT 256

#define SP_MAX_GAMEPADS 4
/* Button index = GameMaker gp_* constant - 32769 (gp_face1 .. gp_padr). */
#define SP_GAMEPAD_BUTTON_COUNT 16
/* Axis index = gp_* constant - 32785 (gp_axislh, gp_axislv, gp_axisrh, gp_axisrv). */
#define SP_GAMEPAD_AXIS_COUNT 4

typedef struct sp_gamepad_state {
    bool connected;
    float button_value[SP_GAMEPAD_BUTTON_COUNT]; /* 0.0 .. 1.0 (analog triggers) */
    float axis[SP_GAMEPAD_AXIS_COUNT];           /* -1.0 .. 1.0 */
} sp_gamepad_state_t;

typedef struct sp_input_state {
    uint32_t buttons_held;
    uint32_t buttons_pressed;
    uint32_t buttons_released;
    bool key_held[SP_KEY_CODE_COUNT]; /* pressed/released edges are derived by the runtime */
    sp_gamepad_state_t gamepads[SP_MAX_GAMEPADS];
} sp_input_state_t;

typedef struct sp_platform_interface {
    void *user_data; /* forwarded as the first argument of every hook */

    /* Initialization & lifecycle */
    bool (*init)(void *user_data, int width, int height, int scale);
    void (*shutdown)(void *user_data);
    /* Fills *out_input; returns false when the host requests quit. */
    bool (*poll_events)(void *user_data, sp_input_state_t *out_input);

    /* ---- Frame ---- */
    /* Starts a frame: the application surface is app_w x app_h and becomes the target. */
    void (*begin_frame)(void *user_data, int app_w, int app_h);
    /* Ends a frame: the application surface, scaled to fit, is shown in the window. */
    void (*present)(void *user_data);
    /* Fills the current target. */
    void (*clear)(void *user_data, uint32_t colour, float alpha);

    /* ---- Images ---- */
    /* sprite_add: loads `path` (a horizontal strip of `frames` frames) as sprite index
     * `sprite`; writes one frame's size. Static sprites are found by the host itself. */
    bool (*sprite_load)(void *user_data, int sprite, const char *path, int frames, int *w, int *h);
    /* Region (sx, sy, sw, sh) of a sprite frame, placed so that region pixel
     * (xorig, yorig) lands on (x, y), scaled (negative = mirrored) and rotated
     * `angle` degrees counter-clockwise around that point, tinted by colour. */
    void (*draw_image)(void *user_data, int sprite, int frame, int sx, int sy, int sw, int sh,
                       float x, float y, float xorig, float yorig, float xscale, float yscale,
                       float angle, uint32_t colour, float alpha);
    void (*draw_rect)(void *user_data, float x1, float y1, float x2, float y2,
                      uint32_t colour, float alpha, bool outline);
    void (*draw_circle)(void *user_data, float x, float y, float r,
                        uint32_t colour, float alpha, bool outline);

    /* ---- Text ---- */
    /* Returns a font handle > 0, or 0 on failure. Font 0 is the host's default font. */
    int (*font_load)(void *user_data, const char *path, int size);
    /* Top-left aligned; "\n" starts a new line. */
    void (*draw_text)(void *user_data, int font, const char *text, float x, float y,
                      float scale, uint32_t colour, float alpha);

    /* ---- Surfaces (render targets) ---- */
    int (*surface_create)(void *user_data, int w, int h);   /* handle > 0, or 0 */
    void (*surface_free)(void *user_data, int surface);
    void (*surface_target)(void *user_data, int surface);   /* 0: the application surface */
    void (*draw_surface)(void *user_data, int surface, float x, float y, float w, float h,
                         float alpha);

    /* ---- Window ---- */
    void (*window_size)(void *user_data, int *w, int *h);
    void (*display_size)(void *user_data, int *w, int *h);
    void (*set_window_size)(void *user_data, int w, int h);
    void (*set_fullscreen)(void *user_data, bool fullscreen);

    /* System utilities */
    uint32_t (*get_ticks_ms)(void *user_data);
    void (*delay_ms)(void *user_data, uint32_t ms);
    bool (*directory_exists)(void *user_data, const char *path);
} sp_platform_interface_t;

/* Active platform. Initialized to the headless null interface. */
extern sp_platform_interface_t g_platform;

/* Headless no-op interface. Its clock is virtual: get_ticks_ms() only advances
 * through delay_ms(), keeping headless runs deterministic. Window and display are
 * 0 x 0; surfaces get unique handles; fonts fail (0). Its directory_exists is a
 * best-effort stdio probe; real hosts should override. */
sp_platform_interface_t sp_platform_null_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* SP_PLATFORM_H */

/* ------------------------------------------------------------------------- */
#ifdef SP_PLATFORM_IMPLEMENTATION
#ifndef SP_PLATFORM_IMPLEMENTATION_DONE
#define SP_PLATFORM_IMPLEMENTATION_DONE

#include <stdio.h>
#include <string.h>

static uint32_t sp__null_ticks;
static int sp__null_surfaces;

static bool sp__null_init(void *ud, int w, int h, int scale)
{
    (void)ud; (void)w; (void)h; (void)scale;
    sp__null_ticks = 0;
    return true;
}

static void sp__null_shutdown(void *ud) { (void)ud; }

static bool sp__null_poll_events(void *ud, sp_input_state_t *out_input)
{
    (void)ud;
    if (out_input != NULL) {
        memset(out_input, 0, sizeof(*out_input));
    }
    return true;
}

static void sp__null_begin_frame(void *ud, int w, int h) { (void)ud; (void)w; (void)h; }
static void sp__null_present(void *ud) { (void)ud; }
static void sp__null_clear(void *ud, uint32_t c, float a) { (void)ud; (void)c; (void)a; }

static bool sp__null_sprite_load(void *ud, int sprite, const char *path, int frames, int *w, int *h)
{
    (void)ud; (void)sprite; (void)path; (void)frames;
    if (w != NULL) {
        *w = 0;
    }
    if (h != NULL) {
        *h = 0;
    }
    return false;
}

static void sp__null_draw_image(void *ud, int sprite, int frame, int sx, int sy, int sw, int sh,
                                float x, float y, float xo, float yo, float xs, float ys,
                                float angle, uint32_t c, float a)
{
    (void)ud; (void)sprite; (void)frame; (void)sx; (void)sy; (void)sw; (void)sh;
    (void)x; (void)y; (void)xo; (void)yo; (void)xs; (void)ys; (void)angle; (void)c; (void)a;
}

static void sp__null_draw_rect(void *ud, float x1, float y1, float x2, float y2,
                               uint32_t c, float a, bool outline)
{
    (void)ud; (void)x1; (void)y1; (void)x2; (void)y2; (void)c; (void)a; (void)outline;
}

static void sp__null_draw_circle(void *ud, float x, float y, float r, uint32_t c, float a,
                                 bool outline)
{
    (void)ud; (void)x; (void)y; (void)r; (void)c; (void)a; (void)outline;
}

static int sp__null_font_load(void *ud, const char *path, int size)
{
    (void)ud; (void)path; (void)size;
    return 0;
}

static void sp__null_draw_text(void *ud, int font, const char *text, float x, float y,
                               float scale, uint32_t c, float a)
{
    (void)ud; (void)font; (void)text; (void)x; (void)y; (void)scale; (void)c; (void)a;
}

static int sp__null_surface_create(void *ud, int w, int h)
{
    (void)ud; (void)w; (void)h;
    if (sp__null_surfaces <= 0 || sp__null_surfaces == 0x7fffffff) {
        sp__null_surfaces = 0;
    }
    return ++sp__null_surfaces;
}

static void sp__null_surface_free(void *ud, int s) { (void)ud; (void)s; }
static void sp__null_surface_target(void *ud, int s) { (void)ud; (void)s; }

static void sp__null_draw_surface(void *ud, int s, float x, float y, float w, float h, float a)
{
    (void)ud; (void)s; (void)x; (void)y; (void)w; (void)h; (void)a;
}

static void sp__null_size(void *ud, int *w, int *h)
{
    (void)ud;
    if (w != NULL) {
        *w = 0;
    }
    if (h != NULL) {
        *h = 0;
    }
}

static void sp__null_set_window_size(void *ud, int w, int h) { (void)ud; (void)w; (void)h; }
static void sp__null_set_fullscreen(void *ud, bool f) { (void)ud; (void)f; }

static uint32_t sp__null_get_ticks_ms(void *ud)
{
    (void)ud;
    return sp__null_ticks;
}

static void sp__null_delay_ms(void *ud, uint32_t ms)
{
    (void)ud;
    sp__null_ticks += ms;
}

/* On POSIX stdio, fopen() of a directory succeeds but the first read fails
 * (EISDIR); on other C libraries fopen() fails and this reports false. */
static bool sp__null_directory_exists(void *ud, const char *path)
{
    FILE *fp;
    bool is_dir;

    (void)ud;
    if (path == NULL || path[0] == '\0') {
        return false;
    }
    fp = fopen(path, "rb");
    if (fp == NULL) {
        return false;
    }
    is_dir = (fgetc(fp) == EOF) && ferror(fp);
    fclose(fp);
    return is_dir;
}

#define SP__NULL_PLATFORM_TABLE                                                         \
    {                                                                                   \
        NULL, sp__null_init, sp__null_shutdown, sp__null_poll_events,                   \
        sp__null_begin_frame, sp__null_present, sp__null_clear,                         \
        sp__null_sprite_load, sp__null_draw_image, sp__null_draw_rect,                  \
        sp__null_draw_circle, sp__null_font_load, sp__null_draw_text,                   \
        sp__null_surface_create, sp__null_surface_free, sp__null_surface_target,        \
        sp__null_draw_surface, sp__null_size, sp__null_size, sp__null_set_window_size,  \
        sp__null_set_fullscreen, sp__null_get_ticks_ms, sp__null_delay_ms,              \
        sp__null_directory_exists                                                       \
    }

sp_platform_interface_t sp_platform_null_interface(void)
{
    sp_platform_interface_t p = SP__NULL_PLATFORM_TABLE;
    return p;
}

sp_platform_interface_t g_platform = SP__NULL_PLATFORM_TABLE;

#endif /* SP_PLATFORM_IMPLEMENTATION_DONE */
#endif /* SP_PLATFORM_IMPLEMENTATION */
