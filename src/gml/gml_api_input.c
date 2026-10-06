/*
 * gml_api_input - bindings for the keyboard, game control and event_perform
 * built-ins (slice 9). Prototypes come from the generated gml_api.h; see gml_api.c
 * for the conventions.
 *
 * References: Function_IO.js (keyboard_*; key codes go through yyGetInt32),
 * Function_Room.js L875-910 (game_end / game_restart), Function_Game.js L339
 * (event_perform), yyIOManager.js IO_Update (keyboard_key / keyboard_lastkey).
 */
#include "gml_rt.h"

static int i32(float d)
{
    return (int)gm_to_int32(d);
}

static float b2r(bool b)
{
    return b ? 1.0f : 0.0f;
}

/* ------------------------------------------------------------------ keyboard */

float gml_fn_keyboard_check(gm_instance_t *self, gm_instance_t *other, float key)
{
    (void)self;
    (void)other;
    return b2r(gm_input_check(i32(key)));
}

float gml_fn_keyboard_check_pressed(gm_instance_t *self, gm_instance_t *other, float key)
{
    (void)self;
    (void)other;
    return b2r(gm_input_check_pressed(i32(key)));
}

float gml_fn_keyboard_check_released(gm_instance_t *self, gm_instance_t *other, float key)
{
    (void)self;
    (void)other;
    return b2r(gm_input_check_released(i32(key)));
}

void gml_fn_keyboard_set_map(gm_instance_t *self, gm_instance_t *other, float from, float to)
{
    (void)self;
    (void)other;
    gm_input_set_map(i32(from), i32(to));
}

float gml_gget_keyboard_key(void)
{
    return (float)gm_input_keyboard_key();
}

void gml_gset_keyboard_key(float v)
{
    gm_input_set_keyboard_key(i32(v));
}

float gml_gget_keyboard_lastkey(void)
{
    return (float)gm_input_keyboard_lastkey();
}

void gml_gset_keyboard_lastkey(float v)
{
    gm_input_set_keyboard_lastkey(i32(v));
}

/* ------------------------------------------------------------------ game */

void gml_fn_game_end(gm_instance_t *self, gm_instance_t *other)
{
    (void)self;
    (void)other;
    gm_room_game_end();
}

/* The runner also zeroes score / lives / health, which Spelunky does not read. */
void gml_fn_game_restart(gm_instance_t *self, gm_instance_t *other)
{
    (void)self;
    (void)other;
    gm_room_game_restart();
}

/* ------------------------------------------------------------------ events */

void gml_fn_event_perform(gm_instance_t *self, gm_instance_t *other, float type, float number)
{
    gm_loop_event_perform(self, other, i32(type), i32(number));
}
