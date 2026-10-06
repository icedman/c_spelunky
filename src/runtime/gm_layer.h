/*
 * gm_layer - room layers and their tile/background elements (Function_Layers.js).
 *
 * Layers are gameplay state in Spelunky: the level generator adds tiles through the
 * tile_add compat script (layer_get_all -> layer_create -> layer_tile_create) and
 * oBoulder/oExplosion find and delete them (tile_layer_find / tile_delete). Semantics:
 *
 *  - The room's layer list is ordered by depth ascending; a new layer goes after the
 *    layers of equal depth (yyOList.Add, yyOList.js L57). Room start adds the storage
 *    layers last-to-first (BuildRoomLayers, Function_Layers.js L1879).
 *  - Storage layers keep their packed ids; layers created at run time get ids above the
 *    highest storage id, counting up for the whole game (GetNextLayerID, L1494).
 *  - layer_get_all lists non-dynamic layers in list order (L4880); layer_create makes a
 *    non-dynamic layer named "_layer_<hex id>" unless named (L2394).
 *  - Element ids come from one counter that is never reset (GetNextElementID, L1501).
 *    A new element goes to the *front* of its layer's list (AddNewElement inserts at 0:
 *    its instance-skipping loop tests an undefined variable, L892), so
 *    layer_get_all_elements lists the newest first and a room's storage tiles (added
 *    last-to-first) in .yy order.
 *  - layer_depth moves a layer to its new place in the list without merging (L4936).
 *
 * Deviations (agents/HANDOFF_SLICE8.md section 5, Q4): instances are not layer
 * elements. They keep their depth (room instances take their layer's depth at room
 * start), which is all draw order and Spelunky's queries need; no dynamic layers are
 * created for instance depths and instance elements take no element ids.
 *
 * Memory: static pools (GM_LAYER_MAX layers, GM_ELEMENT_MAX elements); creating beyond
 * them fails soft (returns -1) and is counted by gm_layer_overflows().
 */
#ifndef GM_LAYER_H
#define GM_LAYER_H

#include "gm_room.h"

#include <stdbool.h>
#include <stdint.h>

#ifndef GM_LAYER_MAX
#define GM_LAYER_MAX 64
#endif
/* Largest room (rOlmec 672x880) is 42x55 = 2310 cells of 16 px. */
#ifndef GM_ELEMENT_MAX
#define GM_ELEMENT_MAX 4096
#endif
#define GM_LAYER_NAME_MAX 64

/* eLayerElementType (Function_Layers.js L27). */
#define GM_ELEMENT_BACKGROUND 1
#define GM_ELEMENT_TILE 7

typedef struct gm_layer {
    int id;
    int depth;
    int kind;               /* gm_layer_kind_t; GM_LAYER_ASSETS for layers made at run time */
    bool dynamic;
    bool visible;
    float x, y, hspeed, vspeed;
    char name[GM_LAYER_NAME_MAX];
    int head, tail, count;  /* element slots, front to back */
} gm_layer_t;

typedef struct gm_element {
    int id;
    int type;               /* GM_ELEMENT_* */
    int layer;              /* layer slot */
    int prev, next;         /* element slots in the layer's list, -1 at the ends */
    bool visible;
    int sprite;
    float x, y;            /* tiles */
    int xo, yo, w, h;       /* tiles: region of the sprite */
    float xscale, yscale;
    uint32_t blend;         /* GM colour (BGR) */
    float alpha;
    bool htiled, vtiled, stretch;     /* backgrounds */
    float image_index, image_speed;  /* backgrounds */
} gm_element_t;

/* Clears everything, including the id counters. */
void gm_layer_reset(void);

/* Highest storage layer id of the whole game (LayerManager watermark). */
void gm_layer_set_watermark(int id);

/* Room switching (called by gm_room): build the room's storage layers / remove all. */
void gm_layer_room_start(const gm_room_def_t *room);
void gm_layer_room_end(void);

/* Layers of the current room in list order. */
int gm_layer_count(void);
gm_layer_t *gm_layer_at(int index);
gm_layer_t *gm_layer_find(int id);
gm_layer_t *gm_layer_find_name(const char *name);   /* case-insensitive */

/* layer_get_all: ids of non-dynamic layers. Returns the count written (<= max). */
int gm_layer_get_all(int *out, int max);

int gm_layer_create(int depth, const char *name);   /* NULL name: "_layer_<hex id>" */
void gm_layer_destroy(int id);
void gm_layer_set_depth(int id, int depth);

/* layer_get_all_elements: element ids, newest first. Returns the count written. */
int gm_layer_elements(int layer_id, int *out, int max);

gm_element_t *gm_layer_element(int element_id);

/* Element in pool slot `slot` (gm_layer_t.head / gm_element_t.next walk), or NULL. */
gm_element_t *gm_layer_element_slot(int slot);
int gm_layer_element_type(int element_id);          /* -1 if none */
void gm_layer_element_destroy(int element_id);

int gm_layer_tile_create(int layer_id, float x, float y, int sprite, int left, int top, int w, int h);
int gm_layer_background_create(int layer_id, int sprite);
bool gm_layer_background_exists(int layer_id, int element_id);

/* Per-frame scrolling (UpdateLayers, Function_Layers.js L1663): x += hspeed ... */
void gm_layer_update(void);

long gm_layer_overflows(void);

#endif /* GM_LAYER_H */
