/*
 * main_sdl2.c - SDL2 host for the Spelunky Classic HD port (slice 10).
 *
 * Implements sp_platform (SDL2 renderer, SDL2_image, sprite fonts) and sp_audio
 * (SDL2_mixer), feeds the keyboard into gm_input and runs gm_loop_tick at the room
 * speed. It is the only file that knows about SDL; the runtime stays pure C99.
 * Host-side allocation (textures, caches) is outside the runtime's no-allocation rule.
 *
 *   spelunky [--assets DIR] [--data DIR] [--frames N] [--screenshot FILE.bmp]
 *            [--press FRAME:VK[:N]]... [--mute] [--item OBJECT]
 *
 *   --assets      build/assets (sprites/<name>/<i>.png, sounds/<name>)
 *   --data        the game's datafiles directory, used as working_directory
 *   --frames N    run N frames and quit (with --screenshot: save the last frame)
 *   --press F:VK[:N]  hold GameMaker key VK from frame F for N frames (default 3)
 *   --mute        no audio device (also used when none can be opened)
 *   --item OBJ    debug: pressing O (or --press F:79) spawns an instance of object
 *                 OBJ (e.g. oJar) at the player
 *
 * Headless runs: SDL_VIDEODRIVER=offscreen (or dummy) with --frames.
 */
#include "gml_assets.h"
#include "gml_rt.h"

#include "gm_audio.h"
#include "gm_draw.h"
#include "sp_audio.h"
#include "sp_platform.h"

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_mixer.h>


#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SPRITE_SLOTS (GM_SPRITE_MAX + GM_DRAW_ADDED_SPRITE_MAX)
#define SURFACE_MAX 64
#define FONT_SMALL 1
#define FONT_LARGE 2
#define CHANNELS 32
#define PRESS_MAX 64
#define PATH_MAX_LEN 1024

/* Default --assets / --data; standalone builds (tools/package_src.py) override them. */
#ifndef SP_DEFAULT_ASSETS_DIR
#define SP_DEFAULT_ASSETS_DIR "build/assets"
#endif
#ifndef SP_DEFAULT_DATA_DIR
#define SP_DEFAULT_DATA_DIR "build/data"
#endif
#define PATH_BUF (2 * PATH_MAX_LEN + 128)
#define HOST_PI 3.14159265358979323846

typedef struct sprite_tex {
    bool tried;
    int count;              /* frame textures, or 1 strip texture */
    int strip_w;            /* > 0: one strip texture, frames side by side */
    SDL_Texture **frames;
} sprite_tex_t;

typedef struct sprite_font {
    int sprite;   /* -1: not loaded */
    int w, h;     /* glyph cell = advance / line height */
    int glyphs;
} sprite_font_t;

typedef struct press {
    int frame, key, hold;
} press_t;

static struct host {
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Texture *app;
    int app_w, app_h;
    char assets[PATH_MAX_LEN];
    char data[PATH_MAX_LEN];
    sprite_tex_t sprites[SPRITE_SLOTS];
    SDL_Texture *surfaces[SURFACE_MAX + 1];
    sprite_font_t fonts[FONT_LARGE + 1];   /* [FONT_SMALL], [FONT_LARGE] */
    const char *screenshot;
    bool want_shot;
    /* audio */
    bool audio;
    Mix_Chunk *chunks[GML_SOUND_COUNT + 1];
    bool chunk_tried[GML_SOUND_COUNT + 1];
    float sound_gain[GML_SOUND_COUNT + 1];
    int ch_sound[CHANNELS];
    int ch_handle[CHANNELS];
    float ch_gain[CHANNELS];
    float master;
    int next_handle;
    press_t presses[PRESS_MAX];
    int press_count;
    int item_object; /* --item object index, -1 if none */
    bool spawn_item; /* O pressed since the last tick */
} H;

/* ===================================================================== helpers */

static void colour_mod(SDL_Texture *t, uint32_t c, float alpha)
{
    SDL_SetTextureColorMod(t, (Uint8)(c & 0xFFu), (Uint8)((c >> 8) & 0xFFu), (Uint8)((c >> 16) & 0xFFu));
    SDL_SetTextureAlphaMod(t, (Uint8)lround(fmax(0.0, fmin(1.0, alpha)) * 255.0));
}

static void draw_colour(uint32_t c, float alpha)
{
    SDL_SetRenderDrawBlendMode(H.ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(H.ren, (Uint8)(c & 0xFFu), (Uint8)((c >> 8) & 0xFFu), (Uint8)((c >> 16) & 0xFFu),
                           (Uint8)lround(fmax(0.0, fmin(1.0, alpha)) * 255.0));
}

static SDL_Texture *load_texture(const char *path)
{
    SDL_Texture *t = IMG_LoadTexture(H.ren, path);
    if (t != NULL) {
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    }
    return t;
}

/* ===================================================================== platform: frame */

static void p_begin_frame(void *ud, int w, int h)
{
    (void)ud;
    if (w < 1 || h < 1) {
        w = 320;
        h = 240;
    }
    if (H.app == NULL || w != H.app_w || h != H.app_h) {
        if (H.app != NULL) {
            SDL_DestroyTexture(H.app);
        }
        H.app = SDL_CreateTexture(H.ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w, h);
        H.app_w = w;
        H.app_h = h;
    }
    SDL_SetRenderTarget(H.ren, H.app);
}

static void save_screenshot(void)
{
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, H.app_w, H.app_h, 32, SDL_PIXELFORMAT_ARGB8888);

    if (s == NULL) {
        return;
    }
    SDL_SetRenderTarget(H.ren, H.app);
    if (SDL_RenderReadPixels(H.ren, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch) == 0 &&
        SDL_SaveBMP(s, H.screenshot) == 0) {
        printf("host: saved %s (%dx%d)\n", H.screenshot, H.app_w, H.app_h);
    }
    SDL_FreeSurface(s);
}

/* The application surface, scaled to fit and centred (letterboxed). */
static void p_present(void *ud)
{
    int ww, wh;
    float scale;
    SDL_FRect dst;

    (void)ud;
    if (H.want_shot && H.screenshot != NULL) {
        save_screenshot();
        H.want_shot = false;
    }
    SDL_SetRenderTarget(H.ren, NULL);
    SDL_SetRenderDrawColor(H.ren, 0, 0, 0, 255);
    SDL_RenderClear(H.ren);
    SDL_GetRendererOutputSize(H.ren, &ww, &wh);
    scale = fmin((float)ww / H.app_w, (float)wh / H.app_h);
    dst.w = (float)(H.app_w * scale);
    dst.h = (float)(H.app_h * scale);
    dst.x = (float)(((float)ww - H.app_w * scale) / 2.0);
    dst.y = (float)(((float)wh - H.app_h * scale) / 2.0);
    SDL_RenderCopyF(H.ren, H.app, NULL, &dst);
    SDL_RenderPresent(H.ren);
}

/* RenderClear writes the colour as is (no blending), alpha included. */
static void p_clear(void *ud, uint32_t c, float alpha)
{
    (void)ud;
    SDL_SetRenderDrawColor(H.ren, (Uint8)(c & 0xFFu), (Uint8)((c >> 8) & 0xFFu), (Uint8)((c >> 16) & 0xFFu),
                           (Uint8)lround(fmax(0.0, fmin(1.0, alpha)) * 255.0));
    SDL_RenderClear(H.ren);
}

/* ===================================================================== platform: images */

static bool p_sprite_load(void *ud, int sprite, const char *path, int frames, int *w, int *h)
{
    sprite_tex_t *s;
    SDL_Texture *t;
    int tw, th;

    (void)ud;
    if (sprite < 0 || sprite >= SPRITE_SLOTS || frames < 1 || (t = load_texture(path)) == NULL) {
        return false;
    }
    SDL_QueryTexture(t, NULL, NULL, &tw, &th);
    s = &H.sprites[sprite];
    s->tried = true;
    s->count = 1;
    s->strip_w = tw / frames > 0 ? tw / frames : tw;
    s->frames = malloc(sizeof(SDL_Texture *));
    if (s->frames == NULL) {
        SDL_DestroyTexture(t);
        return false;
    }
    s->frames[0] = t;
    *w = s->strip_w;
    *h = th;
    return true;
}

/* Static sprites: ASSET_DIR/sprites/<name>/<frame>.png, loaded on first use. */
static SDL_Texture *frame_texture(int sprite, int frame, int *xoff)
{
    sprite_tex_t *s;
    const gm_sprite_def_t *def;
    int i;

    *xoff = 0;
    if (sprite < 0 || sprite >= SPRITE_SLOTS) {
        return NULL;
    }
    s = &H.sprites[sprite];
    if (!s->tried) {
        s->tried = true;
        def = gm_sprite_get(sprite);
        if (def != NULL && def->frame_count > 0) {
            s->frames = calloc((size_t)def->frame_count, sizeof(SDL_Texture *));
            if (s->frames != NULL) {
                s->count = def->frame_count;
                for (i = 0; i < s->count; ++i) {
                    char path[PATH_BUF];
                    snprintf(path, sizeof(path), "%s/sprites/%s/%d.png", H.assets, def->name, i);
                    s->frames[i] = load_texture(path);
                }
            }
        }
    }
    if (s->strip_w > 0) {
        *xoff = frame * s->strip_w;
        return s->count > 0 ? s->frames[0] : NULL;
    }
    return (frame >= 0 && frame < s->count) ? s->frames[frame] : NULL;
}

static void p_draw_image(void *ud, int sprite, int frame, int sx, int sy, int sw, int sh, float x, float y,
                         float xo, float yo, float xs, float ys, float angle, uint32_t c, float alpha)
{
    int xoff, tw, th;
    SDL_Texture *t = frame_texture(sprite, frame, &xoff);
    SDL_Rect src;
    SDL_FRect dst;
    SDL_FPoint centre;
    float axs = fabs(xs), ays = fabs(ys), ox, oy;
    int flip = SDL_FLIP_NONE;

    (void)ud;
    if (t == NULL || sw <= 0 || sh <= 0 || axs == 0.0 || ays == 0.0) {
        return;
    }
    SDL_QueryTexture(t, NULL, NULL, &tw, &th);
    src.x = sx + xoff;
    src.y = sy;
    src.w = sw;
    src.h = sh;
    if (src.x + src.w > tw) {   /* a frame PNG smaller than its sprite */
        src.w = tw - src.x;
    }
    if (src.y + src.h > th) {
        src.h = th - src.y;
    }
    if (src.w <= 0 || src.h <= 0) {
        return;
    }
    ox = (xs < 0.0 ? (float)sw - xo : xo) * axs;
    oy = (ys < 0.0 ? (float)sh - yo : yo) * ays;
    if (xs < 0.0) {
        flip |= SDL_FLIP_HORIZONTAL;
    }
    if (ys < 0.0) {
        flip |= SDL_FLIP_VERTICAL;
    }
    dst.x = (float)(x - ox);
    dst.y = (float)(y - oy);
    dst.w = (float)(src.w * axs);
    dst.h = (float)(src.h * ays);
    centre.x = (float)ox;
    centre.y = (float)oy;
    colour_mod(t, c, alpha);
    SDL_RenderCopyExF(H.ren, t, &src, &dst, -angle, &centre, (SDL_RendererFlip)flip);
}

/* draw_rectangle covers x1..x2 and y1..y2 inclusive. */
static void p_draw_rect(void *ud, float x1, float y1, float x2, float y2, uint32_t c, float alpha, bool outline)
{
    SDL_FRect r;

    (void)ud;
    r.x = (float)fmin(x1, x2);
    r.y = (float)fmin(y1, y2);
    r.w = (float)(fabs(x2 - x1) + 1.0);
    r.h = (float)(fabs(y2 - y1) + 1.0);
    draw_colour(c, alpha);
    if (outline) {
        SDL_RenderDrawRectF(H.ren, &r);
    } else {
        SDL_RenderFillRectF(H.ren, &r);
    }
}

static void p_draw_circle(void *ud, float x, float y, float r, uint32_t c, float alpha, bool outline)
{
    (void)ud;
    draw_colour(c, alpha);
    if (r <= 0.0) {
        return;
    }
    if (outline) {
        SDL_FPoint pts[49];
        int i;
        for (i = 0; i <= 48; ++i) {
            float a = i * (2.0 * HOST_PI / 48.0);
            pts[i].x = (float)(x + r * cos(a));
            pts[i].y = (float)(y + r * sin(a));
        }
        SDL_RenderDrawLinesF(H.ren, pts, 49);
    } else {
        float dy;
        for (dy = -floor(r); dy <= floor(r); dy += 1.0) {
            float dx = sqrt(r * r - dy * dy);
            SDL_RenderDrawLineF(H.ren, (float)(x - dx), (float)(y + dy), (float)(x + dx), (float)(y + dy));
        }
    }
}

/* ===================================================================== platform: text */

/*
 * Sprite fonts (font_add_sprite_ext with the English charset): sFont (large) and
 * sFontSmall. Frame i is the glyph for character FONT_FIRST + i; lower case is drawn
 * as upper case. Platform handles: FONT_LARGE, FONT_SMALL; 0 (no font) = small.
 */
#define FONT_FIRST ' '
#define FONT_LAST 'Z'

static int sprite_by_name(const char *name)
{
    int i;

    for (i = 0; i < GM_SPRITE_MAX; ++i) {
        const gm_sprite_def_t *def = gm_sprite_get(i);
        if (def != NULL && def->name != NULL && strcmp(def->name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static bool font_init(sprite_font_t *f, const char *name)
{
    const gm_sprite_def_t *def;

    f->sprite = sprite_by_name(name);
    def = gm_sprite_get(f->sprite);
    if (def == NULL || def->width <= 0 || def->height <= 0) {
        f->sprite = -1;
        fprintf(stderr, "host: no sprite font %s\n", name);
        return false;
    }
    f->w = def->width;
    f->h = def->height;
    f->glyphs = def->frame_count;
    return true;
}

/* font_add: "sFont" selects the large sprite font; a locale TTF (size from
 * setLocale: 24 / 12) maps by size; anything else is the small font. */
static int p_font_load(void *ud, const char *path, int size)
{
    const char *base;

    (void)ud;
    if (path == NULL) {
        return FONT_SMALL;
    }
    base = strrchr(path, '/');
    base = base != NULL ? base + 1 : path;
    if (strcmp(base, "sFont") == 0) {
        return FONT_LARGE;
    }
    if (strcmp(base, "sFontSmall") == 0) {
        return FONT_SMALL;
    }
    return size >= 16 ? FONT_LARGE : FONT_SMALL;
}

static void p_draw_text(void *ud, int font, const char *text, float x, float y, float scale, uint32_t c,
                        float alpha)
{
    const sprite_font_t *f = &H.fonts[font == FONT_LARGE ? FONT_LARGE : FONT_SMALL];
    float lx = x, ly = y;

    if (text == NULL || f->sprite < 0) {
        return;
    }
    for (; *text != '\0'; ++text) {
        int ch = (unsigned char)*text;
        if (ch == '\r' || ch == '\n') {
            if (ch == '\r' && text[1] == '\n') {
                text++;
            }
            lx = x;
            ly += f->h * scale;
            continue;
        }
        if (ch >= 'a' && ch <= 'z') {
            ch -= 'a' - 'A';
        }
        if (ch > FONT_FIRST && ch <= FONT_LAST && ch - FONT_FIRST < f->glyphs) {
            p_draw_image(ud, f->sprite, ch - FONT_FIRST, 0, 0, f->w, f->h, lx, ly, 0.0, 0.0, scale, scale, 0.0, c,
                         alpha);
        }
        lx += f->w * scale;
    }
}

/* ===================================================================== platform: surfaces */

static int p_surface_create(void *ud, int w, int h)
{
    int i;
    SDL_Texture *prev = SDL_GetRenderTarget(H.ren);

    (void)ud;
    for (i = 1; i <= SURFACE_MAX && H.surfaces[i] != NULL; ++i) {
    }
    if (i > SURFACE_MAX) {
        return 0;
    }
    H.surfaces[i] = SDL_CreateTexture(H.ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w, h);
    if (H.surfaces[i] == NULL) {
        return 0;
    }
    SDL_SetTextureBlendMode(H.surfaces[i], SDL_BLENDMODE_BLEND);
    SDL_SetRenderTarget(H.ren, H.surfaces[i]);
    SDL_SetRenderDrawColor(H.ren, 0, 0, 0, 0);
    SDL_RenderClear(H.ren);
    SDL_SetRenderTarget(H.ren, prev);
    return i;
}

static void p_surface_free(void *ud, int s)
{
    (void)ud;
    if (s >= 1 && s <= SURFACE_MAX && H.surfaces[s] != NULL) {
        SDL_DestroyTexture(H.surfaces[s]);
        H.surfaces[s] = NULL;
    }
}

static void p_surface_target(void *ud, int s)
{
    (void)ud;
    SDL_SetRenderTarget(H.ren, (s >= 1 && s <= SURFACE_MAX && H.surfaces[s] != NULL) ? H.surfaces[s] : H.app);
}

static void p_draw_surface(void *ud, int s, float x, float y, float w, float h, float alpha)
{
    SDL_FRect dst;

    (void)ud;
    if (s < 1 || s > SURFACE_MAX || H.surfaces[s] == NULL) {
        return;
    }
    dst.x = (float)x;
    dst.y = (float)y;
    dst.w = (float)w;
    dst.h = (float)h;
    colour_mod(H.surfaces[s], 0xFFFFFFu, alpha);
    SDL_RenderCopyF(H.ren, H.surfaces[s], NULL, &dst);
}

/* ===================================================================== platform: window */

static void p_window_size(void *ud, int *w, int *h)
{
    (void)ud;
    SDL_GetWindowSize(H.win, w, h);
}

static void p_display_size(void *ud, int *w, int *h)
{
    SDL_DisplayMode m;

    (void)ud;
    if (SDL_GetDesktopDisplayMode(0, &m) == 0 && m.w > 0 && m.h > 0) {
        *w = m.w;
        *h = m.h;
    } else {
        *w = 1920;   /* offscreen / dummy drivers report nothing useful */
        *h = 1080;
    }
}

static void p_set_window_size(void *ud, int w, int h)
{
    (void)ud;
    SDL_SetWindowSize(H.win, w, h);
    SDL_SetWindowPosition(H.win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
}

static void p_set_fullscreen(void *ud, bool on)
{
    (void)ud;
    SDL_SetWindowFullscreen(H.win, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}

static uint32_t p_ticks(void *ud)
{
    (void)ud;
    return SDL_GetTicks();
}

static void p_delay(void *ud, uint32_t ms)
{
    (void)ud;
    SDL_Delay(ms);
}

/* ===================================================================== audio (SDL2_mixer) */

static Mix_Chunk *chunk(int sound)
{
    if (!H.audio || sound < 0 || sound >= GML_SOUND_COUNT) {
        return NULL;
    }
    if (!H.chunk_tried[sound]) {
        char path[PATH_BUF];
        H.chunk_tried[sound] = true;
        snprintf(path, sizeof(path), "%s/sounds/%s", H.assets, g_gml_sound_defs[sound].name);
        H.chunks[sound] = Mix_LoadWAV(path);
        if (H.chunks[sound] == NULL) {
            fprintf(stderr, "host: sound %s: %s\n", g_gml_sound_defs[sound].name, Mix_GetError());
        }
    }
    return H.chunks[sound];
}

static void apply_volume(int ch)
{
    int s = H.ch_sound[ch];
    float v = H.master * H.ch_gain[ch] * (s >= 0 ? H.sound_gain[s] * g_gml_sound_defs[s].volume : 1.0);
    Mix_Volume(ch, (int)lround(fmax(0.0, fmin(1.0, v)) * MIX_MAX_VOLUME));
}

static int a_play(void *ud, int sound, float volume, float pan, bool loop)
{
    Mix_Chunk *c = chunk(sound);
    int ch;

    (void)ud;
    (void)pan;
    if (c == NULL || (ch = Mix_PlayChannel(-1, c, loop ? -1 : 0)) < 0 || ch >= CHANNELS) {
        return -1;
    }
    H.ch_sound[ch] = sound;
    H.ch_gain[ch] = volume;
    H.ch_handle[ch] = GM_AUDIO_HANDLE_BASE + H.next_handle++;
    apply_volume(ch);
    return H.ch_handle[ch];
}

/* Channels a sound id or a playing-instance handle refers to. */
static bool matches(int ch, int id)
{
    return Mix_Playing(ch) && (id >= GM_AUDIO_HANDLE_BASE ? H.ch_handle[ch] == id : H.ch_sound[ch] == id);
}

static void a_stop(void *ud, int id)
{
    int ch;
    (void)ud;
    for (ch = 0; H.audio && ch < CHANNELS; ++ch) {
        if (matches(ch, id)) {
            Mix_HaltChannel(ch);
        }
    }
}

static void a_stop_all(void *ud)
{
    (void)ud;
    if (H.audio) {
        Mix_HaltChannel(-1);
    }
}

static void a_volume(void *ud, int id, float v)
{
    int ch;
    (void)ud;
    if (id >= 0 && id < GML_SOUND_COUNT) {
        H.sound_gain[id] = v;
    }
    for (ch = 0; H.audio && ch < CHANNELS; ++ch) {
        if (matches(ch, id)) {
            if (id >= GM_AUDIO_HANDLE_BASE) {
                H.ch_gain[ch] = v;
            }
            apply_volume(ch);
        }
    }
}

static bool a_playing(void *ud, int id)
{
    int ch;
    (void)ud;
    for (ch = 0; H.audio && ch < CHANNELS; ++ch) {
        if (matches(ch, id)) {
            return true;
        }
    }
    return false;
}

static void a_pause(void *ud)
{
    (void)ud;
    if (H.audio) {
        Mix_Pause(-1);
    }
}

static void a_resume(void *ud)
{
    (void)ud;
    if (H.audio) {
        Mix_Resume(-1);
    }
}

static void a_master(void *ud, float v)
{
    int ch;
    (void)ud;
    H.master = v;
    for (ch = 0; H.audio && ch < CHANNELS; ++ch) {
        apply_volume(ch);
    }
}

/* ===================================================================== input */

/* SDL keycode -> GameMaker virtual key (browser keyCode values), 0 if unmapped. */
static int vk_of(SDL_Keycode k)
{
    if (k >= SDLK_a && k <= SDLK_z) {
        return 'A' + (int)(k - SDLK_a);
    }
    if (k >= SDLK_0 && k <= SDLK_9) {
        return '0' + (int)(k - SDLK_0);
    }
    if (k >= SDLK_F1 && k <= SDLK_F12) {
        return 112 + (int)(k - SDLK_F1);
    }
    if (k >= SDLK_KP_1 && k <= SDLK_KP_9) {
        return 97 + (int)(k - SDLK_KP_1);
    }
    switch (k) {
    case SDLK_KP_0: return 96;
    case SDLK_LEFT: return 37;
    case SDLK_UP: return 38;
    case SDLK_RIGHT: return 39;
    case SDLK_DOWN: return 40;
    case SDLK_RETURN: case SDLK_KP_ENTER: return 13;
    case SDLK_ESCAPE: return 27;
    case SDLK_SPACE: return 32;
    case SDLK_BACKSPACE: return 8;
    case SDLK_TAB: return 9;
    case SDLK_LSHIFT: case SDLK_RSHIFT: return 16;
    case SDLK_LCTRL: case SDLK_RCTRL: return 17;
    case SDLK_LALT: case SDLK_RALT: return 18;
    case SDLK_PAUSE: return 19;
    case SDLK_PAGEUP: return 33;
    case SDLK_PAGEDOWN: return 34;
    case SDLK_END: return 35;
    case SDLK_HOME: return 36;
    case SDLK_INSERT: return 45;
    case SDLK_DELETE: return 46;
    default: return 0;
    }
}

/* Returns false on quit. */
static bool pump_events(void)
{
    SDL_Event ev;

    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) {
            return false;
        }
        if (ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) {
            int vk = vk_of(ev.key.keysym.sym);
            if (ev.type == SDL_KEYDOWN && !ev.key.repeat && ev.key.keysym.sym == SDLK_o && H.item_object >= 0) {
                H.spawn_item = true;
            }
            if (vk == 0) {
                continue;
            }
            if (ev.type == SDL_KEYDOWN) {
                gm_input_key_down(vk);
            } else {
                gm_input_key_up(vk);
            }
        }
    }
    return true;
}

static int object_by_name(const char *name)
{
    int i, n = gm_object_count();

    for (i = 0; i < n; ++i) {
        const gm_object_def_t *def = gm_object_get(i);
        if (def && def->name && strcmp(def->name, name) == 0) {
            return i;
        }
    }
    return -1;
}

/* Spawns the --item object on the first player instance. */
static void spawn_debug_item(void)
{
    static int player = -2;
    gm_instance_t *p;

    H.spawn_item = false;
    if (player == -2) {
        player = object_by_name("oPlayer1");
    }
    p = player >= 0 ? gm_instance_find(player, 0, NULL, NULL) : NULL;
    if (!p) {
        fprintf(stderr, "host: --item: no player in this room\n");
        return;
    }
    /* At the player's own position: a tile beside them may be solid. */
    gm_instance_create(p->x + 40, p->y, H.item_object);
}

static void scripted_input(int frame)
{
    int i;

    for (i = 0; i < H.press_count; ++i) {
        if (H.presses[i].frame == frame) {
            gm_input_key_down(H.presses[i].key);
            if (H.presses[i].key == 'O' && H.item_object >= 0) {
                H.spawn_item = true;
            }
        } else if (H.presses[i].frame + H.presses[i].hold == frame) {
            gm_input_key_up(H.presses[i].key);
        }
    }
}

/* ===================================================================== setup */

static void install_platform(void)
{
    g_platform.begin_frame = p_begin_frame;
    g_platform.present = p_present;
    g_platform.clear = p_clear;
    g_platform.sprite_load = p_sprite_load;
    g_platform.draw_image = p_draw_image;
    g_platform.draw_rect = p_draw_rect;
    g_platform.draw_circle = p_draw_circle;
    g_platform.font_load = p_font_load;
    g_platform.draw_text = p_draw_text;
    g_platform.surface_create = p_surface_create;
    g_platform.surface_free = p_surface_free;
    g_platform.surface_target = p_surface_target;
    g_platform.draw_surface = p_draw_surface;
    g_platform.window_size = p_window_size;
    g_platform.display_size = p_display_size;
    g_platform.set_window_size = p_set_window_size;
    g_platform.set_fullscreen = p_set_fullscreen;
    g_platform.get_ticks_ms = p_ticks;
    g_platform.delay_ms = p_delay;
}

static void install_audio(bool mute)
{
    int i;

    H.master = 1.0;
    for (i = 0; i <= GML_SOUND_COUNT; ++i) {
        H.sound_gain[i] = 1.0;
    }
    for (i = 0; i < CHANNELS; ++i) {
        H.ch_sound[i] = -1;
        H.ch_gain[i] = 1.0;
    }
    if (!mute && Mix_OpenAudio(44100, AUDIO_S16SYS, 2, 1024) == 0) {
        Mix_AllocateChannels(CHANNELS);
        H.audio = true;
    } else if (!mute) {
        fprintf(stderr, "host: no audio (%s)\n", Mix_GetError());
    }
    g_audio.play_sound = a_play;
    g_audio.stop_sound = a_stop;
    g_audio.stop_all_sounds = a_stop_all;
    g_audio.set_sound_volume = a_volume;
    g_audio.is_playing = a_playing;
    g_audio.pause_all = a_pause;
    g_audio.resume_all = a_resume;
    g_audio.set_master_volume = a_master;
}

static const gm_room_hooks_t s_room_hooks = { gml_perform_event, gml_room_begin };
static const gm_loop_hooks_t s_loop_hooks = { gml_find_event, gml_find_own_event, gm_draw_self,
                                              gml_collect_garbage };

static bool install_game(void)
{

    gm_instance_system_reset();
    gm_heap_reset();
    gm_ds_reset();
    gm_input_reset();
    gm_draw_reset();
    gml_rt_reset();
    gml_rt_install(&gml_game);
    if (!gml_assets_install()) {
        return false;
    }
    gm_collision_init();
    gm_collision_index_default_roots();
    gm_room_set_hooks(&s_room_hooks);
    if (!gm_room_registry_init(g_gml_room_defs, RM_COUNT_, g_gml_room_order, GML_ROOM_ORDER_COUNT) ||
        !gm_loop_init(&s_loop_hooks)) {
        return false;
    }
    gm_set_working_directory(H.data);
    /* Sprite fonts, in this order: font id 0 = sFont, font id 1 = sFontSmall. */
    font_init(&H.fonts[FONT_LARGE], "sFont");
    font_init(&H.fonts[FONT_SMALL], "sFontSmall");
    if (gm_draw_font_add("sFont", 16) != 0 || gm_draw_font_add("sFontSmall", 8) != 1) {
        fprintf(stderr, "host: sprite fonts did not get ids 0 and 1\n");
    }
    gml_rt_global_init();
    return gm_room_start_game();
}

static void shutdown_all(void)
{
    int i, j;

    for (i = 0; i < SPRITE_SLOTS; ++i) {
        for (j = 0; j < H.sprites[i].count; ++j) {
            if (H.sprites[i].frames[j] != NULL) {
                SDL_DestroyTexture(H.sprites[i].frames[j]);
            }
        }
        free(H.sprites[i].frames);
    }

    for (i = 0; i < GML_SOUND_COUNT; ++i) {
        if (H.chunks[i] != NULL) {
            Mix_FreeChunk(H.chunks[i]);
        }
    }
    if (H.audio) {
        Mix_CloseAudio();
    }
    if (H.ren != NULL) {
        SDL_DestroyRenderer(H.ren);
    }
    if (H.win != NULL) {
        SDL_DestroyWindow(H.win);
    }

    IMG_Quit();
    SDL_Quit();
}

int main(int argc, char **argv)
{
    long frames = -1, frame;
    bool mute = false;
    const char *item_name = NULL;
    int i;

    H.item_object = -1;

    snprintf(H.assets, sizeof(H.assets), "%s", SP_DEFAULT_ASSETS_DIR);
    snprintf(H.data, sizeof(H.data), "%s", SP_DEFAULT_DATA_DIR);
    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--assets") == 0 && i + 1 < argc) {
            snprintf(H.assets, sizeof(H.assets), "%s", argv[++i]);
        } else if (strcmp(argv[i], "--data") == 0 && i + 1 < argc) {
            snprintf(H.data, sizeof(H.data), "%s", argv[++i]);
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            frames = strtol(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            H.screenshot = argv[++i];
        } else if (strcmp(argv[i], "--press") == 0 && i + 1 < argc && H.press_count < PRESS_MAX) {
            press_t *p = &H.presses[H.press_count];
            p->hold = 3;
            if (sscanf(argv[++i], "%d:%d:%d", &p->frame, &p->key, &p->hold) >= 2 && p->hold > 0) {
                H.press_count++;
            }
        } else if (strcmp(argv[i], "--mute") == 0) {
            mute = true;
        } else if (strcmp(argv[i], "--item") == 0 && i + 1 < argc) {
            item_name = argv[++i];
        } else {
            fprintf(stderr, "usage: %s [--assets DIR] [--data DIR] [--frames N] [--screenshot FILE.bmp] "
                            "[--press FRAME:VK[:N]]... [--mute] [--item OBJECT]\n", argv[0]);
            return 2;
        }
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "host: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    IMG_Init(IMG_INIT_PNG);

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    H.win = SDL_CreateWindow("Spelunky", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 960, 540,
                             SDL_WINDOW_RESIZABLE);
    H.ren = H.win != NULL ? SDL_CreateRenderer(H.win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE)
                          : NULL;
    if (H.ren == NULL) {
        H.ren = H.win != NULL ? SDL_CreateRenderer(H.win, -1, SDL_RENDERER_SOFTWARE) : NULL;
    }
    if (H.ren == NULL) {
        fprintf(stderr, "host: no renderer: %s\n", SDL_GetError());
        shutdown_all();
        return 1;
    }
    install_platform();
    install_audio(mute);
    if (!install_game()) {
        fprintf(stderr, "host: the game tables did not install\n");
        shutdown_all();
        return 1;
    }
    if (item_name != NULL) {
        H.item_object = object_by_name(item_name);
        if (H.item_object < 0) {
            fprintf(stderr, "host: --item: unknown object '%s'\n", item_name);
            shutdown_all();
            return 2;
        }
        printf("host: press O to spawn %s\n", item_name);
    }

    for (frame = 1; frames < 0 || frame <= frames; ++frame) {
        Uint64 start = SDL_GetPerformanceCounter();
        float budget = 1000.0 / (gm_room_speed() > 0.0 ? gm_room_speed() : 30.0);
        float used;

        if (!pump_events()) {
            break;
        }
        scripted_input((int)frame);
        if (H.spawn_item) {
            spawn_debug_item();
        }
        H.want_shot = frame == frames;
        if (gm_loop_tick() == GM_LOOP_ENDED) {
            break;
        }
        if (frames < 0) {
            used = (float)(SDL_GetPerformanceCounter() - start) * 1000.0 / (float)SDL_GetPerformanceFrequency();
            if (used < budget) {
                SDL_Delay((Uint32)(budget - used));
            }
        }
    }
    if (frames >= 0) {
        printf("host: %ld frames, room %s, %d instances\n", gm_loop_frame_count(),
               gm_room_get_name(gm_room_current()), gm_instance_count());
    }
    shutdown_all();
    return 0;
}
