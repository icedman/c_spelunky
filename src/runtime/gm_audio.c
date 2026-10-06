/*
 * gm_audio - see gm_audio.h.
 */
#include "gm_audio.h"

#include "gm_math.h"
#include "sp_audio.h"

#define A g_audio.user_data

static int id_of(float v)
{
    return (int)gm_to_int32(v);
}

float gm_audio_play_sound(float sound, float priority, float loop)
{
    int s = id_of(sound);

    (void)priority;
    if (s < 0) {
        return -1.0f;
    }
    return (float)g_audio.play_sound(A, s, 1.0f, 0.0f, loop >= 0.5f);
}

void gm_audio_stop_sound(float id)
{
    g_audio.stop_sound(A, id_of(id));
}

void gm_audio_stop_all(void)
{
    g_audio.stop_all_sounds(A);
}

bool gm_audio_is_playing(float id)
{
    return g_audio.is_playing(A, id_of(id));
}

/* audio_sound_gain(index, volume, time): fades are applied at once. */
void gm_audio_sound_gain(float id, float volume, float ms)
{
    (void)ms;
    g_audio.set_sound_volume(A, id_of(id), volume < 0.0f ? 0.0f : volume);
}

void gm_audio_master_gain(float volume)
{
    g_audio.set_master_volume(A, volume < 0.0f ? 0.0f : volume);
}

void gm_audio_pause_all(void)
{
    g_audio.pause_all(A);
}

void gm_audio_resume_all(void)
{
    g_audio.resume_all(A);
}
