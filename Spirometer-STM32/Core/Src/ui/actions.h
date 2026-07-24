#ifndef EEZ_LVGL_UI_EVENTS_H
#define EEZ_LVGL_UI_EVENTS_H

#include <lvgl/lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Dashboard ─────────────────────────────────────────────────────────── */
void action_start_test(lv_event_t *e);       /* START TEST button → countdown  */
void action_go_to_dashboard(lv_event_t *e);  /* any → dashboard                */

/* ── Results carousel ─────────────────────────────────────────────────── */
void action_go_to_results(lv_event_t *e);
void action_go_to_fvl(lv_event_t *e);
void action_go_to_vt(lv_event_t *e);

/* ── Legacy — kept so older callers still compile ─────────────────────── */
void action_change_screen(lv_event_t *e);
void action_go_to_history(lv_event_t *e);

#ifdef __cplusplus
}
#endif

#endif /* EEZ_LVGL_UI_EVENTS_H */
