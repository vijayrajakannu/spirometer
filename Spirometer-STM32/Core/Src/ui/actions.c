/*
 * actions.c  —  SpiroFlow navigation actions
 */

#include <stdio.h>        /* printf */
#include "actions.h"
#include "ui.h"
#include "screens.h"
#include "spirometry.h"   /* spiro_reset() on a fresh test */

/* ── Dashboard ──────────────────────────────────────────────────────────── */

void action_start_test(lv_event_t *e)
{
    (void)e;
    printf("[action] start_test -> spiro_reset + go dashboard\r\n");
    spiro_reset();
    /* Navigate to dashboard so the "READY / blow to start" prompt is visible.
     * spiro_reset() puts the engine back in SPIRO_STATE_IDLE, which auto-detects
     * flow and starts acquisition — but only once the user is back on the
     * dashboard and actually blows into the device. */
    loadScreen(SCREEN_ID_DASHBOARD);
}

void action_go_to_dashboard(lv_event_t *e)
{
    (void)e;
    printf("[action] go_to_dashboard -> loadScreen(%d)\r\n", SCREEN_ID_DASHBOARD);
    loadScreen(SCREEN_ID_DASHBOARD);
}

/* ── Results carousel ────────────────────────────────────────────────────── */

void action_go_to_results(lv_event_t *e)
{
    (void)e;
    printf("[action] go_to_results -> loadScreen(%d)\r\n", SCREEN_ID_RESULTS);
    loadScreen(SCREEN_ID_RESULTS);
}

void action_go_to_fvl(lv_event_t *e)
{
    (void)e;
    printf("[action] go_to_fvl -> loadScreen(%d)\r\n", SCREEN_ID_FVL);
    loadScreen(SCREEN_ID_FVL);
}

void action_go_to_vt(lv_event_t *e)
{
    (void)e;
    printf("[action] go_to_vt -> loadScreen(%d)\r\n", SCREEN_ID_VT);
    loadScreen(SCREEN_ID_VT);
}

/* ── Legacy shims ────────────────────────────────────────────────────────── */

void action_change_screen(lv_event_t *e)
{
    /* Old buttonmatrix nav — redirect all to dashboard */
    (void)e;
    loadScreen(SCREEN_ID_DASHBOARD);
}

void action_go_to_history(lv_event_t *e)
{
    (void)e;
    loadScreen(SCREEN_ID_VT);   /* History → Volume-Time screen */
}
