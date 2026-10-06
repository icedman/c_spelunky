/*
 * gm_math - GML math built-ins with GameMaker-HTML5 semantics, plus the
 * runner's WELL512 random number generator (bit-exact port).
 *
 * Reference: reference/GameMaker-HTML5/scripts/functions/Function_Maths.js
 * and Globals.js. Parity with the HTML5 runner (our JS oracle) is the goal,
 * so several HTML5 quirks are reproduced on purpose:
 *   - Pi is the truncated constant 3.14159265 (Globals.js L16).
 *   - round() is banker's rounding (DelphiRound, Function_Maths.js L46).
 *   - degtorad/radtodeg use 0.0174532925 / 57.2957795.
 *   - lengthdir_* snap to the nearest integer within 0.0001.
 *   - point_direction is quantized to 1e-6 degrees.
 *
 * Only functions with GML-specific behaviour live here; plain C equivalents
 * (abs -> fabs, floor, ceil, sqrt, sin, cos, arctan -> atan) are mapped
 * directly by the transpiler.
 */
#ifndef GM_MATH_H
#define GM_MATH_H

#include <stdint.h>

/* HTML5 runner's Pi (Globals.js L16), NOT M_PI. */
#define GM_PI 3.14159265f

/* JavaScript ToInt32 (what yyGetInt32 / `~~x` / `x | 0` do): truncate, then
 * wrap modulo 2^32 into [-2^31, 2^31). NaN and infinities map to 0. */
int32_t gm_to_int32(float x);

float gm_round(float x); /* banker's rounding: round(2.5) == 2 */
float gm_frac(float x);  /* x - ToInt32(x) */
float gm_sign(float x);
float gm_min(float a, float b);
float gm_max(float a, float b);
float gm_clamp(float value, float lo, float hi);
float gm_lerp(float a, float b, float amount);
float gm_sqr(float x);
float gm_power(float x, float n);
float gm_degtorad(float deg);
float gm_radtodeg(float rad);

/* sin/cos of an angle in DEGREES. Table-driven when gm_math.c defines
 * ENABLE_LUT (error < 4e-7), else sinf/cosf. */
float gm_dsin(float deg);
float gm_dcos(float deg);

float gm_lengthdir_x(float len, float dir);
float gm_lengthdir_y(float len, float dir);
float gm_point_distance(float x1, float y1, float x2, float y2);
float gm_point_direction(float x1, float y1, float x2, float y2);

/* ---- Random (WELL512, Function_Maths.js L412-636) ------------------------ */

/* Reseeds the generator (InitRandom). The generator starts seeded with 0. */
void gm_random_set_seed(int32_t seed);
int32_t gm_random_get_seed(void);

/* Seeds from the wall clock (C99 time()/clock()); returns the new seed. */
int32_t gm_randomize(void);

/* Raw generator output in [0, 1] (`rand()` in the reference). */
float gm_rand01(void);

/* random(x): [0, x). Consumes one generator step. */
float gm_random(float x);

/* random_range(a, b): [min, max). Consumes two generator steps, or none when
 * a == b (matching the reference exactly). */
float gm_random_range(float a, float b);

#endif /* GM_MATH_H */
