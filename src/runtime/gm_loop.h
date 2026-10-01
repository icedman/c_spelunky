/*
 * gm_loop - the GameMaker frame pipeline (GameMaker-HTML5 _GameMaker.js
 * GameMaker_Tick L2314 and GameMaker_DoAStep L1716).
 *
 * gm_loop_step() runs one frame, in the runner's order:
 *
 *   1. input           IO_StartStep (gm_input_start_step)
 *   2. old positions   xprevious/yprevious = x/y and Animate (image_index += image_speed)
 *   3. images          UpdateImages: wrap image_index, Animation End (other 7)
 *   4. layers          UpdateLayers (gm_layer_update)
 *   5. Begin Step      (step 1), over the active list from the end
 *   6. alarms          HandleAlarm: only alarms with a handler tick; fire at 0
 *   7. keyboard        Keyboard / Key Press / Key Release events (HandleKeyboard)
 *   8. Step            (step 0), from the end
 *   9. motion          UpdatePositions: friction, gravity, x += hspeed, y += vspeed
 *  10. other           HandleOther: Outside Room (other 0), Intersect Boundary (other 1)
 *  11. collisions      HandleCollision over the collision pair list (see below)
 *  12. End Step        (step 2), from the end
 *  13. reclaim         RemoveMarked (gm_instance_reclaim_marked)
 *  14. draw            Pre Draw, view update, per visible view: Draw Begin / Draw /
 *                      Draw End, then Post Draw, Draw GUI Begin / GUI / GUI End
 *
 * After every user-code phase from 3 on the step ends early if a room change is
 * pending (`if (New_Room != -1) return;`), and every single event asks
 * gm_room_event_blocked (yyObject.PerformEvent's gate). gm_loop_tick() is
 * GameMaker_Tick's inner loop: after a step it switches room and runs another full
 * step in the new room (at most 10 steps per tick), restarts the game, or reports
 * that the game ended.
 *
 * Event handlers come from hooks (the generated event table), using GML event
 * codes: (type, number) as in .yy files. Collision pairs are derived once, in
 * gm_loop_init, exactly as CreateCollisionArrays / AddCollision (LoadGame.js L636)
 * do: an object pair is listed once, under the first object (parents first) that
 * defines it, and skipped when either side's ancestors already collide.
 *
 * Draw events run headless too: Spelunky's draw code changes game state (e.g.
 * oGameMakerLogo creates instances from Draw). Instances without a Draw event are
 * passed to hooks.draw_self (the platform's default draw; NULL when headless).
 *
 * Not modelled (unused by Spelunky Classic HD, see HANDOFF_SLICE9.md): paths,
 * timelines, time sources, async events, mouse events, outside/boundary view
 * events, sequences, particles, physics, collision compatibility mode.
 *
 * Memory: static buffers only; no allocation.
 */
#ifndef GM_LOOP_H
#define GM_LOOP_H

#include "gm_instance.h"

#include <stdbool.h>

/* GML event codes used by the loop (.yy eventType / eventNum, Globals.js GML_EVENT_*). */
#define GM_EV_TYPE_ALARM 2
#define GM_EV_TYPE_STEP 3
#define GM_EV_TYPE_COLLISION 4
#define GM_EV_TYPE_KEYBOARD 5
#define GM_EV_TYPE_OTHER 7
#define GM_EV_TYPE_DRAW 8
#define GM_EV_TYPE_KEYPRESS 9
#define GM_EV_TYPE_KEYRELEASE 10

#define GM_EV_STEP_NORMAL 0
#define GM_EV_STEP_BEGIN 1
#define GM_EV_STEP_END 2

#define GM_EV_OTHER_OUTSIDE 0
#define GM_EV_OTHER_BOUNDARY 1
#define GM_EV_OTHER_ANIMATION_END 7

#define GM_EV_DRAW_NORMAL 0
#define GM_EV_DRAW_GUI 64
#define GM_EV_DRAW_BEGIN 72
#define GM_EV_DRAW_END 73
#define GM_EV_DRAW_GUI_BEGIN 74
#define GM_EV_DRAW_GUI_END 75
#define GM_EV_DRAW_PRE 76
#define GM_EV_DRAW_POST 77

/* Collision pairs (Spelunky Classic HD derives about 150). */
#ifndef GM_LOOP_MAX_PAIRS
#define GM_LOOP_MAX_PAIRS 1024
#endif

#define GM_LOOP_MAX_STEPS_PER_TICK 10   /* ErrorCount (GameMaker_Tick L2378) */

typedef struct gm_loop_hooks {
    /* Handler of event (type, number) for the object or its nearest ancestor
     * defining it (REvent / PerformEvent), or NULL. Required. */
    gm_event_fn (*find_event)(int object_index, int type, int number);
    /* Handler defined on the object itself (pObj.Event / pObj.Collisions), or
     * NULL. Required: collision events resolve through both parent chains. */
    gm_event_fn (*find_own_event)(int object_index, int type, int number);
    /* Default draw of a visible instance without a Draw event. May be NULL. */
    void (*draw_self)(gm_instance_t *inst);
    /* Called at the end of every tick, with no GML on the stack (the gm_heap
     * collection safe point). May be NULL. */
    void (*safe_point)(void);
} gm_loop_hooks_t;

typedef enum gm_loop_status {
    GM_LOOP_RUNNING = 0,
    GM_LOOP_ENDED      /* game_end() was called; the host should quit */
} gm_loop_status_t;

/* Installs the hooks (borrowed) and derives the collision pairs of the current
 * object registry. Call after gm_object_registry_init. Returns false (and runs
 * nothing) if a required hook is missing or the pairs do not fit. */
bool gm_loop_init(const gm_loop_hooks_t *hooks);

/* One frame (GameMaker_DoAStep). */
void gm_loop_step(void);

/* GameMaker_Tick: steps, then acts on the pending room / restart / end. */
gm_loop_status_t gm_loop_tick(void);

/* event_perform (Function_Game.js L339): self's (inherited) handler of (type, number)
 * with the caller's other, through the room-change gate. Collision events would also
 * walk the other object's parents; Spelunky only performs key press events. */
void gm_loop_event_perform(gm_instance_t *self, gm_instance_t *other, int type, int number);

/* Draw_Automatic (draw events on by default). */
void gm_loop_set_draw_enabled(bool enabled);

/* Diagnostics / tests. */
long gm_loop_frame_count(void);            /* gm_loop_step calls since init */
int gm_loop_pair_count(void);
bool gm_loop_pair(int index, int *object1, int *object2);   /* in dispatch order */

#endif /* GM_LOOP_H */
