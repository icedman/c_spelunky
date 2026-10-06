#ifndef GM_PERF_H
#define GM_PERF_H

#include <stdbool.h>
#include <stdint.h>

#ifndef ENABE_PERF
// #define ENABLE_PERF
#endif

typedef uint64_t (*gm_perf_now_fn)(void);

typedef enum gm_perf_timer {
    GM_PERF_TIMER_FRAME,
    GM_PERF_TIMER_STEP,
    GM_PERF_TIMER_COLLISION,
    GM_PERF_TIMER_COLLISION_QUERY,
    GM_PERF_TIMER_DRAW,
    GM_PERF_TIMER_HOST_DRAW,
    GM_PERF_TIMER_GC,
    GM_PERF_TIMER_DRAW_HANDLERS, /* custom Draw handlers (incl. their host calls) */
    GM_PERF_TIMER_DRAW_DEFAULT,  /* default draw_self (incl. host calls) */
    GM_PERF_TIMER_DRAW_LAYERS,   /* background/tile layers (incl. host calls) */
    /* gm_loop_step phases, mirroring zig/runtime/profile.zig. Phases cut short by a
     * room change are not timed. */
    GM_PERF_TIMER_IMAGES,        /* input start, old positions, image advance */
    GM_PERF_TIMER_STEP_BEGIN,    /* layer update + Step Begin handlers */
    GM_PERF_TIMER_ALARMS,        /* alarm ticking + handlers */
    GM_PERF_TIMER_KEYBOARD,      /* keyboard events */
    GM_PERF_TIMER_STEP_NORMAL,   /* Step handlers */
    GM_PERF_TIMER_POSITIONS,     /* speeds, gravity, friction, x/y update */
    GM_PERF_TIMER_OTHER,         /* outside/boundary events */
    GM_PERF_TIMER_STEP_END,      /* Step End handlers */
    GM_PERF_TIMER_RECLAIM,       /* destroyed instance reclaim */
    GM_PERF_TIMER_COUNT
} gm_perf_timer_t;

typedef enum gm_perf_counter {
    GM_PERF_AUTO_COLLISION_CANDIDATES,
    GM_PERF_AUTO_COLLISION_HITS,
    GM_PERF_COLLISION_QUERIES,
    GM_PERF_COLLISION_GRID_QUERIES,
    GM_PERF_COLLISION_LINEAR_QUERIES,
    GM_PERF_COLLISION_QUERY_CANDIDATES,
    GM_PERF_COLLISION_QUERY_HITS,
    GM_PERF_PRECISE_MASK_SAMPLES,
    GM_PERF_TILES_VISITED,
    GM_PERF_TILES_DRAWN,
    GM_PERF_IMAGES,
    GM_PERF_IMAGES_PLAIN,
    GM_PERF_IMAGES_PLAIN_FAST,
    GM_PERF_IMAGES_PLAIN_FALLBACK,
    GM_PERF_IMAGES_CULLED,
    GM_PERF_IMAGES_REGION,
    GM_PERF_IMAGES_SCALED,
    GM_PERF_IMAGES_ROTATED,
    GM_PERF_GLYPHS,
    GM_PERF_DRAW_PASSES,
    GM_PERF_DRAW_INSTANCES,
    GM_PERF_DRAW_HANDLER_CALLS,
    GM_PERF_DRAW_DEFAULT_CALLS,
    GM_PERF_GRAVITY_INSTANCES,
    GM_PERF_GC_RUNS,
    GM_PERF_COUNTER_COUNT
} gm_perf_counter_t;

typedef struct gm_perf_report {
    uint32_t frames;
    uint64_t wall_us;
    uint64_t instances_sum; /* active instances summed over the frames */
    uint64_t timer_us[GM_PERF_TIMER_COUNT];
    uint64_t timer_max_us[GM_PERF_TIMER_COUNT];
    uint64_t counters[GM_PERF_COUNTER_COUNT];
} gm_perf_report_t;

void gm_perf_init(gm_perf_now_fn now, uint32_t report_frames);
bool gm_perf_enabled(void);
uint64_t gm_perf_now(void);
void gm_perf_begin_frame(void);
void gm_perf_end_frame(int active_instances);
uint64_t gm_perf_timer_begin(void);
void gm_perf_timer_end(gm_perf_timer_t timer, uint64_t started);
void gm_perf_timer_end_scaled(gm_perf_timer_t timer, uint64_t started, uint32_t scale);
void gm_perf_count(gm_perf_counter_t counter, uint64_t amount);
bool gm_perf_report_due(void);
bool gm_perf_take_report(gm_perf_report_t *out);

#endif
