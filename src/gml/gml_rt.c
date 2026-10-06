/*
 * gml_rt - see gml_rt.h.
 */
#include "gml_rt.h"

#include "gm_perf.h"

#include <string.h>

/* ------------------------------------------------------------------ values */

int gml_case(gm_value_t subject, gm_value_t label)
{
    if (subject.kind != label.kind) {
        return 0;
    }
    switch (subject.kind) {
    case GM_VALUE_REAL:
        return gm_compare_real(subject.real, label.real) == 0;
    case GM_VALUE_STRING:
        return strcmp(subject.str, label.str) == 0;
    case GM_VALUE_ARRAY:
        return subject.ref == label.ref;
    case GM_VALUE_UNDEFINED:
    default:
        return 1;
    }
}

int gml_str_cmp(const char *a, const char *b)
{
    int c = strcmp(gml_s(a), gml_s(b));
    return c == 0 ? 0 : (c > 0 ? 1 : -1);
}

float gml_str_real(const char *s)
{
    return gm_value_to_real(gm_value_string(s));
}

int gml_str_truthy(const char *s)
{
    return gm_value_to_bool(gm_value_string(s));
}

float gml_str_to_real(const char *s)
{
    return gm_string_parse_real(gml_s(s), NULL);
}

const char *gml_real_str(float d)
{
    return gm_value_to_string(gm_value_real(d)).str;
}

const char *gml_as_str(gm_value_t v)
{
    if (v.kind == GM_VALUE_STRING) {
        return gml_s(v.str);
    }
    return gml_s(gm_value_to_string(v).str);
}

gm_value_t gml_add(gm_value_t a, gm_value_t b)
{
    if (a.kind == GM_VALUE_STRING && b.kind == GM_VALUE_STRING) {
        return gm_heap_concat(a.str, b.str);
    }
    /* A string plus a non-string is a runner error; the reference then falls
     * through to real addition (Function_Maths.js L1853-1856). */
    return gm_value_real(gm_value_to_real(a) + gm_value_to_real(b));
}

float gml_div(float a, float b)
{
    int32_t ia, ib;

    if (isnan(a) || isnan(b)) {
        if (isnan(a) && isnan(b)) {
            return 1.0f;
        }
        if (isnan(a)) {
            return isfinite(b) ? a : 1.0f;
        }
        return isfinite(a) ? b : 1.0f;
    }
    if (a == b) {
        return 1.0f;
    }
    ia = gm_to_int32(a);
    ib = gm_to_int32(b);
    if (ib == 0) {
        return 0.0f; /* ~~(x / 0) == 0 after the runner's divide-by-zero error */
    }
    return (float)gm_to_int32((float)ia / (float)ib);
}

/* yyfbitshiftleft/right: the magnitude goes through `new Long(v)` (whose low
 * word is ToInt32(v)), is shifted as a 64-bit value and re-negated. */
static float shift(float a, float n, int left)
{
    int32_t s = gm_to_int32(n);
    int neg = a < 0.0f;
    int64_t v = (int64_t)(uint32_t)gm_to_int32(neg ? -a : a);
    int64_t r;

    if (s >= 64) {
        r = 0;
    } else {
        unsigned k = (unsigned)s & 63u;
        r = left ? (int64_t)((uint64_t)v << k) : (v >> k);
    }
    return neg ? -(float)r : (float)r;
}

float gml_shl(float a, float n)
{
    return shift(a, n, 1);
}

float gml_shr(float a, float n)
{
    return shift(a, n, 0);
}

float gml_to_real(gm_value_t v)
{
    switch (v.kind) {
    case GM_VALUE_REAL:
        return v.real;
    case GM_VALUE_STRING:
        return gm_string_parse_real(v.str, NULL);
    case GM_VALUE_UNDEFINED:
    case GM_VALUE_ARRAY:
    default:
        return 0.0f; /* runner error */
    }
}

const char *gml_keep_str(const char *s, char *buf, size_t size)
{
    size_t n;

    if (size == 0) {
        return "";
    }
    s = gml_s(s);
    n = strlen(s);
    if (n >= size) {
        n = size - 1;
    }
    memmove(buf, s, n);
    buf[n] = '\0';
    return buf;
}

/* ------------------------------------------------------------------ typed ds_map reads */

float gml_ds_map_find_real(float map, const char *key)
{
    return gm_value_to_real(gm_ds_map_find_value(gml_target(map), gml_s(key)));
}

const char *gml_ds_map_find_str(float map, const char *key)
{
    gm_value_t v = gm_ds_map_find_value(gml_target(map), gml_s(key));

    /* Map storage changes on the next mutation: detach into gm_heap. */
    return v.kind == GM_VALUE_STRING ? gm_heap_str(v.str) : gml_as_str(v);
}

float gml_ds_map_missing(float map, const char *key)
{
    return gm_ds_map_find_value(gml_target(map), gml_s(key)).kind == GM_VALUE_UNDEFINED ? 1.0f : 0.0f;
}

/* ------------------------------------------------------------------ instances */

static gm_value_t s_vvar_scratch;

gm_instance_t *gml_deref(float target, gm_instance_t *self, gm_instance_t *other)
{
    int t = gml_target(target);
    gm_instance_cursor_t c;
    gm_instance_t *p;
    gm_instance_t *first_child = NULL;

    if (t == GM_SELF || t == GM_ALL) {
        return self;
    }
    if (t == GM_OTHER) {
        return other;
    }
    if (t >= GM_INSTANCE_ID_BASE) {
        return gm_instance_find_by_id(t);
    }
    if (!gm_object_valid(t)) {
        return NULL;
    }
    /* pObject.Instances.Get(0): prefer the object's own instances. An abstract
     * parent (e.g. `oCharacter.x`, which oJar/oSkull rely on while held) has
     * none, so fall back to its first descendant as GM8/desktop runners do;
     * returning NULL there reads every field as 0. */
    gm_instance_search_begin(&c, t);
    while ((p = gm_instance_search_next(&c)) != NULL) {
        if (p->object_index == t) {
            return p;
        }
        if (first_child == NULL) {
            first_child = p;
        }
    }
    return first_child;
}

gm_value_t *gml_vvar_ref(gm_instance_t *p, int i)
{
    if (p == NULL) {
        s_vvar_scratch = gm_value_undefined();
        return &s_vvar_scratch;
    }
    return &p->vals[i];
}

/* ------------------------------------------------------------------ arrays */

gm_value_t gml_array_lit(int n, const gm_value_t *items)
{
    gm_value_t a = gm_array_new(n);
    int i;

    for (i = 0; i < n && gm_array_valid(a); ++i) {
        gm_array_set(&a, i, items[i]);
    }
    return a;
}

gm_value_t gml_struct_get(gm_value_t s, gm_value_t key)
{
    (void)s;
    (void)key;
    gml_pending_hit("struct accessor [$");
    return gm_value_undefined();
}

void gml_struct_set(gm_value_t s, gm_value_t key, gm_value_t v)
{
    (void)s;
    (void)key;
    (void)v;
    gml_pending_hit("struct accessor [$");
}

/* ------------------------------------------------------------------ events and scripts */

#define EVENT_STACK_MAX 64

typedef struct event_frame {
    int object_index;
    int type;
    int number;
} event_frame_t;

static const gml_game_t *s_game;
static event_frame_t s_events[EVENT_STACK_MAX];
static int s_event_depth;
static int s_event_overflow; /* pushes beyond EVENT_STACK_MAX (not recorded) */

static void build_dispatch_cache(void);

void gml_rt_install(const gml_game_t *game)
{
    s_game = game;
    build_dispatch_cache();
}

const gml_game_t *gml_rt_game(void)
{
    return s_game;
}

void gml_rt_global_init(void)
{
    if (s_game != NULL && s_game->global_init != NULL) {
        s_game->global_init();
    }
}

void gml_event_push(gm_instance_t *self, gm_instance_t *other, int object_index, int type, int number)
{
    gm_ctx_push(self, other);
    if (s_event_depth >= EVENT_STACK_MAX) {
        s_event_overflow++;
        return;
    }
    s_events[s_event_depth].object_index = object_index;
    s_events[s_event_depth].type = type;
    s_events[s_event_depth].number = number;
    s_event_depth++;
}

void gml_event_pop(void)
{
    if (s_event_overflow > 0) {
        s_event_overflow--;
    } else if (s_event_depth > 0) {
        s_event_depth--;
    }
    gm_ctx_pop();
}

static int entry_cmp(const gml_event_entry_t *e, int object_index, int type, int number)
{
    if (e->object_index != object_index) {
        return e->object_index < object_index ? -1 : 1;
    }
    if (e->type != type) {
        return e->type < type ? -1 : 1;
    }
    if (e->number != number) {
        return e->number < number ? -1 : 1;
    }
    return 0;
}

static gml_event_fn find_own_event(int object_index, int type, int number)
{
    int lo = 0, hi;

    if (s_game == NULL) {
        return NULL;
    }
    hi = s_game->event_count - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        int c = entry_cmp(&s_game->events[mid], object_index, type, number);
        if (c == 0) {
            return s_game->events[mid].fn;
        }
        if (c < 0) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return NULL;
}

static int parent_of(int object_index)
{
    if (s_game == NULL || object_index < 0 || object_index >= s_game->object_count) {
        return -1;
    }
    return s_game->object_parents[object_index];
}

void gml_event_inherited(gm_instance_t *self, gm_instance_t *other)
{
    event_frame_t cur;
    int o, guard;

    if (s_event_depth == 0 || s_event_overflow > 0) {
        return;
    }
    cur = s_events[s_event_depth - 1];
    if (gm_room_event_blocked(self, cur.type)) {
        return; /* the parent's PerformEvent is gated too (yyObject.js L702) */
    }
    o = parent_of(cur.object_index);
    for (guard = 0; o >= 0 && guard < GM_OBJECT_MAX; ++guard) {
        gml_event_fn fn = find_own_event(o, cur.type, cur.number);
        if (fn != NULL) {
            fn(self, other); /* the generated handler pushes its own frame */
            return;
        }
        o = parent_of(o);
    }
}

static gml_event_fn find_event_uncached(int object_index, int type, int number)
{
    int o = object_index, guard;

    for (guard = 0; o >= 0 && guard < GM_OBJECT_MAX; ++guard) {
        gml_event_fn fn = find_own_event(o, type, number);
        if (fn != NULL) {
            return fn;
        }
        o = parent_of(o);
    }
    return NULL;
}

/* ---- dispatch cache ----------------------------------------------------------------
 * The game loop asks every instance for its step, alarm, draw, other and key handlers
 * every frame; most objects (blocks) define none of them. Built once at install from
 * the static tables:
 *  - s_type_mask[o]: bit t set if o or an ancestor defines any event of type t, so
 *    absent types answer NULL without searching;
 *  - s_dense[o][slot]: the inherited handler for the events the loop dispatches per
 *    instance per frame (step 0-2, alarm 0-11, the eight draw numbers). */

#define DENSE_STEP 0
#define DENSE_ALARM (DENSE_STEP + 3)
#define DENSE_DRAW (DENSE_ALARM + GM_ALARM_COUNT)
#define DENSE_COUNT (DENSE_DRAW + 8)

static const int s_draw_numbers[8] = { GM_EV_DRAW_NORMAL, GM_EV_DRAW_GUI, GM_EV_DRAW_BEGIN, GM_EV_DRAW_END,
                                       GM_EV_DRAW_GUI_BEGIN, GM_EV_DRAW_GUI_END, GM_EV_DRAW_PRE,
                                       GM_EV_DRAW_POST };

static uint32_t s_type_mask[GM_OBJECT_MAX];
static gml_event_fn s_dense[GM_OBJECT_MAX][DENSE_COUNT];
static bool s_cache_ready;

static inline int dense_slot(int type, int number)
{
    switch (type) {
    case GM_EV_TYPE_STEP:
        return (number >= 0 && number < 3) ? DENSE_STEP + number : -1;
    case GM_EV_TYPE_ALARM:
        return (number >= 0 && number < GM_ALARM_COUNT) ? DENSE_ALARM + number : -1;
    case GM_EV_TYPE_DRAW:
        if (number == 0) {
            return DENSE_DRAW;
        }
        if (number >= 72 && number <= 77) {
            return DENSE_DRAW + 2 + (number - 72);
        }
        if (number == 64) {
            return DENSE_DRAW + 1;
        }
        return -1;
    default:
        return -1;
    }
}

static void build_dispatch_cache(void)
{
    static uint32_t own[GM_OBJECT_MAX];
    int o, e, n, slot;

    s_cache_ready = false;
    if (s_game == NULL || s_game->object_count < 0 || s_game->object_count > GM_OBJECT_MAX) {
        return;
    }
    n = s_game->object_count;
    memset(own, 0, sizeof(own));
    for (e = 0; e < s_game->event_count; ++e) {
        const gml_event_entry_t *ev = &s_game->events[e];
        if (ev->object_index >= 0 && ev->object_index < n) {
            /* Types beyond the mask width always take the slow path. */
            own[ev->object_index] |= (ev->type >= 0 && ev->type < 32) ? (1u << ev->type) : 0xFFFFFFFFu;
        }
    }
    for (o = 0; o < n; ++o) {
        uint32_t mask = 0;
        int p = o, guard;
        for (guard = 0; p >= 0 && p < n && guard < GM_OBJECT_MAX; ++guard) {
            mask |= own[p];
            p = parent_of(p);
        }
        s_type_mask[o] = mask;
        for (slot = 0; slot < 3; ++slot) {
            s_dense[o][DENSE_STEP + slot] = find_event_uncached(o, GM_EV_TYPE_STEP, slot);
        }
        for (slot = 0; slot < GM_ALARM_COUNT; ++slot) {
            s_dense[o][DENSE_ALARM + slot] = find_event_uncached(o, GM_EV_TYPE_ALARM, slot);
        }
        for (slot = 0; slot < 8; ++slot) {
            s_dense[o][DENSE_DRAW + slot] = find_event_uncached(o, GM_EV_TYPE_DRAW, s_draw_numbers[slot]);
        }
    }
    s_cache_ready = true;
}

gml_event_fn gml_find_event(int object_index, int type, int number)
{
    int slot;

    if (!s_cache_ready || object_index < 0 || object_index >= s_game->object_count) {
        return find_event_uncached(object_index, type, number);
    }
    if (type >= 0 && type < 32 && (s_type_mask[object_index] & (1u << type)) == 0) {
        return NULL;
    }
    slot = dense_slot(type, number);
    if (slot >= 0) {
        return s_dense[object_index][slot];
    }
    return find_event_uncached(object_index, type, number);
}

gml_event_fn gml_find_own_event(int object_index, int type, int number)
{
    return find_own_event(object_index, type, number);
}

void gml_perform_event(gm_instance_t *inst, int type, int number)
{
    gml_event_fn fn = inst != NULL ? gml_find_event(inst->object_index, type, number) : NULL;

    if (fn != NULL && !gm_room_event_blocked(inst, type)) {
        fn(inst, inst);
    }
}

gm_value_t gml_script_execute(gm_instance_t *self, gm_instance_t *other, float script,
                              int argc, const gm_value_t *argv)
{
    int index = gml_target(script);

    if (s_game == NULL || index < 0 || index >= s_game->script_count || s_game->scripts[index] == NULL) {
        gml_pending_hit("script_execute(<invalid script>)");
        return gm_value_undefined();
    }
    return s_game->scripts[index](self, other, argc, argv);
}

/* ------------------------------------------------------------------ pending */

#define PENDING_MAX 512

static const char *s_pending[PENDING_MAX];
static int s_pending_count;

void gml_pending_hit(const char *name)
{
    int i;

    for (i = 0; i < s_pending_count; ++i) {
        if (s_pending[i] == name || strcmp(s_pending[i], name) == 0) {
            return;
        }
    }
    if (s_pending_count < PENDING_MAX) {
        s_pending[s_pending_count++] = name;
    }
}

int gml_pending_count(void)
{
    return s_pending_count;
}

const char *gml_pending_name(int index)
{
    return (index >= 0 && index < s_pending_count) ? s_pending[index] : NULL;
}

void gml_pending_reset(void)
{
    s_pending_count = 0;
}

/* ------------------------------------------------------------------ collection */

static void visit_roots(gm_heap_visit_fn visit)
{
    int slot, i;

    for (slot = 0; slot < GM_INSTANCE_MAX; ++slot) {
        gm_instance_t *p = gm_instance_at_slot(slot);
        if (p == NULL) {
            continue;
        }
        for (i = 0; i < GM_SVAR_COUNT; ++i) {
            gm_heap_visit_str(&p->strs[i]);
        }
        for (i = 0; i < GM_VVAR_COUNT; ++i) {
            visit(&p->vals[i]);
        }
    }
    if (s_game != NULL && s_game->visit_roots != NULL) {
        s_game->visit_roots(visit);
    }
}

void gml_collect_garbage(void)
{
    if (gm_heap_used() > (GM_HEAP_BYTES * 3) / 4) {
        uint64_t perf_started = gm_perf_timer_begin();

        gm_perf_count(GM_PERF_GC_RUNS, 1);
        gm_heap_collect(visit_roots);
        gm_perf_timer_end(GM_PERF_TIMER_GC, perf_started);
    }
}

void gml_collect_garbage_force(void)
{
    gm_heap_collect(visit_roots);
}

void gml_room_begin(int room)
{
    (void)room;
    gm_heap_collect(visit_roots);
}

void gml_rt_reset(void)
{
    s_event_depth = 0;
    s_event_overflow = 0;
    s_pending_count = 0;
    s_game = NULL;
    gm_with_reset();
}
