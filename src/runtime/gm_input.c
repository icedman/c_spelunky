/*
 * gm_input - see gm_input.h. References are yyIOManager.js unless noted.
 */
#include "gm_input.h"

#include <string.h>

/* Raw state (g_KeyDown / g_KeyPressed / g_KeyUp / g_LastKeyPressed_code). */
static bool s_raw_down[GM_INPUT_MAX_KEYS];
static bool s_raw_pressed[GM_INPUT_MAX_KEYS];
static bool s_raw_up[GM_INPUT_MAX_KEYS];
static int s_raw_last;

/* Per-step state (yyIOManager KeyDown / KeyPressed / KeyReleased / KeyMap). */
static bool s_down[GM_INPUT_MAX_KEYS];
static bool s_pressed[GM_INPUT_MAX_KEYS];
static bool s_released[GM_INPUT_MAX_KEYS];
static int s_map[GM_INPUT_MAX_KEYS];   /* mapped key + 1; 0 = identity (zero-init safe) */

static int s_keyboard_key;
static int s_keyboard_lastkey;

static bool valid(int key)
{
    return key >= 0 && key < GM_INPUT_MAX_KEYS;
}

void gm_input_reset(void)
{
    memset(s_raw_down, 0, sizeof(s_raw_down));
    memset(s_raw_pressed, 0, sizeof(s_raw_pressed));
    memset(s_raw_up, 0, sizeof(s_raw_up));
    memset(s_down, 0, sizeof(s_down));
    memset(s_pressed, 0, sizeof(s_pressed));
    memset(s_released, 0, sizeof(s_released));
    memset(s_map, 0, sizeof(s_map));
    s_raw_last = 0;
    s_keyboard_key = 0;
    s_keyboard_lastkey = 0;
}

/* yyKeyDownCallback L771-777: pressed only if not already down; every call (OS
 * repeats too) records the last key. */
void gm_input_key_down(int key)
{
    if (!valid(key)) {
        return;
    }
    if (!s_raw_down[key]) {
        s_raw_pressed[key] = true;
    }
    s_raw_down[key] = true;
    s_raw_last = key;
}

/* yyKeyUpCallback L829 */
void gm_input_key_up(int key)
{
    if (!valid(key)) {
        return;
    }
    s_raw_up[key] = true;
    s_raw_down[key] = false;
}

void gm_input_start_step(void)
{
    bool pressed = false, down = false, released = false;
    int i;

    /* IO_StartStep L2104-2132 */
    memset(s_down, 0, sizeof(s_down));
    memset(s_pressed, 0, sizeof(s_pressed));
    memset(s_released, 0, sizeof(s_released));
    for (i = 0; i < GM_INPUT_MAX_KEYS; ++i) {
        int key = s_map[i] != 0 ? s_map[i] - 1 : i;
        s_pressed[key] = s_pressed[key] || s_raw_pressed[i];
        s_released[key] = s_released[key] || s_raw_up[i];
        s_down[key] = s_down[key] || s_raw_down[i];
        pressed = pressed || s_raw_pressed[i];
        down = down || s_raw_down[i];
        released = released || s_raw_up[i];
        s_raw_pressed[i] = false;
        s_raw_up[i] = false;
    }
    s_pressed[GM_VK_ANYKEY] = pressed;
    s_down[GM_VK_ANYKEY] = down;
    s_released[GM_VK_ANYKEY] = released;
    s_pressed[GM_VK_NOKEY] = !pressed;
    s_down[GM_VK_NOKEY] = !down;
    s_released[GM_VK_NOKEY] = !released;

    /* IO_Update L1717-1729 (keyboard_lastchar / keyboard_string are not modelled). */
    if (s_raw_last != 0) {
        if (s_keyboard_key != 0) {
            s_keyboard_lastkey = s_keyboard_key;
        }
        s_keyboard_key = s_raw_last;
        s_raw_last = 0;
    } else if (!valid(s_keyboard_key) || !s_down[s_keyboard_key]) {
        s_keyboard_lastkey = s_keyboard_key;
        s_keyboard_key = 0;
    }
}

/* Function_IO.js L233-304 */
bool gm_input_check(int key)
{
    return valid(key) && s_down[key];
}

bool gm_input_check_pressed(int key)
{
    return valid(key) && s_pressed[key];
}

bool gm_input_check_released(int key)
{
    return valid(key) && s_released[key];
}

/* keyboard_clear (Function_IO.js L263) */
void gm_input_clear(int key)
{
    if (valid(key)) {
        s_down[key] = false;
        s_pressed[key] = false;
        s_released[key] = false;
    }
}

/* keyboard_set_map (Function_IO.js L350): the runner accepts 0..MAX_KEYS inclusive,
 * an off-by-one we do not copy. */
void gm_input_set_map(int from, int to)
{
    if (valid(from) && valid(to)) {
        s_map[from] = to + 1;
    }
}

int gm_input_keyboard_key(void)
{
    return s_keyboard_key;
}

int gm_input_keyboard_lastkey(void)
{
    return s_keyboard_lastkey;
}

void gm_input_set_keyboard_key(int key)
{
    s_keyboard_key = key;
}

void gm_input_set_keyboard_lastkey(int key)
{
    s_keyboard_lastkey = key;
}
