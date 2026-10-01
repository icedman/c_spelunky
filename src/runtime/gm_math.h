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
#define GM_PI 3.14159265

/* JavaScript ToInt32 (what yyGetInt32 / `~~x` / `x | 0` do): truncate, then
 * wrap modulo 2^32 into [-2^31, 2^31). NaN and infinities map to 0. */
int32_t gm_to_int32(double x);

double gm_round(double x); /* banker's rounding: round(2.5) == 2 */
double gm_frac(double x);  /* x - ToInt32(x) */
double gm_sign(double x);
double gm_min(double a, double b);
double gm_max(double a, double b);
double gm_clamp(double value, double lo, double hi);
double gm_lerp(double a, double b, double amount);
double gm_sqr(double x);
double gm_power(double x, double n);
double gm_degtorad(double deg);
double gm_radtodeg(double rad);

double gm_lengthdir_x(double len, double dir);
double gm_lengthdir_y(double len, double dir);
double gm_point_distance(double x1, double y1, double x2, double y2);
double gm_point_direction(double x1, double y1, double x2, double y2);

/* ---- Random (WELL512, Function_Maths.js L412-636) ------------------------ */

/* Reseeds the generator (InitRandom). The generator starts seeded with 0. */
void gm_random_set_seed(int32_t seed);
int32_t gm_random_get_seed(void);

/* Seeds from the wall clock (C99 time()/clock()); returns the new seed. */
int32_t gm_randomize(void);

/* Raw generator output in [0, 1] (`rand()` in the reference). */
double gm_rand01(void);

/* random(x): [0, x). Consumes one generator step. */
double gm_random(double x);

/* random_range(a, b): [min, max). Consumes two generator steps, or none when
 * a == b (matching the reference exactly). */
double gm_random_range(double a, double b);

#endif /* GM_MATH_H */
