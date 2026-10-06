/*
 * gm_collision - see gm_collision.h for semantics and references.
 *
 * Ports keep the reference's operation order so that floating-point results
 * match the JS oracle; comments cite the reference lines.
 */
#include "gm_collision.h"

#include "gm_math.h"
#include "gm_object.h"
#include "gm_perf.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

/* Math.PI: some precise routines use it instead of the runner's truncated
 * Pi (GM_PI). Each port below uses whichever constant its reference uses. */
#define JS_MATH_PI 3.141592653589793f

/* g_GMLMathEpsilon default (Function_Maths.js L19). */
#define GML_MATH_EPSILON 1e-5f

/* Collision_Point / Collision_Line col_delta (compatibility mode off). */
#define COL_DELTA (-0.00001f)

/* Loops below step floats by 1.0; beyond 2^52 that would never terminate. */
#define LOOP_LIMIT 4503599627370496.0f

typedef struct bbox {
    float left, top, right, bottom;
} bbox_t;

/* yymin / yymax (Function_Maths.js L336): not gm_min/gm_max, whose NaN and
 * signed-zero behaviour differs. */
static float yymin(float a, float b)
{
    return a < b ? a : b;
}

static float yymax(float a, float b)
{
    return a > b ? a : b;
}

static bool loop_range_ok(float lo, float hi)
{
    return lo > -LOOP_LIMIT && hi < LOOP_LIMIT;
}

/* ---- Per-slot state ---------------------------------------------------------- */

typedef struct coll_state {
    /* bbox cache key: the inputs the cached bbox was computed from */
    bool valid;
    int id;
    uint32_t sprite_gen;
    int sprite_index, mask_index;
    float x, y, xscale, yscale, angle;
    int colcheck;

    /* grid membership */
    bool in_grid;
    bool cells_stale; /* bbox recomputed since the cells below were derived */
    bool big; /* in the oversize list instead of cells */
    int ncells;
    int cx0, cy0, cx1, cy1;
} coll_state_t;

static coll_state_t s_cs[GM_INSTANCE_MAX];

/* ---- Grid storage ------------------------------------------------------------ */

#define GRID_MAX_CELLS (GM_GRID_MAX_COLS * GM_GRID_MAX_ROWS)
#define GRID_MAX_ENTRIES (GM_INSTANCE_MAX * GM_GRID_CELLS_PER_INSTANCE)
#define NIL (-1)

typedef char gm_grid_fits_int16[(GRID_MAX_CELLS <= 32767 && GRID_MAX_ENTRIES <= 32767 &&
                                 GM_INSTANCE_MAX <= 32767) ? 1 : -1];

static int16_t s_cell_head[GRID_MAX_CELLS];
/* Entry e belongs to slot e / GM_GRID_CELLS_PER_INSTANCE. */
static int16_t s_entry_next[GRID_MAX_ENTRIES];
static int16_t s_entry_prev[GRID_MAX_ENTRIES];
static int16_t s_entry_cell[GRID_MAX_ENTRIES];

static int16_t s_big_head;
static int16_t s_big_next[GM_INSTANCE_MAX];
static int16_t s_big_prev[GM_INSTANCE_MAX];

static int s_cols, s_rows; /* 0 = grid not configured */
static float s_cell_size;
static int s_grid_count;

static int s_roots[GM_GRID_MAX_ROOTS];
static int s_root_count;
static bool s_indexed[GM_OBJECT_MAX];

/* Dedupe stamps for candidate collection. */
static uint32_t s_stamp[GM_INSTANCE_MAX];
static uint32_t s_stamp_counter;

static bool s_verify;
static long s_verify_failures;

/* ---- Bounding boxes (Compute_BoundingBox, yyInstance.js L1455) ---------------- */

static const gm_sprite_def_t *collision_sprite(const gm_instance_t *inst)
{
    return gm_sprite_get(inst->mask_index >= 0 ? inst->mask_index : inst->sprite_index);
}

static void compute_bbox(gm_instance_t *inst, coll_state_t *cs)
{
    const gm_sprite_def_t *spr = collision_sprite(inst);
    float l, t, r, b, tmp;

    if (spr == NULL) {
        /* No sprite: a point box, never collides (L1507-1518). */
        l = r = inst->x;
        t = b = inst->y;
        cs->colcheck = GM_COLCHECK_AABB;
    } else if (inst->image_angle == 0.0f) {
        /* L1539-1577 (non-compatibility branches) */
        float width = (float)((spr->bbox_right + 1) - spr->bbox_left);
        float height = (float)((spr->bbox_bottom + 1) - spr->bbox_top);

        l = inst->x + inst->image_xscale * (float)(spr->bbox_left - spr->xorigin);
        r = l + inst->image_xscale * width;
        if (l > r) {
            tmp = l;
            l = r;
            r = tmp;
        }
        t = inst->y + inst->image_yscale * (float)(spr->bbox_top - spr->yorigin);
        b = t + inst->image_yscale * height;
        if (t > b) {
            tmp = t;
            t = b;
            b = tmp;
        }
        cs->colcheck = spr->colcheck;
    } else {
        /* Rotated: axis-aligned box around the rotated rectangle (L1609-1672). */
        float xmin = inst->image_xscale * (float)(spr->bbox_left - spr->xorigin);
        float xmax = inst->image_xscale * (float)(spr->bbox_right - spr->xorigin + 1);
        float ymin = inst->image_yscale * (float)(spr->bbox_top - spr->yorigin);
        float ymax = inst->image_yscale * (float)(spr->bbox_bottom - spr->yorigin + 1);
        float cc = gm_dcos(inst->image_angle);
        float ss = gm_dsin(inst->image_angle);
        float cc_xmax = cc * xmax, cc_xmin = cc * xmin;
        float ss_ymax = ss * ymax, ss_ymin = ss * ymin;
        float cc_ymax = cc * ymax, cc_ymin = cc * ymin;
        float ss_xmax = ss * xmax, ss_xmin = ss * xmin;

        if (cc_xmax < cc_xmin) {
            tmp = cc_xmin;
            cc_xmin = cc_xmax;
            cc_xmax = tmp;
        }
        if (ss_ymax < ss_ymin) {
            tmp = ss_ymin;
            ss_ymin = ss_ymax;
            ss_ymax = tmp;
        }
        l = inst->x + cc_xmin + ss_ymin;
        r = inst->x + cc_xmax + ss_ymax;

        if (cc_ymax < cc_ymin) {
            tmp = cc_ymin;
            cc_ymin = cc_ymax;
            cc_ymax = tmp;
        }
        if (ss_xmax < ss_xmin) {
            tmp = ss_xmin;
            ss_xmin = ss_xmax;
            ss_xmax = tmp;
        }
        t = inst->y + cc_ymin - ss_xmax;
        b = inst->y + cc_ymax - ss_xmin;
        cs->colcheck = spr->colcheck;
    }

    inst->bbox_left = l;
    inst->bbox_top = t;
    inst->bbox_right = r;
    inst->bbox_bottom = b;

    cs->valid = true;
    cs->cells_stale = true;
    cs->id = inst->id;
    cs->sprite_gen = gm_sprite_generation();
    cs->sprite_index = inst->sprite_index;
    cs->mask_index = inst->mask_index;
    cs->x = inst->x;
    cs->y = inst->y;
    cs->xscale = inst->image_xscale;
    cs->yscale = inst->image_yscale;
    cs->angle = inst->image_angle;
}

static bool bbox_current(const gm_instance_t *inst, const coll_state_t *cs)
{
    return cs->valid && cs->id == inst->id && cs->sprite_gen == gm_sprite_generation() &&
           cs->sprite_index == inst->sprite_index && cs->mask_index == inst->mask_index &&
           cs->x == inst->x && cs->y == inst->y && cs->xscale == inst->image_xscale &&
           cs->yscale == inst->image_yscale && cs->angle == inst->image_angle;
}

static coll_state_t *ensure_bbox(gm_instance_t *inst, bbox_t *out)
{
    coll_state_t *cs = &s_cs[gm_instance_slot(inst)];

    if (!bbox_current(inst, cs)) {
        compute_bbox(inst, cs);
    }
    if (out != NULL) {
        out->left = inst->bbox_left;
        out->top = inst->bbox_top;
        out->right = inst->bbox_right;
        out->bottom = inst->bbox_bottom;
    }
    return cs;
}

int gm_collision_update_bbox(gm_instance_t *inst)
{
    return ensure_bbox(inst, NULL)->colcheck;
}

/* ---- Masks (ColMaskSet, yySprite.js L1626) ------------------------------------ */

/* A NULL mask stands for the runner's missing colmask entry: always set. */
static bool colmask_set(const gm_sprite_def_t *spr, float u, float v, const uint8_t *mask)
{
    int32_t ui, vi;

    if (mask == NULL) {
        return true;
    }
    gm_perf_count(GM_PERF_PRECISE_MASK_SAMPLES, 1);
    if (u < spr->bbox_left || u > spr->bbox_right) {
        return false;
    }
    if (v < spr->bbox_top || v > spr->bbox_bottom) {
        return false;
    }
    if (v != v) {
        return false; /* NaN row: the JS array index is NaN -> undefined -> 0 */
    }
    u -= spr->bbox_left;
    v -= spr->bbox_top;
    ui = gm_to_int32(u); /* `u >> 3` and `u & 7` apply ToInt32 */
    vi = gm_to_int32(v); /* integral at every call site */
    return (mask[vi * gm_sprite_mask_stride(spr) + (ui >> 3)] & (1u << (7 - (ui & 7)))) != 0;
}

/* Math.floor(img) % colmask.length, made non-negative. -1 for NaN/Inf
 * (colmask[NaN] is undefined, which gm_sprite_mask maps to NULL). */
static int wrap_frame_floor(float img, int len)
{
    float r = fmodf(floorf(img), (float)len);

    if (r < 0.0f) {
        r += (float)len;
    }
    return (r >= 0.0f && r < (float)len) ? (int)r : -1;
}

/* (img | 0) % colmask.length, made non-negative (len > 0). */
static int wrap_frame_int(int32_t img, int len)
{
    int32_t r = img % len;
    return r < 0 ? r + len : r;
}

/* ---- Precise tests (yySprite.js) ------------------------------------------------ */

/* PreciseCollisionPoint (L2126). img is the unfloored image_index. */
static bool precise_point(const gm_sprite_def_t *spr, float img, float x1, float y1,
                          float scalex, float scaley, float angle, float x, float y)
{
    float xx, yy;
    int n;

    if (spr->mask_count == 0) {
        return true;
    }
    if (spr->frame_count <= 0) {
        return false;
    }
    n = wrap_frame_floor(img, spr->mask_count);

    x1 -= 0.5f;
    y1 -= 0.5f;
    if (fabsf(angle) < 0.0001f) {
        xx = floorf((x - x1) / scalex + spr->xorigin);
        yy = floorf((y - y1) / scaley + spr->yorigin);
    } else {
        float ss = gm_dsin(-angle);
        float cc = gm_dcos(-angle);
        xx = floorf((cc * (x - x1) + ss * (y - y1)) / scalex + spr->xorigin);
        yy = floorf((cc * (y - y1) - ss * (x - x1)) / scaley + spr->yorigin);
    }
    return colmask_set(spr, xx, yy, gm_sprite_mask(spr, n));
}

/* PreciseCollisionRectangle (L2171). img is floored by the callee. */
static bool precise_rectangle(const gm_sprite_def_t *spr, float img, const bbox_t *bb1,
                              float x1, float y1, float scalex, float scaley,
                              float angle, const bbox_t *rr)
{
    const uint8_t *mask;
    float l, r, t, b, i, j;

    if (spr->mask_count == 0) {
        return true;
    }
    if (spr->frame_count <= 0) {
        return false;
    }
    mask = gm_sprite_mask(spr, wrap_frame_floor(img, spr->mask_count));

    l = yymax(bb1->left, rr->left);
    r = yymin(bb1->right, rr->right);
    t = yymax(bb1->top, rr->top);
    b = yymin(bb1->bottom, rr->bottom);
    if (!loop_range_ok(l, r) || !loop_range_ok(t, b)) {
        return false;
    }

    x1 -= 0.5f;
    y1 -= 0.5f;
    if (scalex == 1.0f && scaley == 1.0f && fabsf(angle) < 0.0001f) {
        for (i = l; i <= r; i++) {
            for (j = t; j <= b; j++) {
                int32_t xx = gm_to_int32(i - x1 + spr->xorigin);
                int32_t yy = gm_to_int32(j - y1 + spr->yorigin);
                if (xx < 0 || xx >= spr->width) {
                    continue;
                }
                if (yy < 0 || yy >= spr->height) {
                    continue;
                }
                if (colmask_set(spr, (float)xx, (float)yy, mask)) {
                    return true;
                }
            }
        }
    } else {
        float ss = gm_dsin(-angle);
        float cc = gm_dcos(-angle);
        float onescalex = 1.0f / scalex;
        float onescaley = 1.0f / scaley;
        for (i = l; i <= r; i++) {
            for (j = t; j <= b; j++) {
                float xx = floorf((cc * (i - x1) + ss * (j - y1)) * onescalex + spr->xorigin);
                float yy = floorf((cc * (j - y1) - ss * (i - x1)) * onescaley + spr->yorigin);
                if (xx < 0 || xx >= spr->width) {
                    continue;
                }
                if (yy < 0 || yy >= spr->height) {
                    continue;
                }
                if (colmask_set(spr, xx, yy, mask)) {
                    return true;
                }
            }
        }
    }
    return false;
}

/* PreciseCollisionLine (L2709). img is image_index | 0. */
static bool precise_line(const gm_sprite_def_t *spr, int32_t img, const bbox_t *bb1,
                         float x1, float y1, float scalex, float scaley, float angle,
                         float xl, float yl, float xr, float yr)
{
    const uint8_t *mask;
    float ss, cc, dd, i, val, lo, hi;
    int n;

    if (spr->mask_count == 0) {
        return true;
    }
    if (spr->frame_count <= 0) {
        return false;
    }
    n = wrap_frame_int(img, spr->mask_count);

    /* Vertical, horizontal or a single pixel: rectangle test with the
     * unshifted origin (the reference passes left = xl, right = xr as is). */
    if (xl == xr || yl == yr) {
        bbox_t rc;
        rc.left = xl;
        rc.top = yymin(yl, yr);
        rc.right = xr;
        rc.bottom = yymax(yl, yr);
        return precise_rectangle(spr, (float)n, bb1, x1, y1, scalex, scaley, angle, &rc);
    }
    mask = gm_sprite_mask(spr, n);

    ss = gm_dsin(-angle);
    cc = gm_dcos(-angle);
    x1 -= 0.5f;
    y1 -= 0.5f;

    if (fabsf(xr - xl) >= fabsf(yr - yl)) {
        if (xr < xl) {
            val = xr;
            xr = xl;
            xl = val;
            val = yr;
            yr = yl;
            yl = val;
        }
        dd = (yr - yl) / (xr - xl);
        lo = yymax(bb1->left, xl);
        hi = yymin(bb1->right, xr);
        if (!loop_range_ok(lo, hi)) {
            return false;
        }
        for (i = lo; i <= hi; i++) {
            float xx = floorf((cc * (i - x1) + ss * (yl + (i - xl) * dd - y1)) / scalex + spr->xorigin);
            float yy = floorf((cc * (yl + (i - xl) * dd - y1) - ss * (i - x1)) / scaley + spr->yorigin);
            if (colmask_set(spr, xx, yy, mask)) {
                return true;
            }
        }
    } else {
        if (yr < yl) {
            val = yr;
            yr = yl;
            yl = val;
            val = xr;
            xr = xl;
            xl = val;
        }
        dd = (xr - xl) / (yr - yl);
        lo = yymax(bb1->top, yl);
        hi = yymin(bb1->bottom, yr);
        if (!loop_range_ok(lo, hi)) {
            return false;
        }
        for (i = lo; i <= hi; i++) {
            float xx = floorf((cc * (xl + (i - yl) * dd - x1) + ss * (i - y1)) / scalex + spr->xorigin);
            float yy = floorf((cc * (i - y1) - ss * (xl + (i - yl) * dd - x1)) / scaley + spr->yorigin);
            if (colmask_set(spr, xx, yy, mask)) {
                return true;
            }
        }
    }
    return false;
}

/* Precise instance/instance test (yySprite.PreciseCollision, L2364). */
typedef struct precise_side {
    const gm_sprite_def_t *spr;
    int32_t img;
    const bbox_t *bb;
    float x, y, scalex, scaley, angle;
} precise_side_t;

static bool precise_collision(const precise_side_t *a, const precise_side_t *o)
{
    const gm_sprite_def_t *s1 = a->spr;
    const gm_sprite_def_t *s2 = o->spr;
    const uint8_t *mask1 = NULL, *mask2 = NULL;
    int32_t img1 = a->img, img2 = o->img;
    float sc1x, sc1y, sc2x, sc2y, l, r, t, b, i, j;
    float leftedge, rightedge, topedge, bottomedge;
    float sleftedge, srightedge, stopedge, sbottomedge;
    bool hasrot1, hasrot2;
    bool prec1 = s1->colcheck == GM_COLCHECK_PRECISE;
    bool prec2 = s2->colcheck == GM_COLCHECK_PRECISE;

    if (s1->frame_count <= 0 || s2->frame_count <= 0) {
        return false;
    }
    if (s1->mask_count > 0) {
        img1 = img1 % s1->mask_count;
        if (img1 < 0) {
            img1 += s1->mask_count;
        }
        mask1 = gm_sprite_mask(s1, img1);
    }
    if (s2->mask_count > 0) {
        img2 = img2 % s2->mask_count;
        if (img2 < 0) {
            img2 += s2->mask_count;
        }
        mask2 = gm_sprite_mask(s2, img2);
    }

    sc1x = 1.0f / a->scalex;
    sc1y = 1.0f / a->scaley;
    sc2x = 1.0f / o->scalex;
    sc2y = 1.0f / o->scaley;

    l = yymax(a->bb->left, o->bb->left);
    l = floorf(l) + 0.5f;
    r = yymin(a->bb->right, o->bb->right);
    t = yymax(a->bb->top, o->bb->top);
    t = floorf(t) + 0.5f;
    b = yymin(a->bb->bottom, o->bb->bottom);
    if (!loop_range_ok(l, r) || !loop_range_ok(t, b)) {
        return false;
    }

    /* The reference clamps these edges to the texture only when
     * `colcheck === yySprite.PRECISE` - an undefined property - so the
     * clamping never happens (L2401, L2420). Reproduced by omission. */
    leftedge = s1->bbox_left;
    rightedge = s1->bbox_right + 1.0f;
    topedge = s1->bbox_top;
    bottomedge = s1->bbox_bottom + 1.0f;
    sleftedge = s2->bbox_left;
    srightedge = s2->bbox_right + 1.0f;
    stopedge = s2->bbox_top;
    sbottomedge = s2->bbox_bottom + 1.0f;

    hasrot1 = a->angle > GML_MATH_EPSILON || a->angle < -GML_MATH_EPSILON;
    hasrot2 = o->angle > GML_MATH_EPSILON || o->angle < -GML_MATH_EPSILON;

    if (!hasrot1 && !hasrot2) {
        float du1 = sc1x;
        float du2 = sc2x;
        float u1 = (l - a->x) * sc1x + s1->xorigin;
        float u2 = (l - o->x) * sc2x + s2->xorigin;

        for (i = l; i < r; i += 1.0f, u1 += du1, u2 += du2) {
            float u1i, u2i;
            if (u1 < leftedge || u1 >= rightedge) {
                continue;
            }
            if (u2 < sleftedge || u2 >= srightedge) {
                continue;
            }
            u1i = (float)gm_to_int32(u1);
            u2i = (float)gm_to_int32(u2);
            for (j = t; j < b; j += 1.0f) {
                if (prec1) {
                    float v1 = (j - a->y) * sc1y + s1->yorigin;
                    if (v1 < topedge || v1 >= bottomedge) {
                        continue;
                    }
                    if (s1->mask_count > 0 &&
                        !colmask_set(s1, u1i, (float)gm_to_int32(v1), mask1)) {
                        continue;
                    }
                }
                if (prec2) {
                    float v2 = (j - o->y) * sc2y + s2->yorigin;
                    if (v2 < stopedge || v2 >= sbottomedge) {
                        continue;
                    }
                    if (s2->mask_count > 0 &&
                        !colmask_set(s2, u2i, (float)gm_to_int32(v2), mask2)) {
                        continue;
                    }
                }
                return true;
            }
        }
    } else {
        float ss1 = 0.0f, cc1 = 0.0f, ss2 = 0.0f, cc2 = 0.0f;
        float u1 = 0.0f, u2 = 0.0f, v1, v2;

        if (hasrot1) {
            ss1 = gm_dsin(-a->angle);
            cc1 = gm_dcos(-a->angle);
        }
        if (hasrot2) {
            ss2 = gm_dsin(-o->angle);
            cc2 = gm_dcos(-o->angle);
        }
        for (i = l; i < r; i += 1.0f) {
            if (!hasrot1) {
                u1 = (i - a->x) * sc1x + s1->xorigin;
                if (u1 < leftedge || u1 >= rightedge) {
                    continue;
                }
            }
            if (!hasrot2) {
                u2 = (i - o->x) * sc2x + s2->xorigin;
                if (u2 < sleftedge || u2 >= srightedge) {
                    continue;
                }
            }
            for (j = t; j < b; j += 1.0f) {
                if (hasrot1) {
                    u1 = (cc1 * (i - a->x) + ss1 * (j - a->y)) * sc1x + s1->xorigin;
                    if (u1 < leftedge || u1 >= rightedge) {
                        continue;
                    }
                    v1 = (cc1 * (j - a->y) - ss1 * (i - a->x)) * sc1y + s1->yorigin;
                } else {
                    v1 = (j - a->y) * sc1y + s1->yorigin;
                }
                if (v1 < topedge || v1 >= bottomedge) {
                    continue;
                }
                if (prec1 && s1->mask_count > 0 &&
                    !colmask_set(s1, (float)gm_to_int32(u1), (float)gm_to_int32(v1), mask1)) {
                    continue;
                }

                if (hasrot2) {
                    u2 = (cc2 * (i - o->x) + ss2 * (j - o->y)) * sc2x + s2->xorigin;
                    if (u2 < sleftedge || u2 >= srightedge) {
                        continue;
                    }
                    v2 = (cc2 * (j - o->y) - ss2 * (i - o->x)) * sc2y + s2->yorigin;
                } else {
                    v2 = (j - o->y) * sc2y + s2->yorigin;
                }
                if (v2 < stopedge || v2 >= sbottomedge) {
                    continue;
                }
                /* The reference passes u2 untruncated here (L2563). */
                if (prec2 && s2->mask_count > 0 &&
                    !colmask_set(s2, u2, (float)gm_to_int32(v2), mask2)) {
                    continue;
                }
                return true;
            }
        }
    }
    return false;
}

/* ---- Instance primitives (yyInstance.js) --------------------------------------- */

/* Collision_Point (L1730) */
bool gm_collision_test_point(gm_instance_t *inst, float x, float y, bool prec)
{
    const gm_sprite_def_t *spr;
    coll_state_t *cs;
    bbox_t bb;

    if (inst == NULL || inst->marked_for_destroy) {
        return false;
    }
    cs = ensure_bbox(inst, &bb);
    if (x >= bb.right + COL_DELTA) {
        return false;
    }
    if (x < bb.left) {
        return false;
    }
    if (y >= bb.bottom + COL_DELTA) {
        return false;
    }
    if (y < bb.top) {
        return false;
    }
    spr = collision_sprite(inst);
    if (spr == NULL || spr->frame_count == 0) {
        return false;
    }
    if (!prec || cs->colcheck == GM_COLCHECK_AABB) {
        return true;
    }
    return precise_point(spr, inst->image_index, gm_round(inst->x), gm_round(inst->y),
                         inst->image_xscale, inst->image_yscale, inst->image_angle,
                         gm_round(x), gm_round(y));
}

/* Collision_Rectangle (L1807) */
bool gm_collision_test_rectangle(gm_instance_t *inst, float x1, float y1,
                                 float x2, float y2, bool prec)
{
    const gm_sprite_def_t *spr;
    coll_state_t *cs;
    bbox_t bb, rr;
    float bl, br, bt, bbm;

    if (inst == NULL || inst->marked_for_destroy) {
        return false;
    }
    cs = ensure_bbox(inst, &bb);

    bl = yymin(x1, x2);
    if (bl >= bb.right) {
        return false;
    }
    br = yymax(x1, x2);
    if (br < bb.left) {
        return false;
    }
    bt = yymin(y1, y2);
    if (bt >= bb.bottom) {
        return false;
    }
    bbm = yymax(y1, y2);
    if (bbm < bb.top) {
        return false;
    }

    spr = collision_sprite(inst);
    if (spr == NULL || spr->frame_count == 0) {
        return false;
    }
    if (!prec || cs->colcheck == GM_COLCHECK_AABB) {
        float l = yymax(bl, bb.left);
        float t = yymax(bt, bb.top);
        float r = yymin(br, bb.right);
        float b = yymin(bbm, bb.bottom);
        if (floorf(l + 0.5f) == floorf(r + 0.5f)) {
            return false;
        }
        if (floorf(t + 0.5f) == floorf(b + 0.5f)) {
            return false;
        }
        return true;
    }

    rr.left = gm_round(yymin(x1, x2));
    rr.top = gm_round(yymin(y1, y2));
    rr.right = gm_round(yymax(x1, x2));
    rr.bottom = gm_round(yymax(y1, y2));
    return precise_rectangle(spr, inst->image_index, &bb, gm_round(inst->x), gm_round(inst->y),
                             inst->image_xscale, inst->image_yscale, inst->image_angle, &rr);
}

/* Collision_Line (L2053) */
bool gm_collision_test_line(gm_instance_t *inst, float x1, float y1,
                            float x2, float y2, bool prec)
{
    const gm_sprite_def_t *spr;
    coll_state_t *cs;
    bbox_t bb;

    if (inst == NULL || inst->marked_for_destroy) {
        return false;
    }
    cs = ensure_bbox(inst, &bb);

    if (yymin(x1, x2) >= bb.right) {
        return false;
    }
    if (yymax(x1, x2) < bb.left) {
        return false;
    }
    if (yymin(y1, y2) >= bb.bottom) {
        return false;
    }
    if (yymax(y1, y2) < bb.top) {
        return false;
    }

    /* make the line run left to right, then clip it to the box */
    if (x2 < x1) {
        float val = x2;
        x2 = x1;
        x1 = val;
        val = y2;
        y2 = y1;
        y1 = val;
    }
    if (x1 < bb.left) {
        y1 = y1 + (bb.left - x1) * (y2 - y1) / (x2 - x1);
        x1 = bb.left;
    }
    if (x2 > (bb.right + COL_DELTA)) {
        y2 = y2 + (bb.right + COL_DELTA - x2) * (y2 - y1) / (x2 - x1);
        x2 = bb.right + COL_DELTA;
    }
    if (y1 < bb.top && y2 < bb.top) {
        return false;
    }
    if (y1 > bb.bottom && y2 > bb.bottom) {
        return false;
    }

    spr = collision_sprite(inst);
    if (spr == NULL || spr->frame_count == 0) {
        return false;
    }
    if (!prec || cs->colcheck == GM_COLCHECK_AABB) {
        return true;
    }
    return precise_line(spr, gm_to_int32(inst->image_index), &bb,
                        gm_round(inst->x), gm_round(inst->y),
                        inst->image_xscale, inst->image_yscale, inst->image_angle,
                        gm_round(x1), gm_round(y1), gm_round(x2), gm_round(y2));
}

/* Collision_Instance (L2539): `inst` is `this`, `other` is `_pInst`. */
bool gm_collision_test_instance(gm_instance_t *inst, gm_instance_t *other, bool prec)
{
    const gm_sprite_def_t *spr1, *spr2;
    coll_state_t *cs1, *cs2;
    bbox_t b1, b2; /* b1 = other's box, b2 = inst's box, as in the reference */
    precise_side_t side1, side2;

    if (inst == NULL || other == NULL || inst == other || inst->marked_for_destroy ||
        other->marked_for_destroy) {
        return false;
    }
    cs2 = ensure_bbox(inst, &b2);
    cs1 = ensure_bbox(other, &b1);

    if (b1.left >= b2.right) {
        return false;
    }
    if (b1.right <= b2.left) {
        return false;
    }
    if (b1.top >= b2.bottom) {
        return false;
    }
    if (b1.bottom <= b2.top) {
        return false;
    }

    spr1 = collision_sprite(inst);
    if (spr1 == NULL || spr1->frame_count == 0) {
        return false;
    }
    spr2 = collision_sprite(other);
    if (spr2 == NULL || spr2->frame_count == 0) {
        return false;
    }

    if (!prec || (cs2->colcheck == GM_COLCHECK_AABB && cs1->colcheck == GM_COLCHECK_AABB)) {
        float l = yymax(b1.left, b2.left);
        float t = yymax(b1.top, b2.top);
        float r = yymin(b1.right, b2.right);
        float b = yymin(b1.bottom, b2.bottom);
        if (floorf(l + 0.5f) == floorf(r + 0.5f)) {
            return false;
        }
        if (floorf(t + 0.5f) == floorf(b + 0.5f)) {
            return false;
        }
        return true;
    }

    side1.spr = spr1;
    side1.img = gm_to_int32(inst->image_index);
    side1.bb = &b2;
    side1.x = gm_round(inst->x);
    side1.y = gm_round(inst->y);
    side1.scalex = inst->image_xscale;
    side1.scaley = inst->image_yscale;
    side1.angle = inst->image_angle;

    side2.spr = spr2;
    side2.img = gm_to_int32(other->image_index);
    side2.bb = &b1;
    side2.x = gm_round(other->x);
    side2.y = gm_round(other->y);
    side2.scalex = other->image_xscale;
    side2.scaley = other->image_yscale;
    side2.angle = other->image_angle;
    return precise_collision(&side1, &side2);
}

/* ---- Grid ------------------------------------------------------------------------ */

static bool grid_active(void)
{
    return s_cols > 0;
}

static int cell_clamp(float c, int n)
{
    if (!(c > 0.0f)) {
        return 0;
    }
    if (c >= (float)(n - 1)) {
        return n - 1;
    }
    return (int)c;
}

/* Cell range covered by the half-open box [l, r) x [t, b): the region in
 * which every hit test can succeed (see the proof sketch in
 * grid_query_cells). False when the box is degenerate or not finite. */
static bool instance_cells(const gm_instance_t *inst, int *cx0, int *cy0, int *cx1, int *cy1)
{
    float l = inst->bbox_left, t = inst->bbox_top;
    float r = inst->bbox_right, b = inst->bbox_bottom;

    if (!(r > l && b > t) || !isfinite(l) || !isfinite(r) || !isfinite(t) || !isfinite(b)) {
        return false;
    }
    *cx0 = cell_clamp(floorf(l / s_cell_size), s_cols);
    *cx1 = cell_clamp(ceilf(r / s_cell_size) - 1.0f, s_cols);
    *cy0 = cell_clamp(floorf(t / s_cell_size), s_rows);
    *cy1 = cell_clamp(ceilf(b / s_cell_size) - 1.0f, s_rows);
    return true;
}

static void big_link(int slot)
{
    s_big_prev[slot] = NIL;
    s_big_next[slot] = s_big_head;
    if (s_big_head != NIL) {
        s_big_prev[s_big_head] = (int16_t)slot;
    }
    s_big_head = (int16_t)slot;
}

static void big_unlink(int slot)
{
    if (s_big_prev[slot] != NIL) {
        s_big_next[s_big_prev[slot]] = s_big_next[slot];
    } else {
        s_big_head = s_big_next[slot];
    }
    if (s_big_next[slot] != NIL) {
        s_big_prev[s_big_next[slot]] = s_big_prev[slot];
    }
}

static void entry_link(int e, int cell)
{
    s_entry_cell[e] = (int16_t)cell;
    s_entry_prev[e] = NIL;
    s_entry_next[e] = s_cell_head[cell];
    if (s_cell_head[cell] != NIL) {
        s_entry_prev[s_cell_head[cell]] = (int16_t)e;
    }
    s_cell_head[cell] = (int16_t)e;
}

static void entry_unlink(int e)
{
    int cell = s_entry_cell[e];

    if (s_entry_prev[e] != NIL) {
        s_entry_next[s_entry_prev[e]] = s_entry_next[e];
    } else {
        s_cell_head[cell] = s_entry_next[e];
    }
    if (s_entry_next[e] != NIL) {
        s_entry_prev[s_entry_next[e]] = s_entry_prev[e];
    }
}

static void grid_remove(gm_instance_t *inst)
{
    int slot = gm_instance_slot(inst);
    coll_state_t *cs = &s_cs[slot];
    int k;

    if (!cs->in_grid) {
        return;
    }
    if (cs->big) {
        big_unlink(slot);
    } else {
        for (k = 0; k < cs->ncells; ++k) {
            entry_unlink(slot * GM_GRID_CELLS_PER_INSTANCE + k);
        }
    }
    cs->in_grid = false;
    s_grid_count--;
}

static void grid_insert(gm_instance_t *inst)
{
    int slot = gm_instance_slot(inst);
    coll_state_t *cs = ensure_bbox(inst, NULL);
    int cx0, cy0, cx1, cy1;

    cs->big = true;
    cs->ncells = 0;
    if (instance_cells(inst, &cx0, &cy0, &cx1, &cy1) &&
        (cx1 - cx0 + 1) * (cy1 - cy0 + 1) <= GM_GRID_CELLS_PER_INSTANCE) {
        int cx, cy, k = 0;
        for (cy = cy0; cy <= cy1; ++cy) {
            for (cx = cx0; cx <= cx1; ++cx) {
                entry_link(slot * GM_GRID_CELLS_PER_INSTANCE + k, cy * s_cols + cx);
                k++;
            }
        }
        cs->big = false;
        cs->ncells = k;
        cs->cx0 = cx0;
        cs->cy0 = cy0;
        cs->cx1 = cx1;
        cs->cy1 = cy1;
    } else {
        big_link(slot);
    }
    cs->cells_stale = false;
    cs->in_grid = true;
    s_grid_count++;
}

/* Re-buckets an in-grid instance whose cells changed. Compares against the
 * stored buckets (not the bbox cache, which other queries may already have
 * refreshed after an untracked move). */
static void grid_refresh(gm_instance_t *inst)
{
    coll_state_t *cs = &s_cs[gm_instance_slot(inst)];
    int cx0, cy0, cx1, cy1;
    bool fits;

    if (!cs->in_grid) {
        return;
    }
    ensure_bbox(inst, NULL);
    if (!cs->cells_stale) {
        return; /* bbox unchanged since the cells were computed */
    }
    cs->cells_stale = false;
    fits = instance_cells(inst, &cx0, &cy0, &cx1, &cy1) &&
           (cx1 - cx0 + 1) * (cy1 - cy0 + 1) <= GM_GRID_CELLS_PER_INSTANCE;
    if (fits ? (!cs->big && cx0 == cs->cx0 && cy0 == cs->cy0 && cx1 == cs->cx1 && cy1 == cs->cy1)
             : cs->big) {
        return; /* same buckets */
    }
    grid_remove(inst);
    grid_insert(inst);
}

static void grid_clear_contents(void)
{
    int i;

    for (i = 0; i < GRID_MAX_CELLS; ++i) {
        s_cell_head[i] = NIL;
    }
    s_big_head = NIL;
    for (i = 0; i < GM_INSTANCE_MAX; ++i) {
        s_cs[i].in_grid = false;
    }
    s_grid_count = 0;
}

static bool wants_grid(const gm_instance_t *inst)
{
    return grid_active() && inst->active && !inst->marked_for_destroy &&
           gm_object_valid(inst->object_index) && s_indexed[inst->object_index];
}

static void grid_insert_all(void)
{
    int i, n = gm_instance_active_count();

    for (i = 0; i < n; ++i) {
        gm_instance_t *inst = gm_instance_active_at(i);
        if (!s_cs[gm_instance_slot(inst)].in_grid && wants_grid(inst)) {
            grid_insert(inst);
        }
    }
}

/* ---- Instance hooks --------------------------------------------------------------- */

static void hook_reset(void)
{
    memset(s_cs, 0, sizeof(s_cs));
    grid_clear_contents();
}

static void hook_enter(gm_instance_t *inst)
{
    /* Room instances reuse fixed ids, so a re-created instance can land in its
     * old slot with an identical cache key while inst->bbox_* was reset. */
    s_cs[gm_instance_slot(inst)].valid = false;
    if (wants_grid(inst)) {
        grid_insert(inst);
    }
}

static void hook_leave(gm_instance_t *inst)
{
    grid_remove(inst);
}

static const gm_instance_hooks_t s_hooks = { hook_reset, hook_enter, hook_leave };

void gm_collision_init(void)
{
    gm_collision_shutdown();
    gm_instance_set_hooks(&s_hooks);
}

void gm_collision_shutdown(void)
{
    gm_instance_set_hooks(NULL);
    memset(s_cs, 0, sizeof(s_cs));
    memset(s_stamp, 0, sizeof(s_stamp));
    s_stamp_counter = 0;
    s_cols = s_rows = 0;
    s_cell_size = 0.0f;
    s_root_count = 0;
    memset(s_indexed, 0, sizeof(s_indexed));
    s_verify = false;
    s_verify_failures = 0;
    grid_clear_contents();
}

/* ---- Grid configuration ------------------------------------------------------------ */

void gm_collision_index_default_roots(void)
{
    int o, count = gm_object_count();

    for (o = 0; o < count; ++o) {
        const gm_object_def_t *def = gm_object_get(o);
        if (def != NULL && def->name != NULL && strcmp(def->name, "oSolid") == 0) {
            gm_collision_grid_index_object(o);
            break;
        }
    }
}

void gm_collision_grid_configure(float room_width, float room_height, float cell_size)
{
    float cols, rows;

    grid_clear_contents();
    s_cols = s_rows = 0;
    s_cell_size = 0.0f;
    if (!(cell_size > 0.0f) || !isfinite(cell_size) || !(room_width > 0.0f) ||
        !(room_height > 0.0f) || !isfinite(room_width) || !isfinite(room_height)) {
        return;
    }
    if (s_root_count == 0) {
        gm_collision_index_default_roots();
    }
    for (;;) {
        cols = ceilf(room_width / cell_size);
        rows = ceilf(room_height / cell_size);
        if (cols <= GM_GRID_MAX_COLS && rows <= GM_GRID_MAX_ROWS) {
            break;
        }
        cell_size *= 2.0f;
    }
    s_cols = (int)cols;
    s_rows = (int)rows;
    s_cell_size = cell_size;
    grid_insert_all();
}

bool gm_collision_grid_index_object(int object_index)
{
    int o, r, n = gm_object_count();

    if (!gm_object_valid(object_index)) {
        return false;
    }
    for (r = 0; r < s_root_count; ++r) {
        if (s_roots[r] == object_index) {
            return true;
        }
    }
    if (s_root_count >= GM_GRID_MAX_ROOTS) {
        return false;
    }
    s_roots[s_root_count++] = object_index;
    for (o = 0; o < n; ++o) {
        if (gm_object_is_a(o, object_index)) {
            s_indexed[o] = true;
        }
    }
    grid_insert_all();
    return true;
}

bool gm_collision_grid_is_indexed(int object_index)
{
    return gm_object_valid(object_index) && s_indexed[object_index];
}

void gm_collision_touch(gm_instance_t *inst)
{
    if (inst != NULL && s_cs[gm_instance_slot(inst)].in_grid) {
        grid_refresh(inst);
    }
}

void gm_collision_grid_sync(void)
{
    int i, n = gm_instance_active_count();

    for (i = 0; i < n; ++i) {
        grid_refresh(gm_instance_active_at(i));
    }
}

void gm_collision_set_verify(bool enabled)
{
    s_verify = enabled;
    s_verify_failures = 0;
}

long gm_collision_verify_failures(void)
{
    return s_verify_failures;
}

float gm_collision_grid_cell_size(void)
{
    return s_cell_size;
}

int gm_collision_grid_indexed_count(void)
{
    return s_grid_count;
}

/* ---- Queries ------------------------------------------------------------------------ */

typedef enum { Q_POINT, Q_RECT, Q_LINE, Q_PLACE } query_kind_t;

typedef struct query {
    query_kind_t kind;
    float x1, y1, x2, y2;
    bool prec;
    gm_instance_t *place; /* Q_PLACE: the placed instance (_pInst) */
    gm_instance_t *skip;  /* notme */
} query_t;

static bool query_hit(const query_t *q, gm_instance_t *cand)
{
    switch (q->kind) {
    case Q_POINT:
        return gm_collision_test_point(cand, q->x1, q->y1, q->prec);
    case Q_RECT:
        return gm_collision_test_rectangle(cand, q->x1, q->y1, q->x2, q->y2, q->prec);
    case Q_LINE:
        return gm_collision_test_line(cand, q->x1, q->y1, q->x2, q->y2, q->prec);
    case Q_PLACE:
        /* Command_InstancePlace: _pInstance.Collision_Instance(_pInst, true) */
        return gm_collision_test_instance(cand, q->place, true);
    default:
        return false;
    }
}

/* Instance_SearchLoop: the first hit in list order. */
static gm_instance_t *query_linear(const query_t *q, int target)
{
    gm_instance_cursor_t c;
    gm_instance_t *inst;

    gm_instance_search_begin(&c, target);
    while ((inst = gm_instance_search_next(&c)) != NULL) {
        if (inst == q->skip) {
            continue;
        }
        gm_perf_count(GM_PERF_COLLISION_QUERY_CANDIDATES, 1);
        if (query_hit(q, inst)) {
            return inst;
        }
    }
    return NULL;
}

/* Closed query box [x0, x1] x [y0, y1] -> cell range. Every hit test above
 * rejects candidates whose half-open box [l, r) x [t, b) misses the query box
 * (point: l <= x < r; rect/line: qmin < r && qmax >= l; place: strict
 * overlap), and floor/ceil/clamp are monotonic, so the cell ranges of any
 * hit intersect. False when the range is unusable (non-finite or too big). */
static bool grid_query_cells(float x0, float y0, float x1, float y1,
                             int *cx0, int *cy0, int *cx1, int *cy1)
{
    if (!isfinite(x0) || !isfinite(x1) || !isfinite(y0) || !isfinite(y1)) {
        return false;
    }
    *cx0 = cell_clamp(floorf(x0 / s_cell_size), s_cols);
    *cx1 = cell_clamp(floorf(x1 / s_cell_size), s_cols);
    *cy0 = cell_clamp(floorf(y0 / s_cell_size), s_rows);
    *cy1 = cell_clamp(floorf(y1 / s_cell_size), s_rows);
    return (*cx1 - *cx0 + 1) * (*cy1 - *cy0 + 1) <= GM_GRID_MAX_QUERY_CELLS;
}

static void consider(const query_t *q, int target, int slot, gm_instance_t **best)
{
    gm_instance_t *inst;

    if (s_stamp[slot] == s_stamp_counter) {
        return;
    }
    s_stamp[slot] = s_stamp_counter;
    gm_perf_count(GM_PERF_COLLISION_QUERY_CANDIDATES, 1);
    inst = gm_instance_at_slot(slot);
    if (inst == NULL || !inst->active || inst->marked_for_destroy || inst == q->skip ||
        !gm_object_is_a(inst->object_index, target)) {
        return;
    }
    /* The object list is in link_serial order: keep the earliest hit. */
    if (*best != NULL && inst->link_serial >= (*best)->link_serial) {
        return;
    }
    if (query_hit(q, inst)) {
        *best = inst;
    }
}

/* Returns false when the grid cannot serve the query (caller goes linear). */
static bool query_grid(const query_t *q, int target, float x0, float y0,
                       float x1, float y1, gm_instance_t **result)
{
    gm_instance_t *best = NULL;
    int cx0, cy0, cx1, cy1, cx, cy, slot;

    if (!grid_active() || !gm_collision_grid_is_indexed(target) ||
        !grid_query_cells(x0, y0, x1, y1, &cx0, &cy0, &cx1, &cy1)) {
        return false;
    }
    if (++s_stamp_counter == 0) {
        memset(s_stamp, 0, sizeof(s_stamp));
        s_stamp_counter = 1;
    }
    for (cy = cy0; cy <= cy1; ++cy) {
        for (cx = cx0; cx <= cx1; ++cx) {
            int e = s_cell_head[cy * s_cols + cx];
            while (e != NIL) {
                consider(q, target, e / GM_GRID_CELLS_PER_INSTANCE, &best);
                e = s_entry_next[e];
            }
        }
    }
    for (slot = s_big_head; slot != NIL; slot = s_big_next[slot]) {
        consider(q, target, slot, &best);
    }
    *result = best;
    return true;
}

static gm_instance_t *run_query(const query_t *q, int target, float x0, float y0,
                                float x1, float y1)
{
    uint64_t perf_started = gm_perf_timer_begin();
    gm_instance_t *hit;

    gm_perf_count(GM_PERF_COLLISION_QUERIES, 1);
    if (!query_grid(q, target, x0, y0, x1, y1, &hit)) {
        gm_perf_count(GM_PERF_COLLISION_LINEAR_QUERIES, 1);
        hit = query_linear(q, target);
    } else {
        gm_perf_count(GM_PERF_COLLISION_GRID_QUERIES, 1);
        if (s_verify && query_linear(q, target) != hit) {
            s_verify_failures++;
        }
    }
    if (hit != NULL) {
        gm_perf_count(GM_PERF_COLLISION_QUERY_HITS, 1);
    }
    gm_perf_timer_end(GM_PERF_TIMER_COLLISION_QUERY, perf_started);
    return hit;
}

gm_instance_t *gm_collision_point(gm_instance_t *self, float x, float y,
                                  int target, bool prec, bool notme)
{
    query_t q;

    q.kind = Q_POINT;
    q.x1 = x;
    q.y1 = y;
    q.x2 = x;
    q.y2 = y;
    q.prec = prec;
    q.place = NULL;
    q.skip = notme ? self : NULL;
    return run_query(&q, target, x, y, x, y);
}

gm_instance_t *gm_collision_rectangle(gm_instance_t *self, float x1, float y1,
                                      float x2, float y2, int target,
                                      bool prec, bool notme)
{
    query_t q;

    q.kind = Q_RECT;
    q.x1 = x1;
    q.y1 = y1;
    q.x2 = x2;
    q.y2 = y2;
    q.prec = prec;
    q.place = NULL;
    q.skip = notme ? self : NULL;
    return run_query(&q, target, yymin(x1, x2), yymin(y1, y2), yymax(x1, x2), yymax(y1, y2));
}

gm_instance_t *gm_collision_line(gm_instance_t *self, float x1, float y1,
                                 float x2, float y2, int target,
                                 bool prec, bool notme)
{
    query_t q;

    q.kind = Q_LINE;
    q.x1 = x1;
    q.y1 = y1;
    q.x2 = x2;
    q.y2 = y2;
    q.prec = prec;
    q.place = NULL;
    q.skip = notme ? self : NULL;
    return run_query(&q, target, yymin(x1, x2), yymin(y1, y2), yymax(x1, x2), yymax(y1, y2));
}

/* Command_InstancePlace (Function_Movement.js L473): move self to (x, y),
 * search (notme = false; Collision_Instance skips self), move back. */
gm_instance_t *gm_collision_instance_place(gm_instance_t *self, float x, float y, int target)
{
    float xx, yy;
    gm_instance_t *hit;
    query_t q;

    if (self == NULL) {
        return NULL;
    }
    xx = self->x;
    yy = self->y;
    self->x = x;
    self->y = y;
    gm_collision_update_bbox(self);

    q.kind = Q_PLACE;
    q.x1 = q.y1 = q.x2 = q.y2 = 0.0f;
    q.prec = true;
    q.place = self;
    q.skip = NULL;
    hit = run_query(&q, target, self->bbox_left, self->bbox_top,
                    self->bbox_right, self->bbox_bottom);

    self->x = xx;
    self->y = yy;
    gm_collision_update_bbox(self);
    return hit;
}

bool gm_collision_place_meeting(gm_instance_t *self, float x, float y, int target)
{
    return gm_collision_instance_place(self, x, y, target) != NULL;
}

/* Command_InstancePosition (Function_Movement.js L503) and position_meeting
 * (Command_CollisionPoint with prec = true, notme = false): identical walks. */
gm_instance_t *gm_collision_instance_position(float x, float y, int target)
{
    return gm_collision_point(NULL, x, y, target, true, false);
}

bool gm_collision_position_meeting(float x, float y, int target)
{
    return gm_collision_instance_position(x, y, target) != NULL;
}

/* distance_to_object (Function_Movement.js L902) via Instance_SearchLoop2. */
float gm_collision_distance_to_object(gm_instance_t *self, int target)
{
    float dist = GM_COLLISION_NO_DISTANCE;
    gm_instance_cursor_t c;
    gm_instance_t *inst;
    bbox_t s;

    if (self == NULL) {
        return dist;
    }
    ensure_bbox(self, &s);
    gm_instance_search_begin(&c, target);
    while ((inst = gm_instance_search_next(&c)) != NULL) {
        bbox_t r;
        float xd = 0.0f, yd = 0.0f, d;

        ensure_bbox(inst, &r);
        if (r.left > s.right) {
            xd = r.left - s.right;
        }
        if (r.right < s.left) {
            xd = r.right - s.left;
        }
        if (r.top > s.bottom) {
            yd = r.top - s.bottom;
        }
        if (r.bottom < s.top) {
            yd = r.bottom - s.top;
        }
        d = sqrtf(xd * xd + yd * yd);
        if (d < dist) {
            dist = d;
        }
    }
    return dist;
}

/* distance_to_point (Function_Movement.js L869) */
float gm_collision_distance_to_point(gm_instance_t *self, float x, float y)
{
    float xd = 0.0f, yd = 0.0f;
    bbox_t r;

    if (self == NULL) {
        return 0.0f;
    }
    ensure_bbox(self, &r);
    if (x > r.right) {
        xd = x - r.right;
    }
    if (x < r.left) {
        xd = x - r.left;
    }
    if (y > r.bottom) {
        yd = y - r.bottom;
    }
    if (y < r.top) {
        yd = y - r.top;
    }
    return sqrtf(xd * xd + yd * yd);
}

void gm_move_snap(gm_instance_t *inst, float hsnap, float vsnap)
{
    if (inst == NULL) {
        return;
    }
    if (hsnap > 0.0f) {
        inst->x = roundf(inst->x / (float)hsnap) * (float)hsnap;
    }
    if (vsnap > 0.0f) {
        inst->y = roundf(inst->y / (float)vsnap) * (float)vsnap;
    }
}

