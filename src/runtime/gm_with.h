/*
 * gm_with - the `with` iterator and the current-instance context.
 *
 * with (target) { body } runs `body` once per instance of the GetWithArray
 * set (yyObject.js L1599), snapshotted when the statement starts: instances
 * created by the body are not visited, instances destroyed or deactivated
 * before their turn are skipped. Inside the body `self` is the visited
 * instance and `other` the instance that ran the `with`. Transpiled code
 * looks like:
 *
 *     gm_with_begin(target, self, other);
 *     while ((self2 = gm_with_next()) != NULL) { ... }
 *     gm_with_end();
 *
 * and a function that leaves a with body early (return / exit) first calls
 * gm_with_unwind() with the gm_with_depth() it saw on entry.
 *
 * Context: events push (self, other) with gm_ctx_push/gm_ctx_pop, so runtime
 * code that is not handed the instances (built-ins) can still ask for the
 * innermost self/other with gm_ctx_self/gm_ctx_other - whichever of the
 * running event and the running with body started last.
 *
 * Memory: static storage only. Snapshots share one slot pool (each with
 * frame is a base offset + count into it), so nesting costs no C stack.
 * Exceeding GM_WITH_MAX_DEPTH frames or GM_WITH_SLOT_POOL slots fails soft:
 * that with body runs zero times and gm_with_overflows() counts it. Context
 * pushes beyond GM_CTX_MAX_DEPTH are counted too (the innermost stored
 * context is reported meanwhile).
 */
#ifndef GM_WITH_H
#define GM_WITH_H

#include "gm_instance.h"

#ifndef GM_WITH_MAX_DEPTH
#define GM_WITH_MAX_DEPTH 32
#endif
#ifndef GM_WITH_SLOT_POOL
#define GM_WITH_SLOT_POOL (4 * GM_INSTANCE_MAX)
#endif
#ifndef GM_CTX_MAX_DEPTH
#define GM_CTX_MAX_DEPTH 64
#endif

/* ---- context ------------------------------------------------------------------ */

void gm_ctx_push(gm_instance_t *self, gm_instance_t *other);
void gm_ctx_pop(void);
gm_instance_t *gm_ctx_self(void);  /* NULL outside any event / with */
gm_instance_t *gm_ctx_other(void);

/* ---- with ---------------------------------------------------------------------- */

/* `target` is an instance id, object index, GM_ALL, GM_SELF, GM_OTHER or
 * GM_NOONE; self/other resolve GM_SELF/GM_OTHER and self becomes the body's
 * `other`. Every begin must be matched by gm_with_end (or an unwind). */
void gm_with_begin(int target, gm_instance_t *self, gm_instance_t *other);

/* Next still-usable snapshot instance (now the context's self), or NULL. */
gm_instance_t *gm_with_next(void);

void gm_with_end(void);

/* Open with statements, and closing them down to `depth` (early exits). */
int gm_with_depth(void);
void gm_with_unwind(int depth);

/* with statements skipped for lack of capacity, plus context overflows. */
long gm_with_overflows(void);

/* Forgets every frame and context (game restart, test isolation). */
void gm_with_reset(void);

#endif /* GM_WITH_H */
