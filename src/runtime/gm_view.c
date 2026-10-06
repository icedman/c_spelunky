/*
 * gm_view - see gm_view.h.
 */
#include "gm_view.h"

#include <math.h>
#include <string.h>

typedef struct view_store {
    gm_room_view_def_t def;
    int camera;             /* storage cameraID, -1 = none */
} view_store_t;

/* A current view (yyView). */
typedef struct view {
    bool visible;
    float worldx, worldy, worldw, worldh;
    float portx, porty, portw, porth;
    float angle;
    float hborder, vborder, hspeed, vspeed;
    float objid;
    float surface_id;
    float camera;
} view_t;

static view_store_t s_store[GM_ROOM_MAX][GM_VIEW_COUNT];
static int s_store_count;

static gm_camera_t s_cams[GM_CAMERA_MAX];
static bool s_cam_used[GM_CAMERA_MAX];
static int s_next_camera_id;
static long s_overflows;

static view_t s_views[GM_VIEW_COUNT];
static float s_arrays[GM_VIEW_FIELD_COUNT][GM_VIEW_COUNT];
static bool s_enabled;
static bool s_in_room;
static int s_current;   /* view_current */

/* ---- cameras --------------------------------------------------------------------------- */

static gm_camera_t *camera_new(void)
{
    int i;

    for (i = 0; i < GM_CAMERA_MAX; ++i) {
        if (!s_cam_used[i]) {
            gm_camera_t *c = &s_cams[i];
            s_cam_used[i] = true;
            memset(c, 0, sizeof(*c));
            c->id = s_next_camera_id++;
            c->target = -1;
            return c;
        }
    }
    s_overflows++;
    return NULL;
}

gm_camera_t *gm_camera_get(int id)
{
    int i;

    for (i = 0; i < GM_CAMERA_MAX; ++i) {
        if (s_cam_used[i] && s_cams[i].id == id) {
            return &s_cams[i];
        }
    }
    return NULL;
}

int gm_camera_count(void)
{
    int i, n = 0;

    for (i = 0; i < GM_CAMERA_MAX; ++i) {
        n += s_cam_used[i] ? 1 : 0;
    }
    return n;
}

/* camera_create_view (CameraManager.js L708). */
int gm_camera_create_view(float x, float y, float w, float h, float angle, int target,
                          float speed_x, float speed_y, float border_x, float border_y)
{
    gm_camera_t *c = camera_new();

    if (c == NULL) {
        return -1;
    }
    c->x = x;
    c->y = y;
    c->w = w;
    c->h = h;
    c->angle = angle;
    c->target = target;
    c->speed_x = speed_x;
    c->speed_y = speed_y;
    c->border_x = border_x;
    c->border_y = border_y;
    return c->id;
}

void gm_camera_destroy(int id)
{
    gm_camera_t *c = gm_camera_get(id);

    if (c != NULL) {
        s_cam_used[c - s_cams] = false;
    }
}

/* CameraManager.CloneCamera (L71). */
static int camera_clone(int id)
{
    gm_camera_t *src = gm_camera_get(id);
    gm_camera_t *c;
    int new_id;

    if (src == NULL) {
        return -1;
    }
    c = camera_new();
    if (c == NULL) {
        return -1;
    }
    new_id = c->id;
    *c = *src;
    c->id = new_id;
    c->cloned = true;
    return new_id;
}

/* ---- setup ------------------------------------------------------------------------------- */

void gm_view_reset(void)
{
    memset(s_cam_used, 0, sizeof(s_cam_used));
    s_next_camera_id = 0;
    s_overflows = 0;
    memset(s_views, 0, sizeof(s_views));
    memset(s_arrays, 0, sizeof(s_arrays));
    s_enabled = false;
    s_in_room = false;
    s_current = 0;
}

void gm_view_init(const gm_room_def_t *rooms, int count)
{
    int r, v;

    gm_view_reset();
    s_store_count = count < GM_ROOM_MAX ? count : GM_ROOM_MAX;
    for (r = 0; r < s_store_count; ++r) {
        for (v = 0; v < GM_VIEW_COUNT; ++v) {
            s_store[r][v].def = rooms[r].views[v];
            s_store[r][v].camera = -1;
        }
    }
}

/* yyRoomManager.ResetAll (yyRoom.js L4746): every camera goes, storage views forget
 * their camera; other storage view edits (room_set_viewport) stay. */
void gm_view_reset_all(void)
{
    int r, v;

    gm_view_reset();
    for (r = 0; r < s_store_count; ++r) {
        for (v = 0; v < GM_VIEW_COUNT; ++v) {
            s_store[r][v].camera = -1;
        }
    }
}

int gm_view_current(void)
{
    return s_current;
}

void gm_view_set_current(int view)
{
    s_current = view;
}

long gm_view_overflows(void)
{
    return s_overflows;
}

/* ---- arrays ------------------------------------------------------------------------------- */

/* yyRoom.CopyViewsToArrays (yyRoom.js L892): all 8 views exist, so each slot is set. */
static void views_to_arrays(void)
{
    int i;

    for (i = 0; i < GM_VIEW_COUNT; ++i) {
        const view_t *v = &s_views[i];
        s_arrays[GM_VIEW_VISIBLE][i] = v->visible ? 1.0f : 0.0f;
        s_arrays[GM_VIEW_XVIEW][i] = v->worldx;
        s_arrays[GM_VIEW_YVIEW][i] = v->worldy;
        s_arrays[GM_VIEW_WVIEW][i] = v->worldw;
        s_arrays[GM_VIEW_HVIEW][i] = v->worldh;
        s_arrays[GM_VIEW_XPORT][i] = v->portx;
        s_arrays[GM_VIEW_YPORT][i] = v->porty;
        s_arrays[GM_VIEW_WPORT][i] = v->portw;
        s_arrays[GM_VIEW_HPORT][i] = v->porth;
        s_arrays[GM_VIEW_ANGLE][i] = v->angle;
        s_arrays[GM_VIEW_HBORDER][i] = v->hborder;
        s_arrays[GM_VIEW_VBORDER][i] = v->vborder;
        s_arrays[GM_VIEW_HSPEED][i] = v->hspeed;
        s_arrays[GM_VIEW_VSPEED][i] = v->vspeed;
        s_arrays[GM_VIEW_OBJECT][i] = v->objid;
        s_arrays[GM_VIEW_SURFACE_ID][i] = v->surface_id;
        s_arrays[GM_VIEW_CAMERA][i] = v->camera;
    }
}

/* yyRoom.CopyViewsFromArrays (L950). */
static void arrays_to_views(void)
{
    int i;

    for (i = 0; i < GM_VIEW_COUNT; ++i) {
        view_t *v = &s_views[i];
        v->visible = s_arrays[GM_VIEW_VISIBLE][i] > 0.5f;
        v->worldx = s_arrays[GM_VIEW_XVIEW][i];
        v->worldy = s_arrays[GM_VIEW_YVIEW][i];
        v->worldw = s_arrays[GM_VIEW_WVIEW][i];
        v->worldh = s_arrays[GM_VIEW_HVIEW][i];
        v->portx = s_arrays[GM_VIEW_XPORT][i];
        v->porty = s_arrays[GM_VIEW_YPORT][i];
        v->portw = s_arrays[GM_VIEW_WPORT][i];
        v->porth = s_arrays[GM_VIEW_HPORT][i];
        v->angle = s_arrays[GM_VIEW_ANGLE][i];
        v->hborder = s_arrays[GM_VIEW_HBORDER][i];
        v->vborder = s_arrays[GM_VIEW_VBORDER][i];
        v->hspeed = s_arrays[GM_VIEW_HSPEED][i];
        v->vspeed = s_arrays[GM_VIEW_VSPEED][i];
        v->objid = s_arrays[GM_VIEW_OBJECT][i];
        v->surface_id = s_arrays[GM_VIEW_SURFACE_ID][i];
        v->camera = s_arrays[GM_VIEW_CAMERA][i];
    }
}

float gm_view_get(int field, int index)
{
    if (field < 0 || field >= GM_VIEW_FIELD_COUNT || index < 0 || index >= GM_VIEW_COUNT) {
        return 0.0f;
    }
    return s_arrays[field][index];
}

void gm_view_set(int field, int index, float value)
{
    if (field >= 0 && field < GM_VIEW_FIELD_COUNT && index >= 0 && index < GM_VIEW_COUNT) {
        s_arrays[field][index] = value;
    }
}

bool gm_view_enabled(void)
{
    return s_enabled;
}

void gm_view_set_enabled(bool enabled)
{
    s_enabled = enabled;
}

/* ---- rooms -------------------------------------------------------------------------------- */

/* CreateViewFromStorage (yyView.js L200) + CreateCameraFromView (CameraManager.js L220). */
void gm_view_room_start(int room, const gm_room_def_t *def)
{
    int i;

    s_enabled = def->enable_views;
    s_in_room = true;
    for (i = 0; i < GM_VIEW_COUNT; ++i) {
        const view_store_t *st = room < s_store_count ? &s_store[room][i] : NULL;
        const gm_room_view_def_t *d = st != NULL ? &st->def : &def->views[i];
        view_t *v = &s_views[i];
        int cam;

        v->visible = d->visible;
        v->worldx = d->xview;
        v->worldy = d->yview;
        v->worldw = d->wview;
        v->worldh = d->hview;
        v->portx = d->xport;
        v->porty = d->yport;
        v->portw = d->wport;
        v->porth = d->hport;
        v->angle = 0.0f;
        v->hborder = d->hborder;
        v->vborder = d->vborder;
        v->hspeed = d->hspeed;
        v->vspeed = d->vspeed;
        v->objid = d->object;
        v->surface_id = -1.0f;
        if (st != NULL && st->camera >= 0) {
            cam = camera_clone(st->camera);
        } else {
            gm_camera_t *c = camera_new();
            cam = -1;
            if (c != NULL) {
                c->x = v->worldx;
                c->y = v->worldy;
                c->w = v->worldw;
                c->h = v->worldh;
                c->speed_x = v->hspeed;
                c->speed_y = v->vspeed;
                c->border_x = v->hborder;
                c->border_y = v->vborder;
                c->target = d->object;
                c->cloned = true;
                cam = c->id;
            }
        }
        v->camera = (float)cam;
    }
    views_to_arrays();
}

/* CameraManager.EndRoom (L257): cloned cameras go (the watermark is 0: no cameras are
 * made at load time in GMS2 games). */
void gm_view_room_end(void)
{
    int i;

    for (i = 0; i < GM_CAMERA_MAX; ++i) {
        if (s_cam_used[i] && s_cams[i].cloned) {
            s_cam_used[i] = false;
        }
    }
    s_in_room = false;
}

int gm_view_room_camera(int room, int view)
{
    if (room < 0 || room >= s_store_count || view < 0 || view >= GM_VIEW_COUNT) {
        return -1;
    }
    return s_store[room][view].camera;
}

void gm_view_room_set_camera(int room, int view, int camera)
{
    if (room >= 0 && room < s_store_count && view >= 0 && view < GM_VIEW_COUNT) {
        s_store[room][view].camera = camera;
    }
}

void gm_view_room_set_viewport(int room, int view, bool visible, int x, int y, int w, int h)
{
    if (room >= 0 && room < s_store_count && view >= 0 && view < GM_VIEW_COUNT) {
        gm_room_view_def_t *d = &s_store[room][view].def;
        d->visible = visible;
        d->xport = x;
        d->yport = y;
        d->wport = w;
        d->hport = h;
    }
}

/* ---- follow --------------------------------------------------------------------------------- */

static gm_instance_t *camera_target(int target)
{
    gm_instance_cursor_t c;
    gm_instance_t *inst;

    if (target < 0) {
        return NULL;
    }
    if (target < 10000) {
        /* First usable instance of the object's recursive pool. */
        gm_instance_search_begin(&c, target);
        return gm_instance_search_next(&c);
    }
    inst = gm_instance_find_by_id(target);
    return (inst != NULL && inst->active && !inst->marked_for_destroy) ? inst : NULL;
}
#include <stdio.h>
/* CCamera.Update (CameraManager.js L557-645), 2D and without an update script. */
void gm_camera_follow(gm_camera_t *cam, float room_width, float room_height)
{
    gm_instance_t *inst = camera_target(cam->target);
    float l, t, ix, iy;

    if (inst == NULL) {
        return;
    }
    l = cam->x;
    t = cam->y;
    ix = floorf(inst->x);
    iy = floorf(inst->y);

    if (2.0f * cam->border_x >= cam->w) {
        l = ix - cam->w * 0.5f;
    } else if (ix - cam->border_x < cam->x) {
        l = ix - cam->border_x;
    } else if (ix + cam->border_x > cam->x + cam->w) {
        l = ix + cam->border_x - cam->w;
    }
    if (2.0f * cam->border_y >= cam->h) {
        t = iy - cam->h * 0.5f;
    } else if (iy - cam->border_y < cam->y) {
        t = iy - cam->border_y;
    } else if (iy + cam->border_y > cam->y + cam->h) {
        t = iy + cam->border_y - cam->h;
    }

    if (l < 0.0f) {
        l = 0.0f;
    }
    if (l + cam->w > room_width) {
        l = room_width - cam->w;
    }
    if (t < 0.0f) {
        t = 0.0f;
    }
    if (t + cam->h > room_height) {
        t = room_height - cam->h;
    }

    if (cam->speed_x >= 0.0f) {
        if (l < cam->x && cam->x - l > cam->speed_x) {
            l = cam->x - cam->speed_x;
        }
        if (l > cam->x && l - cam->x > cam->speed_x) {
            l = cam->x + cam->speed_x;
        }
    }
    if (cam->speed_y >= 0.0f) {
        if (t < cam->y && cam->y - t > cam->speed_y) {
            t = cam->y - cam->speed_y;
        }
        if (t > cam->y && t - cam->y > cam->speed_y) {
            t = cam->y + cam->speed_y;
        }
    }
    cam->x = l;
    cam->y = t;
}

/* yyRoom.UpdateViews (yyRoom.js L1011). */
void gm_view_update(void)
{
    int i;

    if (!s_enabled || !s_in_room) {
        return;
    }
    arrays_to_views();
    for (i = 0; i < GM_VIEW_COUNT; ++i) {
        gm_camera_t *cam;
        if (!s_views[i].visible) {
            continue;
        }
        cam = gm_camera_get((int)s_views[i].camera);
        if (cam != NULL) {
            gm_camera_follow(cam, gm_room_width(), gm_room_height());
        }
    }
    views_to_arrays();
}
