/*
 * gm_view - views and cameras (GMS2 camera model: CameraManager.js, yyView.js,
 * yyRoom.js CopyViewsToArrays / UpdateViews, Function_Graphics.js view_get_*,
 * Function_Room.js room_get/set_camera, room_set_viewport).
 *
 *  - Every room's 8 storage views are copied into mutable storage at registry time,
 *    because room_set_camera / room_set_viewport (used by Spelunky's room_set_view
 *    compat script for every room at boot) rewrite them.
 *  - Room start creates the 8 views from storage (CreateViewFromStorage): a storage
 *    camera is cloned, otherwise a camera is made from the view. Both are "cloned"
 *    cameras, destroyed at room end (CameraManager.EndRoom with watermark 0); cameras
 *    from camera_create_view live until camera_destroy. Camera ids count up from 0.
 *  - The legacy view_* arrays (view_xview[], view_camera[] ...) are plain storage
 *    filled from the views at room start (CopyViewsToArrays) and round-tripped by
 *    gm_view_update. view_get_camera & co. read these arrays. As in the HTML5 runner,
 *    view_xview follows the *view*, not the camera (see HANDOFF_SLICE8.md Q7).
 *  - gm_view_update ports yyRoom.UpdateViews + CCamera.Update (follow a target with
 *    border/speed limits, clamped to the room). The game loop calls it each frame.
 */
#ifndef GM_VIEW_H
#define GM_VIEW_H

#include "gm_room.h"

#include <stdbool.h>

#ifndef GM_CAMERA_MAX
#define GM_CAMERA_MAX 96
#endif

/* Fields of the legacy view arrays (yyBuiltIn view_*). */
typedef enum gm_view_field {
    GM_VIEW_VISIBLE = 0,
    GM_VIEW_XVIEW,
    GM_VIEW_YVIEW,
    GM_VIEW_WVIEW,
    GM_VIEW_HVIEW,
    GM_VIEW_XPORT,
    GM_VIEW_YPORT,
    GM_VIEW_WPORT,
    GM_VIEW_HPORT,
    GM_VIEW_ANGLE,
    GM_VIEW_HBORDER,
    GM_VIEW_VBORDER,
    GM_VIEW_HSPEED,
    GM_VIEW_VSPEED,
    GM_VIEW_OBJECT,
    GM_VIEW_SURFACE_ID,
    GM_VIEW_CAMERA,
    GM_VIEW_FIELD_COUNT
} gm_view_field_t;

typedef struct gm_camera {
    int id;
    float x, y, w, h;
    float angle;
    float speed_x, speed_y;
    float border_x, border_y;
    int target;             /* object index (< 10000) or instance id; -1 none */
    bool cloned;
} gm_camera_t;

/* Copies every room's storage views (called by gm_room_registry_init). */
void gm_view_init(const gm_room_def_t *rooms, int count);

/* Destroys all cameras, restarts camera ids, forgets the current views. */
void gm_view_reset(void);

/* game_restart (yyRoomManager.ResetAll): gm_view_reset, and storage views forget
 * the cameras room_set_camera gave them. */
void gm_view_reset_all(void);

/* view_current: the view being drawn (g_pBuiltIn.view_current, yyRoom.js L3999). */
int gm_view_current(void);
void gm_view_set_current(int view);

/* Room switching (called by gm_room). */
void gm_view_room_start(int room, const gm_room_def_t *def);
void gm_view_room_end(void);

/* view_enabled */
bool gm_view_enabled(void);
void gm_view_set_enabled(bool enabled);

/* Legacy arrays: 0 for out-of-range indices; writes there are ignored. */
float gm_view_get(int field, int index);
void gm_view_set(int field, int index, float value);

/* Cameras. Getters return -1 for unknown cameras (camera_get_view_x ...). */
int gm_camera_create_view(float x, float y, float w, float h, float angle, int target,
                          float speed_x, float speed_y, float border_x, float border_y);
void gm_camera_destroy(int id);
gm_camera_t *gm_camera_get(int id);
int gm_camera_count(void);

/* Storage views of any room (room_get_camera / room_set_camera / room_set_viewport). */
int gm_view_room_camera(int room, int view);                 /* -1 if none */
void gm_view_room_set_camera(int room, int view, int camera);
void gm_view_room_set_viewport(int room, int view, bool visible, int x, int y, int w, int h);

/* The per-frame view update (yyRoom.UpdateViews). */
void gm_view_update(void);

/* CCamera.Update for one camera against the current room size. */
void gm_camera_follow(gm_camera_t *cam, float room_width, float room_height);

long gm_view_overflows(void);   /* cameras not created: pool full */

#endif /* GM_VIEW_H */
