#include "gm_perf.h"

#include <string.h>

static gm_perf_now_fn s_now;
static uint32_t s_report_frames;
static uint32_t s_frames;
static uint64_t s_window_started;
static uint64_t s_frame_started;
static uint64_t s_instances_sum;
static uint64_t s_frame_timers[GM_PERF_TIMER_COUNT];
static uint64_t s_timer_us[GM_PERF_TIMER_COUNT];
static uint64_t s_timer_max_us[GM_PERF_TIMER_COUNT];
static uint64_t s_counters[GM_PERF_COUNTER_COUNT];

void gm_perf_init(gm_perf_now_fn now, uint32_t report_frames)
{
    s_now = now;
    s_report_frames = report_frames > 0 ? report_frames : 30;
    s_frames = 0;
    s_window_started = 0;
    s_frame_started = 0;
    s_instances_sum = 0;
    memset(s_frame_timers, 0, sizeof(s_frame_timers));
    memset(s_timer_us, 0, sizeof(s_timer_us));
    memset(s_timer_max_us, 0, sizeof(s_timer_max_us));
    memset(s_counters, 0, sizeof(s_counters));
}

bool gm_perf_enabled(void)
{
    #ifndef ENABLE_PERF
    return false;
    #endif
    return s_now != NULL;
}

uint64_t gm_perf_now(void)
{
    return s_now != NULL ? s_now() : 0;
}

void gm_perf_begin_frame(void)
{
    uint64_t now;

    if (s_now == NULL) {
        return;
    }
    now = s_now();
    if (s_frames == 0) {
        s_window_started = now;
    }
    s_frame_started = now;
    memset(s_frame_timers, 0, sizeof(s_frame_timers));
}

void gm_perf_end_frame(int active_instances)
{
    uint64_t now, elapsed;
    int i;

    if (s_now == NULL || s_frame_started == 0) {
        return;
    }
    now = s_now();
    elapsed = now >= s_frame_started ? now - s_frame_started : 0;
    s_frame_timers[GM_PERF_TIMER_FRAME] += elapsed;
    for (i = 0; i < GM_PERF_TIMER_COUNT; ++i) {
        s_timer_us[i] += s_frame_timers[i];
        if (s_frame_timers[i] > s_timer_max_us[i]) {
            s_timer_max_us[i] = s_frame_timers[i];
        }
    }
    s_instances_sum += active_instances > 0 ? (uint64_t)active_instances : 0;
    s_frames++;
    s_frame_started = 0;
}

uint64_t gm_perf_timer_begin(void)
{
    return s_now != NULL ? s_now() : 0;
}

void gm_perf_timer_end_scaled(gm_perf_timer_t timer, uint64_t started, uint32_t scale)
{
    uint64_t now;

    if (s_now == NULL || started == 0 || scale == 0 || timer < 0 || timer >= GM_PERF_TIMER_COUNT) {
        return;
    }
    now = s_now();
    if (now >= started) {
        s_frame_timers[timer] += (now - started) * scale;
    }
}

void gm_perf_timer_end(gm_perf_timer_t timer, uint64_t started)
{
    gm_perf_timer_end_scaled(timer, started, 1);
}

void gm_perf_count(gm_perf_counter_t counter, uint64_t amount)
{
    if (s_now != NULL && counter >= 0 && counter < GM_PERF_COUNTER_COUNT) {
        s_counters[counter] += amount;
    }
}

bool gm_perf_report_due(void)
{
    return s_now != NULL && s_frames >= s_report_frames;
}

bool gm_perf_take_report(gm_perf_report_t *out)
{
    uint64_t now;

    if (out == NULL || !gm_perf_report_due()) {
        return false;
    }
    now = s_now();
    out->frames = s_frames;
    out->wall_us = now >= s_window_started ? now - s_window_started : 0;
    out->instances_sum = s_instances_sum;
    memcpy(out->timer_us, s_timer_us, sizeof(out->timer_us));
    memcpy(out->timer_max_us, s_timer_max_us, sizeof(out->timer_max_us));
    memcpy(out->counters, s_counters, sizeof(out->counters));

    s_frames = 0;
    s_window_started = 0;
    s_instances_sum = 0;
    memset(s_timer_us, 0, sizeof(s_timer_us));
    memset(s_timer_max_us, 0, sizeof(s_timer_max_us));
    memset(s_counters, 0, sizeof(s_counters));
    return true;
}
