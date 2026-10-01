/*
 * gm_sprite - see gm_sprite.h.
 */
#include "gm_sprite.h"

static const gm_sprite_def_t *s_defs;
static int s_count;
static uint32_t s_generation;

static bool def_ok(const gm_sprite_def_t *d)
{
    if (d->colcheck != GM_COLCHECK_AABB && d->colcheck != GM_COLCHECK_PRECISE) {
        return false;
    }
    if (d->frame_count < 0 || d->mask_count < 0) {
        return false;
    }
    if (d->mask_count == 0) {
        return true;
    }
    if (d->masks == NULL || d->bbox_right < d->bbox_left || d->bbox_bottom < d->bbox_top) {
        return false;
    }
    return d->mask_count == 1 || d->mask_count == d->frame_count;
}

bool gm_sprite_registry_init(const gm_sprite_def_t *defs, int count)
{
    int i;

    s_defs = NULL;
    s_count = 0;
    s_generation++;
    if (count < 0 || count > GM_SPRITE_MAX || (count > 0 && defs == NULL)) {
        return false;
    }
    for (i = 0; i < count; ++i) {
        if (!def_ok(&defs[i])) {
            return false;
        }
    }
    s_defs = defs;
    s_count = count;
    return true;
}

int gm_sprite_count(void)
{
    return s_count;
}

bool gm_sprite_valid(int sprite_index)
{
    return sprite_index >= 0 && sprite_index < s_count;
}

const gm_sprite_def_t *gm_sprite_get(int sprite_index)
{
    return gm_sprite_valid(sprite_index) ? &s_defs[sprite_index] : NULL;
}

uint32_t gm_sprite_generation(void)
{
    return s_generation;
}

int gm_sprite_mask_stride(const gm_sprite_def_t *spr)
{
    int bwidth = spr->bbox_right - spr->bbox_left + 1;
    return bwidth > 0 ? (bwidth + 7) >> 3 : 0;
}

size_t gm_sprite_mask_size(const gm_sprite_def_t *spr)
{
    int rows = spr->bbox_bottom - spr->bbox_top + 1;
    return rows > 0 ? (size_t)gm_sprite_mask_stride(spr) * (size_t)rows : 0;
}

const uint8_t *gm_sprite_mask(const gm_sprite_def_t *spr, int n)
{
    if (n < 0 || n >= spr->mask_count) {
        return NULL;
    }
    return spr->masks + (size_t)n * gm_sprite_mask_size(spr);
}
