/*
 * ui.c  —  SpiroFlow UI entry point and screen loader
 *
 * FIX-C: loadScreen() now tracks the previous screen ID and selects
 *         LV_SCR_LOAD_ANIM_MOVE_LEFT or LV_SCR_LOAD_ANIM_MOVE_RIGHT
 *         based on whether we are moving to a higher or lower screen ID.
 *         This gives correct left/right animation throughout the carousel
 *         and when navigating dashboard ↔ results.
 *
 * FIX-D: on_screen_enter() — called by loadScreen() after every transition.
 *         Force-invalidates fvl_chart and vt_chart so the LVGL draw-post
 *         callback fires immediately on arrival, not on the next tick.
 *         Without this the charts stay blank because tick_screen() hasn't
 *         run yet for the newly-loaded screen when the user first sees it.
 *
 * FIX-E: previousScreen / currentScreen are updated BEFORE on_screen_enter()
 *         so that currentScreen is already correct when the enter hook fires.
 */

#include "ui.h"
#include "screens.h"
#include "images.h"
#include "actions.h"
#include "vars.h"
#include "spirometry.h"

#include <string.h>
#include <stdio.h>   /* diagnostic traces */

static int16_t currentScreen  = -1;
static int16_t previousScreen = -1;

static lv_obj_t *getLvglObjectFromIndex(int32_t index)
{
    if (index < 0) return NULL;
    lv_obj_t *screen_ptrs[] = {
        objects.boot,        /* 0 → SCREEN_ID_BOOT      */
        objects.scr_home,    /* 1 → SCREEN_ID_DASHBOARD */
        objects.results,     /* 2 → SCREEN_ID_RESULTS   */
        objects.fvl_screen,  /* 3 → SCREEN_ID_FVL       */
        objects.vt_screen,   /* 4 → SCREEN_ID_VT        */
    };
    if (index < 5) return screen_ptrs[index];
    return NULL;
}

/* ── on_screen_enter ─────────────────────────────────────────────────────
 *
 * Called after every screen transition (including the first load).
 * currentScreen has already been updated before this runs.
 *
 * For the two chart screens (FVL, VT):
 *   • Hide the "no data yet" label that was shown at screen-build time.
 *   • Force-invalidate the chart canvas so the LV_EVENT_DRAW_POST callback
 *     fires in the very next lv_timer_handler() pass.  Without this, LVGL
 *     won't repaint the canvas until something else dirties it — which may
 *     never happen if the user just watches the screen without touching it.
 * ─────────────────────────────────────────────────────────────────────── */
static void on_screen_enter(int16_t screenIdx)
{
    switch (screenIdx) {

    case 2:   /* SCREEN_ID_RESULTS - 1 */
        /*
         * Results labels are written by spirometry.c via lv_label_set_text()
         * before loadScreen(SCREEN_ID_RESULTS) is called, so LVGL already
         * marks them dirty.  Nothing extra is needed here.
         */
        break;

    case 3:
    {
        /* Only hide wait label when a result actually exists */
        if (objects.fvl_wait_lbl && spiro_get_result() != NULL)
            lv_obj_add_flag(objects.fvl_wait_lbl, LV_OBJ_FLAG_HIDDEN);

        if (objects.fvl_chart)
        {
            lv_obj_invalidate(objects.fvl_chart);
            lv_refr_now(NULL);
        }

        break;
    }

    case 4:
    {
        /* Only hide wait label when a result actually exists */
        if (objects.vt_wait_lbl && spiro_get_result() != NULL)
            lv_obj_add_flag(objects.vt_wait_lbl, LV_OBJ_FLAG_HIDDEN);

        if (objects.vt_chart)
        {
            lv_obj_invalidate(objects.vt_chart);
            lv_refr_now(NULL);
        }

        break;
    }

    default:
        break;
    }
}

/* ── loadScreen ──────────────────────────────────────────────────────────
 *
 * FIX-C: direction chosen by comparing nextScreen vs currentScreen.
 * FIX-D/E: state updated first, then on_screen_enter() called so the
 *           enter hook always sees the correct currentScreen value.
 * ─────────────────────────────────────────────────────────────────────── */
void loadScreen(enum ScreensEnum screenId)
{
	printf("[loadScreen] id=%d\r\n",
	       (int)screenId);
    int16_t nextScreen = (int16_t)(screenId - 1);
    lv_obj_t *screen   = getLvglObjectFromIndex(nextScreen);

    printf("[loadScreen] id=%d next=%d screen=%p prev=%d cur=%d\r\n",
           (int)screenId, (int)nextScreen, (void*)screen,
           (int)previousScreen, (int)currentScreen);

    if (screen) {
        if (currentScreen < 0) {
            /* ── First ever load: no animation ── */
            printf("[loadScreen] first load — lv_scr_load direct\r\n");

            /* Update state BEFORE entering so on_screen_enter sees correct idx */
            previousScreen = currentScreen;
            currentScreen  = nextScreen;

            lv_scr_load(screen);
            on_screen_enter(nextScreen);

        } else {
            lv_scr_load_anim_t anim = LV_SCR_LOAD_ANIM_MOVE_LEFT;
            if (previousScreen >= 0 && nextScreen < currentScreen) {
                anim = LV_SCR_LOAD_ANIM_MOVE_RIGHT;
            }
            printf("[loadScreen] anim=%s\r\n",
                   anim == LV_SCR_LOAD_ANIM_MOVE_LEFT ? "MOVE_LEFT" : "MOVE_RIGHT");

            /* Update state BEFORE entering */
            previousScreen = currentScreen;
            currentScreen  = nextScreen;
            printf("[loadScreen] loading anim screen=%p\r\n", (void*)screen);
            lv_scr_load_anim(screen, anim, 200, 0, false);
            on_screen_enter(nextScreen);
        }
    } else {
        printf("[loadScreen] ERROR — screen pointer is NULL for id=%d idx=%d\r\n",
               (int)screenId, (int)nextScreen);
    }

    /* NOTE: previousScreen / currentScreen are no longer updated here;
     * they were moved above so on_screen_enter() always sees correct state. */
}

void ui_init(void)
{
    printf("[ui] create_screens...\r\n");
    create_screens();
    printf("[ui] done\r\n");
    loadScreen(SCREEN_ID_DASHBOARD);   /* skip boot, go straight to dashboard */
}

void ui_tick(void)
{
    tick_screen(currentScreen);
}
