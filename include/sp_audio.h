/*
 * sp_audio.h - single-header audio abstraction.
 *
 * The runtime plays sound only through `g_audio`. By default it holds
 * headless no-op stubs that hand out dummy (unique, positive) sound handles.
 * Hosts (e.g. a miniaudio driver) assign their own table at startup.
 *
 * Exactly one translation unit must define SP_AUDIO_IMPLEMENTATION before
 * including this header (see src/runtime/sp_stubs.c).
 *
 * Additions to the sketch in agents/SPELUNKY_C.md section 3.2, driven by the game's
 * calls: `user_data` in the interface, pause_all/resume_all
 * (audio_pause_all/audio_resume_all), is_playing (audio_is_playing,
 * sound_isplaying) and set_master_volume (audio_master_gain,
 * sound_global_volume).
 */
#ifndef SP_AUDIO_H
#define SP_AUDIO_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sp_audio_interface {
    void *user_data; /* forwarded as the first argument of every hook */

    bool (*init)(void *user_data);
    void (*shutdown)(void *user_data);

    /* Sound effects. play_sound returns a handle > 0, or <= 0 on failure. */
    int  (*play_sound)(void *user_data, int sound_id, float volume, float pan, bool loop);
    void (*stop_sound)(void *user_data, int sound_handle);
    void (*stop_all_sounds)(void *user_data);
    void (*set_sound_volume)(void *user_data, int sound_handle, float volume);
    bool (*is_playing)(void *user_data, int sound_id_or_handle);
    void (*pause_all)(void *user_data);
    void (*resume_all)(void *user_data);
    void (*set_master_volume)(void *user_data, float volume);

    /* Music */
    void (*play_music)(void *user_data, int music_id, bool loop);
    void (*stop_music)(void *user_data);
} sp_audio_interface_t;

/* Active audio backend. Initialized to the headless null interface. */
extern sp_audio_interface_t g_audio;

sp_audio_interface_t sp_audio_null_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* SP_AUDIO_H */

/* ------------------------------------------------------------------------- */
#ifdef SP_AUDIO_IMPLEMENTATION
#ifndef SP_AUDIO_IMPLEMENTATION_DONE
#define SP_AUDIO_IMPLEMENTATION_DONE

static int sp__null_next_handle;

static bool sp__null_audio_init(void *ud)
{
    (void)ud;
    sp__null_next_handle = 0;
    return true;
}

static void sp__null_audio_shutdown(void *ud) { (void)ud; }

static int sp__null_play_sound(void *ud, int sound_id, float volume, float pan, bool loop)
{
    (void)ud; (void)sound_id; (void)volume; (void)pan; (void)loop;
    if (sp__null_next_handle <= 0 || sp__null_next_handle == 0x7fffffff) {
        sp__null_next_handle = 0;
    }
    return ++sp__null_next_handle;
}

static void sp__null_stop_sound(void *ud, int h) { (void)ud; (void)h; }
static void sp__null_stop_all_sounds(void *ud) { (void)ud; }
static void sp__null_set_sound_volume(void *ud, int h, float v) { (void)ud; (void)h; (void)v; }
static bool sp__null_is_playing(void *ud, int h) { (void)ud; (void)h; return false; }
static void sp__null_pause_all(void *ud) { (void)ud; }
static void sp__null_resume_all(void *ud) { (void)ud; }
static void sp__null_set_master_volume(void *ud, float v) { (void)ud; (void)v; }
static void sp__null_play_music(void *ud, int id, bool loop) { (void)ud; (void)id; (void)loop; }
static void sp__null_stop_music(void *ud) { (void)ud; }

sp_audio_interface_t sp_audio_null_interface(void)
{
    sp_audio_interface_t a;

    a.user_data = NULL;
    a.init = sp__null_audio_init;
    a.shutdown = sp__null_audio_shutdown;
    a.play_sound = sp__null_play_sound;
    a.stop_sound = sp__null_stop_sound;
    a.stop_all_sounds = sp__null_stop_all_sounds;
    a.set_sound_volume = sp__null_set_sound_volume;
    a.is_playing = sp__null_is_playing;
    a.pause_all = sp__null_pause_all;
    a.resume_all = sp__null_resume_all;
    a.set_master_volume = sp__null_set_master_volume;
    a.play_music = sp__null_play_music;
    a.stop_music = sp__null_stop_music;
    return a;
}

sp_audio_interface_t g_audio = {
    NULL,
    sp__null_audio_init,
    sp__null_audio_shutdown,
    sp__null_play_sound,
    sp__null_stop_sound,
    sp__null_stop_all_sounds,
    sp__null_set_sound_volume,
    sp__null_is_playing,
    sp__null_pause_all,
    sp__null_resume_all,
    sp__null_set_master_volume,
    sp__null_play_music,
    sp__null_stop_music
};

#endif /* SP_AUDIO_IMPLEMENTATION_DONE */
#endif /* SP_AUDIO_IMPLEMENTATION */
