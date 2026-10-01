/*
 * gm_ds - GameMaker data structures used by Spelunky Classic HD.
 *
 * Only ds_map is implemented: the game never calls ds_list_* or ds_grid_*
 * (it uses ds_map for settings/keys/gamepad config and locale tables).
 * Semantics follow reference/GameMaker-HTML5/scripts/functions/collections/
 * ds_map.js, which is backed by a JS Map:
 *   - ids are small integers; we reuse the lowest free slot, whereas
 *     yyList.Alloc reuses the most recently freed one (the game never
 *     depends on id values, only on validity);
 *   - ds_map_add/set/replace all overwrite (HTML5 behaviour);
 *   - iteration (find_first / find_next) follows insertion order; replacing
 *     an existing key keeps its position.
 *
 * Keys are strings (the game only uses string keys). Values are real or
 * string (gm_value_t); nested maps/lists from json_decode are out of scope
 * until JSON support lands.
 *
 * Memory: all maps, entries and string bytes live in static pools. Nothing
 * here allocates; running out of capacity makes the mutating call fail
 * (returns false) instead of allocating.
 */
#ifndef GM_DS_H
#define GM_DS_H

#include "gm_value.h"

#include <stdbool.h>

#define GM_DS_MAP_MAX          32   /* live maps */
#define GM_DS_MAP_MAX_ENTRIES  128  /* entries per map */
#define GM_DS_MAP_STRING_BYTES 8192 /* key + string-value bytes per map (incl. NULs) */

/* Returns a new map id >= 0, or -1 when all GM_DS_MAP_MAX maps are in use. */
int gm_ds_map_create(void);
void gm_ds_map_destroy(int id);
bool gm_ds_map_is_valid(int id); /* ds_exists(id, ds_type_map) */
void gm_ds_map_clear(int id);
int gm_ds_map_size(int id);      /* 0 for invalid ids */

bool gm_ds_map_exists(int id, const char *key);

/* ds_map_replace / ds_map_set / ds_map_add. Copies key and string value into
 * the map. Returns false on invalid id, NULL key or exhausted capacity (the
 * map is left unchanged in that case). */
bool gm_ds_map_replace(int id, const char *key, gm_value_t value);

void gm_ds_map_delete(int id, const char *key);

/* Returned strings (values and keys) point into the map's storage and stay
 * valid until the next mutation of that map. Missing -> undefined. */
gm_value_t gm_ds_map_find_value(int id, const char *key);
gm_value_t gm_ds_map_find_first(int id);
gm_value_t gm_ds_map_find_next(int id, const char *key);

/* Destroys every map (game restart, test isolation). */
void gm_ds_reset(void);

#endif /* GM_DS_H */
