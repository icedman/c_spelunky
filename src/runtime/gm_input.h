/*
 * gm_input - keyboard state as the GameMaker-HTML5 runner keeps it.
 *
 * Two layers, as in yyIOManager.js:
 *  - raw state written by the host between frames (g_KeyDown / g_KeyPressed /
 *    g_KeyUp, set by yyKeyDownCallback / yyKeyUpCallback L760-830, or by
 *    keyboard_key_press / _release, Function_IO.js L29-59);
 *  - the per-step state the game reads, rebuilt once at the start of every step
 *    (IO_StartStep L2095): raw keys are remapped through keyboard_set_map, key 1
 *    is "any key" and key 0 "no key"; raw pressed/released flags are consumed.
 *    IO_Update (L1693) then maintains keyboard_key / keyboard_lastkey.
 *
 * keyboard_check / _pressed / _released read the per-step state, so a key that
 * goes down and up between two steps still reads as pressed and released in the
 * next step. Mouse, gamepad and keyboard_string are not modelled (Spelunky's
 * mouse events are the mobile touch buttons; gamepads are pending built-ins).
 */
#ifndef GM_INPUT_H
#define GM_INPUT_H

#include <stdbool.h>

#define GM_INPUT_MAX_KEYS 256   /* MAX_KEYS (yyIOManager.js) */
#define GM_VK_NOKEY 0
#define GM_VK_ANYKEY 1

/* Clears all state and the key map (identity). */
void gm_input_reset(void);

/* ---- host side (raw state; keys outside 0..255 are ignored) ---- */

/* A key went down. Repeats while held are ignored (no second "pressed"). */
void gm_input_key_down(int key);
void gm_input_key_up(int key);

/* ---- step side ---- */

/* IO_StartStep + IO_Update: builds the step's state from the raw state. */
void gm_input_start_step(void);

bool gm_input_check(int key);           /* keyboard_check / keyboard_check_direct */
bool gm_input_check_pressed(int key);
bool gm_input_check_released(int key);
void gm_input_clear(int key);           /* keyboard_clear */
void gm_input_set_map(int from, int to);/* keyboard_set_map */

int gm_input_keyboard_key(void);
int gm_input_keyboard_lastkey(void);
void gm_input_set_keyboard_key(int key);
void gm_input_set_keyboard_lastkey(int key);

#endif /* GM_INPUT_H */
