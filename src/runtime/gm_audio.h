/*
 * gm_audio - GameMaker audio built-ins over sp_audio (Function_Audio.js subset).
 *
 * Sound ids are SND_* asset indices; audio_play_sound returns a playing-instance
 * handle from the platform (>= GM_AUDIO_HANDLE_BASE, so stop / is_playing / gain
 * can tell handles from sound ids, as the runner's instance ids are). Priority is
 * ignored (the platform mixes every channel it has).
 */
#ifndef GM_AUDIO_H
#define GM_AUDIO_H

#include <stdbool.h>

#define GM_AUDIO_HANDLE_BASE 100000

float gm_audio_play_sound(float sound, float priority, float loop);
void gm_audio_stop_sound(float id);
void gm_audio_stop_all(void);
bool gm_audio_is_playing(float id);
void gm_audio_sound_gain(float id, float volume, float ms);
void gm_audio_master_gain(float volume);
void gm_audio_pause_all(void);
void gm_audio_resume_all(void);

#endif /* GM_AUDIO_H */
