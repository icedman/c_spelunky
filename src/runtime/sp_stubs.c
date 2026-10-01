/*
 * Instantiates the single-header platform and audio stubs. Hosts replace the
 * active tables (g_platform / g_audio) at startup; headless runs and tests use
 * these defaults.
 */
#define SP_PLATFORM_IMPLEMENTATION
#include "sp_platform.h"

#define SP_AUDIO_IMPLEMENTATION
#include "sp_audio.h"
