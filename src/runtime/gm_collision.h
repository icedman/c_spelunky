/*
 * gm_collision - bounding boxes, collision primitives, collision queries and
 * a spatial grid for the heavily queried static object trees (oSolid).
 *
 * Semantics are ported from the GameMaker-HTML5 runner with collision
 * compatibility mode OFF (options_main.yy: option_collision_compatibility
 * false):
 *  - bounding boxes: yyInstance.Compute_BoundingBox (yyInstance.js L1455)
 *  - instance tests: yyInstance.Collision_Point / _Rectangle / _Line /
 *    _Instance (L1730, L1807, L2053, L2539)
 *  - precise tests: yySprite.PreciseCollisionPoint / _Rectangle / _Line /
 *    PreciseCollision and ColMaskSet (yySprite.js L2126-2801, L1626)
 *  - queries: Function_Collision.js, Function_Movement.js (place_meeting,
 *    instance_place, distance_to_*), Function_Instance.js
 *    (instance_position), iterating in Instance_SearchLoop order
 *    (Globals.js L1464).
 *
 * Deliberate omissions / deviations (Spelunky never needs them):
 *  - ROTATED_RECT sprites (rejected by gm_sprite_registry_init), spine,
 *    nine-slice, tilemap targets, array targets, ellipse/circle queries.
 *  - Collision domains (GUI layers) do not exist: every instance is in the
 *    room domain. This also sidesteps the runner bug in
 *    Command_CollisionRectangle (Function_Collision.js L235 compares against
 *    the GetCollisionDomain function object, so collision_rectangle would
 *    always return noone); we implement the intended behaviour.
 *  - self/other/noone as targets find nothing, like the runner's
 *    Instance_SearchLoop. The transpiler passes object indices or ids.
 *
 * Bounding-box invalidation: the runner marks bbox_dirty from its property
 * setters. Here the bbox is cached against its inputs (x, y, sprite_index,
 * mask_index, image_xscale, image_yscale, image_angle) and recomputed on use
 * whenever any of them changed, so plain field writes are always safe for
 * the collision tests themselves. Instances of grid-indexed objects must
 * additionally be re-bucketed after moving: generated code calls
 * gm_collision_touch() after writing a bbox input, and the game loop calls
 * gm_collision_grid_sync() between event phases as a safety net.
 *
 * Memory: all state is static (grid, per-slot caches); no allocation.
 */
#ifndef GM_COLLISION_H
#define GM_COLLISION_H

#include "gm_instance.h"
#include "gm_sprite.h"

#include <stdbool.h>

/* Grid limits. Cells are square; the cell size grows (doubling) until the
 * room fits in GM_GRID_MAX_COLS x GM_GRID_MAX_ROWS. Coordinates outside the
 * room clamp to the border cells, so out-of-room instances are still found. */
#define GM_GRID_MAX_COLS 128
#define GM_GRID_MAX_ROWS 128
#define GM_GRID_MAX_ROOTS 8

/* Instances spanning more cells than this live in an "always test" list. */
#define GM_GRID_CELLS_PER_INSTANCE 4

/* Queries spanning more cells than this walk the object list instead. */
#define GM_GRID_MAX_QUERY_CELLS 64

/* distance_to_object's result when no instance matches (Globals.js L1554). */
#define GM_COLLISION_NO_DISTANCE 10000000000.0f

/* ---- Setup ------------------------------------------------------------------ */

/* Installs the gm_instance hooks and clears grid configuration and caches.
 * Call after gm_object_registry_init and gm_sprite_registry_init. */
void gm_collision_init(void);

/* Removes the hooks and clears all state. */
void gm_collision_shutdown(void);

/* ---- Bounding boxes ------------------------------------------------------------ */

/* Brings inst->bbox_left/top/right/bottom up to date (Maybe_Compute_BoundingBox)
 * and returns the collision type in effect (gm_colcheck_t). */
int gm_collision_update_bbox(gm_instance_t *inst);

/* ---- Instance-level primitives (the yyInstance.Collision_* methods) ------------- */

bool gm_collision_test_point(gm_instance_t *inst, float x, float y, bool prec);
bool gm_collision_test_rectangle(gm_instance_t *inst, float x1, float y1,
                                 float x2, float y2, bool prec);
bool gm_collision_test_line(gm_instance_t *inst, float x1, float y1,
                            float x2, float y2, bool prec);
/* inst.Collision_Instance(other, prec): order matters for precise tests. */
bool gm_collision_test_instance(gm_instance_t *inst, gm_instance_t *other, bool prec);

/* ---- GML queries ----------------------------------------------------------------
 * Instance-returning queries yield the first hit in Instance_SearchLoop order,
 * or NULL for noone. */

gm_instance_t *gm_collision_point(gm_instance_t *self, float x, float y,
                                  int target, bool prec, bool notme);
gm_instance_t *gm_collision_rectangle(gm_instance_t *self, float x1, float y1,
                                      float x2, float y2, int target,
                                      bool prec, bool notme);
gm_instance_t *gm_collision_line(gm_instance_t *self, float x1, float y1,
                                 float x2, float y2, int target,
                                 bool prec, bool notme);

/* instance_place / place_meeting: self placed at (x, y), precise. */
gm_instance_t *gm_collision_instance_place(gm_instance_t *self, float x, float y, int target);
bool gm_collision_place_meeting(gm_instance_t *self, float x, float y, int target);

/* instance_position / position_meeting: precise point test. */
gm_instance_t *gm_collision_instance_position(float x, float y, int target);
bool gm_collision_position_meeting(float x, float y, int target);

void gm_move_snap(gm_instance_t *inst, float hsnap, float vsnap);

/* distance_to_object: bbox gap to the nearest match (self included, as in
 * the runner); GM_COLLISION_NO_DISTANCE when nothing matches. */
float gm_collision_distance_to_object(gm_instance_t *self, int target);
float gm_collision_distance_to_point(gm_instance_t *self, float x, float y);

/* ---- Spatial grid -------------------------------------------------------------------- */

/* Sizes the grid for a room and re-buckets indexed instances. */
void gm_collision_grid_configure(float room_width, float room_height, float cell_size);

/* Indexes `object_index` and all its descendants. Queries whose target is an
 * indexed object (or a descendant of one) use the grid. Returns false if the
 * object is invalid or GM_GRID_MAX_ROOTS roots are already registered. */
bool gm_collision_grid_index_object(int object_index);

bool gm_collision_grid_is_indexed(int object_index);

/* Finds oSolid in the object registry and registers it as a grid root. */
void gm_collision_index_default_roots(void);

/* Re-buckets `inst` if it is grid-indexed and its bbox changed. Cheap no-op
 * for other instances. Call after writing a bbox input. */
void gm_collision_touch(gm_instance_t *inst);

/* Re-buckets every indexed instance whose bbox changed. */
void gm_collision_grid_sync(void);

/* Verification mode: every grid query is repeated as a linear walk and
 * mismatches are counted (for tests and parity debugging). */
void gm_collision_set_verify(bool enabled);
long gm_collision_verify_failures(void);

/* Diagnostics */
float gm_collision_grid_cell_size(void);
int gm_collision_grid_indexed_count(void); /* indexed instances in cells + oversize list */

#endif /* GM_COLLISION_H */
