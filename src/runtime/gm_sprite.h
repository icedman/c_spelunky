/*
 * gm_sprite - static sprite definitions: the subset of yySprite that the
 * collision engine needs (yySprite.js L117-162, CreateSpriteFromStorage
 * L1265). Frames/texture data for drawing belong to the platform layer.
 *
 * The generated asset tables (slice 8) provide the definitions; the runtime
 * only borrows them (no copies, no allocation).
 *
 * Collision masks use the runner's decompressed layout (ColMaskSet,
 * yySprite.js L1626): one bit per pixel of the sprite bounding box, rows of
 * ((bbox_right - bbox_left + 1 + 7) >> 3) bytes, most significant bit first,
 * bbox_bottom - bbox_top + 1 rows per mask. Masks are stored back to back.
 */
#ifndef GM_SPRITE_H
#define GM_SPRITE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GM_SPRITE_NONE (-1)

/* Upper bound on the registry size. Spelunky Classic HD has 860 sprites. */
#define GM_SPRITE_MAX 2048

/* yySprite_CollisionType (yySprite.js L41). */
typedef enum gm_colcheck {
    GM_COLCHECK_AABB = 0,        /* bounding box */
    GM_COLCHECK_PRECISE = 1,     /* per-pixel mask */
    GM_COLCHECK_ROTATED_RECT = 2 /* not used by Spelunky; rejected by init */
} gm_colcheck_t;

typedef struct gm_sprite_def {
    const char *name;
    int width, height;   /* subimage size */
    int xorigin, yorigin;
    /* Inclusive pixel bounds of the collision box (sprite-local). */
    int bbox_left, bbox_top, bbox_right, bbox_bottom;
    int colcheck;        /* gm_colcheck_t */
    int frame_count;     /* numb: sprites without frames never collide */
    /* 0 = no masks (the runner's maskcreated == false: precise tests pass),
     * 1 = one mask shared by all frames, frame_count = one mask per frame. */
    int mask_count;
    const uint8_t *masks;
} gm_sprite_def_t;

/* Installs the definition table (borrowed, must outlive its use). Rejects
 * unsupported collision kinds and inconsistent mask data; on failure the
 * registry is left empty and false is returned. */
bool gm_sprite_registry_init(const gm_sprite_def_t *defs, int count);

int gm_sprite_count(void);
bool gm_sprite_valid(int sprite_index);

/* NULL for invalid indices. */
const gm_sprite_def_t *gm_sprite_get(int sprite_index);

/* Bumped by every gm_sprite_registry_init; lets caches detect a new table. */
uint32_t gm_sprite_generation(void);

/* Bytes per mask row and per mask (0 when the bbox is empty). */
int gm_sprite_mask_stride(const gm_sprite_def_t *spr);
size_t gm_sprite_mask_size(const gm_sprite_def_t *spr);

/* Mask for (already wrapped) mask number `n`, or NULL when out of range. */
const uint8_t *gm_sprite_mask(const gm_sprite_def_t *spr, int n);

#endif /* GM_SPRITE_H */
