/*
 * gm_with - see gm_with.h.
 *
 * Two LIFO stacks: with frames and event contexts. Each with frame records
 * the context depth at its begin, so "who started last" is a comparison:
 * events are strictly nested, hence a frame whose recorded depth equals the
 * current one began after the innermost event. Depth counters keep counting
 * past capacity (entries beyond it are simply not stored), which keeps
 * begin/end and push/pop balanced on overflow.
 */
#include "gm_with.h"

typedef struct with_frame {
    int base;               /* first slot in s_slots */
    int count;              /* snapshot size */
    int pos;                /* next snapshot index */
    int ctx_depth;          /* s_ctx_depth at begin */
    gm_instance_t *self;    /* instance being visited (NULL between visits) */
    gm_instance_t *outer;   /* self of the code that ran the with */
} with_frame_t;

typedef struct ctx_frame {
    gm_instance_t *self;
    gm_instance_t *other;
} ctx_frame_t;

static with_frame_t s_frames[GM_WITH_MAX_DEPTH];
static int s_depth;
static uint16_t s_slots[GM_WITH_SLOT_POOL];
static int s_slot_top;
static ctx_frame_t s_ctx[GM_CTX_MAX_DEPTH];
static int s_ctx_depth;
static long s_overflows;

void gm_with_reset(void)
{
    s_depth = 0;
    s_slot_top = 0;
    s_ctx_depth = 0;
    s_overflows = 0;
}

/* ------------------------------------------------------------------ context */

void gm_ctx_push(gm_instance_t *self, gm_instance_t *other)
{
    if (s_ctx_depth < GM_CTX_MAX_DEPTH) {
        s_ctx[s_ctx_depth].self = self;
        s_ctx[s_ctx_depth].other = other;
    } else {
        s_overflows++;
    }
    s_ctx_depth++;
}

void gm_ctx_pop(void)
{
    if (s_ctx_depth > 0) {
        s_ctx_depth--;
    }
}

/* The with frame whose body is the innermost running code, if any. */
static const with_frame_t *innermost_with(void)
{
    int i = (s_depth < GM_WITH_MAX_DEPTH ? s_depth : GM_WITH_MAX_DEPTH) - 1;

    for (; i >= 0; --i) {
        const with_frame_t *f = &s_frames[i];
        if (f->ctx_depth != s_ctx_depth) {
            return NULL; /* an event started after this with */
        }
        if (f->self != NULL) {
            return f;
        }
    }
    return NULL;
}

static const ctx_frame_t *innermost_ctx(void)
{
    if (s_ctx_depth == 0) {
        return NULL;
    }
    return &s_ctx[(s_ctx_depth < GM_CTX_MAX_DEPTH ? s_ctx_depth : GM_CTX_MAX_DEPTH) - 1];
}

gm_instance_t *gm_ctx_self(void)
{
    const with_frame_t *f = innermost_with();
    const ctx_frame_t *c;

    if (f != NULL) {
        return f->self;
    }
    c = innermost_ctx();
    return c != NULL ? c->self : NULL;
}

gm_instance_t *gm_ctx_other(void)
{
    const with_frame_t *f = innermost_with();
    const ctx_frame_t *c;

    if (f != NULL) {
        return f->outer;
    }
    c = innermost_ctx();
    return c != NULL ? c->other : NULL;
}

/* ------------------------------------------------------------------ with */

void gm_with_begin(int target, gm_instance_t *self, gm_instance_t *other)
{
    with_frame_t *f;
    int n;

    if (s_depth >= GM_WITH_MAX_DEPTH) {
        s_overflows++;
        s_depth++; /* unstored frame: its body runs zero times */
        return;
    }
    f = &s_frames[s_depth++];
    f->base = s_slot_top;
    f->pos = 0;
    f->ctx_depth = s_ctx_depth;
    f->self = NULL;
    f->outer = self;
    n = gm_instance_with_snapshot(target, self, other, &s_slots[s_slot_top], GM_WITH_SLOT_POOL - s_slot_top);
    if (n < 0) {
        s_overflows++;
        n = 0;
    }
    f->count = n;
    s_slot_top += n;
}

gm_instance_t *gm_with_next(void)
{
    with_frame_t *f;

    if (s_depth == 0 || s_depth > GM_WITH_MAX_DEPTH) {
        return NULL;
    }
    f = &s_frames[s_depth - 1];
    while (f->pos < f->count) {
        gm_instance_t *inst = gm_instance_with_slot(s_slots[f->base + f->pos++]);
        if (inst != NULL) {
            f->self = inst;
            return inst;
        }
    }
    f->self = NULL;
    return NULL;
}

void gm_with_end(void)
{
    if (s_depth == 0) {
        return;
    }
    s_depth--;
    if (s_depth < GM_WITH_MAX_DEPTH) {
        s_slot_top = s_frames[s_depth].base;
    }
}

int gm_with_depth(void)
{
    return s_depth;
}

void gm_with_unwind(int depth)
{
    while (s_depth > depth && s_depth > 0) {
        gm_with_end();
    }
}

long gm_with_overflows(void)
{
    return s_overflows;
}
