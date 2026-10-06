/*
 * gm_math - see gm_math.h. Line references are to
 * reference/GameMaker-HTML5/scripts/functions/Function_Maths.js.
 */
#include "gm_math.h"

#include <math.h>
#include <stdbool.h>
#include <time.h>

/* gm_sin/gm_cos use a lookup table. Comment out to use sinf/cosf. */
#define ENABLE_LUT

#ifdef ENABLE_LUT
/* 0.1 degree steps: integer (and tenth) degrees hit table entries exactly, and
 * linear interpolation between entries is off by < 4e-7. One extra entry so
 * interpolation never wraps. 14.4 KB. */
#define LUT_STEPS_PER_DEG 10
#define LUT_SIZE (360 * LUT_STEPS_PER_DEG)

static float s_sin_lut[LUT_SIZE + 1];
static bool s_lut_ready;

static void lut_init(void)
{
    int i;

    for (i = 0; i <= LUT_SIZE; ++i) {
        /* Built in float once so every entry is correctly rounded. */
        s_sin_lut[i] = (float)sinf((float)i * (GM_PI / 180.0f) / LUT_STEPS_PER_DEG);
    }
    s_lut_ready = true;
}

/* sin of an angle in degrees. */
static float lut_sin_deg(float deg)
{
    float t;
    int i;

    if (!isfinite(deg)) {
        return sinf(deg); /* NaN, as sinf would give */
    }
    if (!s_lut_ready) {
        lut_init();
    }
    t = fmodf(deg * (float)LUT_STEPS_PER_DEG, (float)LUT_SIZE);
    if (t < 0.0f) {
        t += (float)LUT_SIZE;
    }
    i = (int)t;
    if (i >= LUT_SIZE) { /* t rounded up to LUT_SIZE */
        i = 0;
        t = 0.0f;
    }
    return s_sin_lut[i] + (s_sin_lut[i + 1] - s_sin_lut[i]) * (t - (float)i);
}
#endif

float gm_dsin(float deg)
{
#ifdef ENABLE_LUT
    return lut_sin_deg(deg);
#else
    return sinf(deg * (float)GM_PI / 180.0f);
#endif
}

float gm_dcos(float deg)
{
#ifdef ENABLE_LUT
    return lut_sin_deg(deg + 90.0f);
#else
    return cosf(deg * (float)GM_PI / 180.0f);
#endif
}

int32_t gm_to_int32(float x)
{
    if (isnan(x) || isinf(x)) {
        return 0;
    }
    return (int32_t)(int64_t)x;
}

/* JavaScript Math.round: nearest integer, ties toward +infinity. */
static float js_math_round(float x)
{
    float r = floorf(x);
    if (x - r >= 0.5f) {
        r += 1.0f;
    }
    return r;
}

/* DelphiRound (L46-84): round half to even. */
float gm_round(float x)
{
    float i;
    float f;
    bool odd;

    if (isnan(x)) {
        return x;
    }
    i = floorf(fabsf(x));
    odd = fmodf(i, 2.0f) == 1.0f;
    if (x < 0.0f) {
        i = -i;
        f = x - i;
        if (odd) {
            return f <= -0.5f ? i - 1.0f : i;
        }
        return f >= -0.5f ? i : i - 1.0f;
    }
    f = x - i;
    if (odd) {
        return f >= 0.5f ? i + 1.0f : i;
    }
    return f <= 0.5f ? i : i + 1.0f;
}

/* L702: _x - ~~_x (note: ToInt32, so wraps for |x| >= 2^31). */
float gm_frac(float x)
{
    return x - (float)gm_to_int32(x);
}

float gm_sign(float x)
{
    if (x == 0.0f) {
        return 0.0f;
    }
    return x < 0.0f ? -1.0f : 1.0f;
}

/* min()/max() (L369-408): m = a; if (m > b) m = b. Nesting two-argument
 * calls reproduces the reference's variadic loop exactly. */
float gm_min(float a, float b)
{
    return a > b ? b : a;
}

float gm_max(float a, float b)
{
    return a < b ? b : a;
}

float gm_clamp(float value, float lo, float hi)
{
    if (value < lo) {
        value = lo;
    }
    if (value > hi) {
        value = hi;
    }
    return value;
}

float gm_lerp(float a, float b, float amount)
{
    return a + ((b - a) * amount);
}

float gm_sqr(float x)
{
    return x * x;
}

float gm_power(float x, float n)
{
    return powf(x, n);
}

float gm_degtorad(float deg)
{
    return deg * 0.0174532925f;
}

float gm_radtodeg(float rad)
{
    return rad * 57.2957795f;
}

/* L213-242 */
float gm_lengthdir_x(float len, float dir)
{
    float v = len * gm_dcos(dir);
    float flv = js_math_round(v);
    return fabsf(v - flv) < 0.0001f ? flv : v;
}

float gm_lengthdir_y(float len, float dir)
{
    float v = -(len * gm_dsin(dir));
    float flv = js_math_round(v);
    return fabsf(v - flv) < 0.0001f ? flv : v;
}

/* L300 */
float gm_point_distance(float x1, float y1, float x2, float y2)
{
    float dx = x2 - x1;
    float dy = y2 - y1;
    return sqrtf(dx * dx + dy * dy);
}

/* L258-284 */
float gm_point_direction(float x1, float y1, float x2, float y2)
{
    float x = x2 - x1;
    float y = y2 - y1;
    float dd;

    if (x == 0.0f) {
        if (y > 0.0f) {
            return 270.0f;
        }
        if (y < 0.0f) {
            return 90.0f;
        }
        return 0.0f;
    }
    dd = 180.0f * atan2f(y, x) / (float)GM_PI;
    dd = (float)gm_to_int32(gm_round(dd * 1000000.0f)) / 1000000.0f;
    return dd <= 0.0f ? -dd : 360.0f - dd;
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
 * The multiply is a float in JS but stays below 2^53, so int64 is exact. */
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
    int32_t seed = gm_to_int32((float)mixed);

    gm_random_set_seed(seed);
    return seed;
}

float gm_rand01(void)
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
    return (float)(s_state[s_index] & 0x7fffffffu) / 2147483647.0f;
}

/* L491 */
float gm_random(float x)
{
    return gm_rand01() * x;
}

/* L529-553 */
float gm_random_range(float a, float b)
{
    float lower, higher, result;

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
