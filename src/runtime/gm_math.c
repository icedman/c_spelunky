/*
 * gm_math - see gm_math.h. Line references are to
 * reference/GameMaker-HTML5/scripts/functions/Function_Maths.js.
 */
#include "gm_math.h"

#include <math.h>
#include <stdbool.h>
#include <time.h>

int32_t gm_to_int32(double x)
{
    const double two32 = 4294967296.0;
    double m;
    uint32_t u;

    if (isnan(x) || isinf(x)) {
        return 0;
    }
    m = fmod(trunc(x), two32);
    if (m < 0.0) {
        m += two32;
    }
    u = (uint32_t)m;
    /* Reinterpret as two's complement without implementation-defined casts. */
    if (u <= 0x7fffffffu) {
        return (int32_t)u;
    }
    return -(int32_t)(~u) - 1;
}

/* JavaScript Math.round: nearest integer, ties toward +infinity. */
static double js_math_round(double x)
{
    double r = floor(x);
    if (x - r >= 0.5) {
        r += 1.0;
    }
    return r;
}

/* DelphiRound (L46-84): round half to even. */
double gm_round(double x)
{
    double i;
    double f;
    bool odd;

    if (isnan(x)) {
        return x;
    }
    i = floor(fabs(x));
    odd = fmod(i, 2.0) == 1.0;
    if (x < 0.0) {
        i = -i;
        f = x - i;
        if (odd) {
            return f <= -0.5 ? i - 1.0 : i;
        }
        return f >= -0.5 ? i : i - 1.0;
    }
    f = x - i;
    if (odd) {
        return f >= 0.5 ? i + 1.0 : i;
    }
    return f <= 0.5 ? i : i + 1.0;
}

/* L702: _x - ~~_x (note: ToInt32, so wraps for |x| >= 2^31). */
double gm_frac(double x)
{
    return x - (double)gm_to_int32(x);
}

double gm_sign(double x)
{
    if (x == 0.0) {
        return 0.0;
    }
    return x < 0.0 ? -1.0 : 1.0;
}

/* min()/max() (L369-408): m = a; if (m > b) m = b. Nesting two-argument
 * calls reproduces the reference's variadic loop exactly. */
double gm_min(double a, double b)
{
    return a > b ? b : a;
}

double gm_max(double a, double b)
{
    return a < b ? b : a;
}

double gm_clamp(double value, double lo, double hi)
{
    if (value < lo) {
        value = lo;
    }
    if (value > hi) {
        value = hi;
    }
    return value;
}

double gm_lerp(double a, double b, double amount)
{
    return a + ((b - a) * amount);
}

double gm_sqr(double x)
{
    return x * x;
}

double gm_power(double x, double n)
{
    return pow(x, n);
}

double gm_degtorad(double deg)
{
    return deg * 0.0174532925;
}

double gm_radtodeg(double rad)
{
    return rad * 57.2957795;
}

/* L213-242 */
double gm_lengthdir_x(double len, double dir)
{
    double v = len * cos(dir * GM_PI / 180.0);
    double flv = js_math_round(v);
    return fabs(v - flv) < 0.0001 ? flv : v;
}

double gm_lengthdir_y(double len, double dir)
{
    double v = -(len * sin(dir * GM_PI / 180.0));
    double flv = js_math_round(v);
    return fabs(v - flv) < 0.0001 ? flv : v;
}

/* L300 */
double gm_point_distance(double x1, double y1, double x2, double y2)
{
    double dx = x2 - x1;
    double dy = y2 - y1;
    return sqrt(dx * dx + dy * dy);
}

/* L258-284 */
double gm_point_direction(double x1, double y1, double x2, double y2)
{
    double x = x2 - x1;
    double y = y2 - y1;
    double dd;

    if (x == 0.0) {
        if (y > 0.0) {
            return 270.0;
        }
        if (y < 0.0) {
            return 90.0;
        }
        return 0.0;
    }
    dd = 180.0 * atan2(y, x) / GM_PI;
    dd = (double)gm_to_int32(gm_round(dd * 1000000.0)) / 1000000.0;
    return dd <= 0.0 ? -dd : 360.0 - dd;
}

/* ---- WELL512 (L412-478) ---------------------------------------------------
 * JS operates on int32 bit patterns; uint32_t reproduces them exactly as long
 * as `>>` (arithmetic in JS) sign-extends, which sar() does. */

#define GM_RANDOM_POLY 0xDA442D24u

static uint32_t s_state[16];
static unsigned s_index;
static int32_t s_seed;
static bool s_seeded;

static uint32_t sar(uint32_t v, unsigned n)
{
    uint32_t r = v >> n;
    if ((v & 0x80000000u) != 0u) {
        r |= ~(0xFFFFFFFFu >> n);
    }
    return r;
}

/* InitRandom (L427-437):
 *   s = (((s * 214013 + 2531011) >> 16) & 0x7fffffff) | 0;
 * The multiply is a double in JS but stays below 2^53, so int64 is exact. */
void gm_random_set_seed(int32_t seed)
{
    int64_t s = seed;
    int i;

    for (i = 0; i < 16; ++i) {
        uint32_t u = (uint32_t)(s * 214013 + 2531011);
        uint32_t v = sar(u, 16) & 0x7fffffffu;
        s_state[i] = v;
        s = (int64_t)v;
    }
    s_index = 0;
    s_seed = seed;
    s_seeded = true;
}

int32_t gm_random_get_seed(void)
{
    if (!s_seeded) {
        gm_random_set_seed(0);
    }
    return s_seed;
}

/* randomize (L580-586) mixes a clock value the same way; the clock source
 * differs (the browser uses Date.getMilliseconds()). */
int32_t gm_randomize(void)
{
    uint32_t t = (uint32_t)time(NULL) ^ (uint32_t)clock();
    uint32_t mixed = t ^ ((t >> 16) & 0xffffu) ^ ((t << 16) & 0xffff0000u);
    int32_t seed = gm_to_int32((double)mixed);

    gm_random_set_seed(seed);
    return seed;
}

double gm_rand01(void)
{
    uint32_t a, b, c, d;

    if (!s_seeded) {
        gm_random_set_seed(0);
    }
    a = s_state[s_index];
    c = s_state[(s_index + 13u) & 15u];
    b = a ^ c ^ (a << 16) ^ (c << 15);
    c = s_state[(s_index + 9u) & 15u];
    c ^= sar(c, 11);
    a = s_state[s_index] = b ^ c;
    d = a ^ ((a << 5) & GM_RANDOM_POLY);
    s_index = (s_index + 15u) & 15u;
    a = s_state[s_index];
    s_state[s_index] = a ^ b ^ d ^ (a << 2) ^ (b << 18) ^ (c << 28);
    return (double)(s_state[s_index] & 0x7fffffffu) / 2147483647.0;
}

/* L491 */
double gm_random(double x)
{
    return gm_rand01() * x;
}

/* L529-553 */
double gm_random_range(double a, double b)
{
    double lower, higher, result;

    if (a == b) {
        return a;
    }
    if (a > b) {
        lower = b;
        higher = a;
    } else {
        lower = a;
        higher = b;
    }
    result = lower + (gm_rand01() * (higher - lower));
    (void)gm_rand01();
    return result;
}
