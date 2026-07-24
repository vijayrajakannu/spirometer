#ifndef EEZ_LVGL_UI_SCREENS_H
#define EEZ_LVGL_UI_SCREENS_H

#include <lvgl/lvgl.h>
#include "fonts.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Screen IDs ─────────────────────────────────────────────────────────── */
enum ScreensEnum {
    _SCREEN_ID_FIRST      = 1,
    SCREEN_ID_BOOT        = 1,   /* Boot splash                              */
    SCREEN_ID_DASHBOARD   = 2,   /* Main dashboard (Ready + recent result)   */
    SCREEN_ID_RESULTS     = 3,   /* Results summary (carousel page 1)        */
    SCREEN_ID_FVL         = 4,   /* Flow-Volume loop (carousel page 2)       */
    SCREEN_ID_VT          = 5,   /* Volume-Time graph (carousel page 3)      */
    /* Legacy aliases kept so spirometry.c / actions.c still compile */
    SCREEN_ID_SCR_HOME    = 2,
    SCREEN_ID_HISTORY     = 5,
    SCREEN_ID_PATIENT     = 2,   /* redirects to dashboard for now           */
    SCREEN_ID_SETTINGS    = 2,
    _SCREEN_ID_LAST       = 5
};

/* ── Master object table ─────────────────────────────────────────────────── */
typedef struct _objects_t {

    /* ── Screens ── */
    lv_obj_t *boot;        /* SCREEN_ID_BOOT        */
    lv_obj_t *scr_home;    /* SCREEN_ID_DASHBOARD   (alias dashboard)       */
    lv_obj_t *countdown;   /* SCREEN_ID_COUNTDOWN                           */
    lv_obj_t *live;        /* SCREEN_ID_LIVE                                */
    lv_obj_t *results;     /* SCREEN_ID_RESULTS                             */
    lv_obj_t *fvl_screen;  /* SCREEN_ID_FVL                                 */
    lv_obj_t *vt_screen;   /* SCREEN_ID_VT                                  */

    /* ── Boot screen ── */
    lv_obj_t *boot_bar;          /* progress bar                            */
    lv_obj_t *boot_status_lbl;   /* "Checking sensors…" / "Ready"           */
    lv_obj_t *boot_fw_lbl;       /* firmware version label                  */

    /* ── Dashboard ── */
    lv_obj_t *obj0;              /* status bar container (legacy)            */
    lv_obj_t *dash_time_lbl;     /* HH:MM                                   */
    lv_obj_t *dash_bat_lbl;      /* "84%"                                   */
    lv_obj_t *dash_start_btn;    /* START TEST button                       */
    lv_obj_t *dash_last_fev1;    /* "3.42 L" recent FEV1                    */
    lv_obj_t *dash_last_fvc;     /* "4.21 L" recent FVC                     */
    lv_obj_t *dash_last_grade;   /* "Quality A"                             */
    lv_obj_t *dash_last_date;    /* date/time of last test                  */

    /* ── Countdown ── */
    lv_obj_t *countdown_lbl;     /* large "3" / "2" / "1" / "BLOW!"         */

    /* ── Live test ── */
    lv_obj_t *live_flow_lbl;     /* current flow "X.XX L/s"                 */
    lv_obj_t *live_vol_lbl;      /* current volume "X.XX L"                 */
    lv_obj_t *live_time_lbl;     /* elapsed time "Xs"                       */
    lv_obj_t *live_coaching_lbl; /* "BLOW HARDER" etc.                      */
    lv_obj_t *live_chart;        /* real-time flow-time canvas              */

    /* ── Results ── */
    lv_obj_t *res_grade_lbl;     /* "A" / "B" etc.                          */
    lv_obj_t *res_interp_lbl;    /* "NORMAL" / "Possible Obstructive…"      */
    lv_obj_t *res_start_lbl;     /* "✓" or "✗"                              */
    lv_obj_t *res_end_lbl;       /* "✓" or "✗"                              */
    /* Parameter table labels — each row: actual / predicted / pct */
    lv_obj_t *res_fvc_act;  lv_obj_t *res_fvc_pred;  lv_obj_t *res_fvc_pct;
    lv_obj_t *res_fev1_act; lv_obj_t *res_fev1_pred; lv_obj_t *res_fev1_pct;
    lv_obj_t *res_fev6_act; lv_obj_t *res_fev6_pred; lv_obj_t *res_fev6_pct;
    lv_obj_t *res_ratio_act;lv_obj_t *res_ratio_pred;lv_obj_t *res_ratio_pct;
    lv_obj_t *res_pef_act;  lv_obj_t *res_pef_pred;  lv_obj_t *res_pef_pct;
    lv_obj_t *res_fef25_act;
    lv_obj_t *res_fef50_act;
    lv_obj_t *res_fef75_act;
    lv_obj_t *res_fef2575_act;
    lv_obj_t *res_page_lbl; /* "1 / 3"                                      */

    /* ── Flow-Volume loop screen ── */
    lv_obj_t *fvl_chart;        /* plot canvas (draw-post callback)         */
    lv_obj_t *fvl_wait_lbl;
    lv_obj_t *fvl_ylabel[4];
    lv_obj_t *fvl_xlabel[4];
    lv_obj_t *fvl_page_lbl;     /* "2 / 3"                                  */

    /* ── Volume-Time graph screen ── */
    lv_obj_t *vt_chart;         /* plot canvas (draw-post callback)         */
    lv_obj_t *vt_wait_lbl;
    lv_obj_t *vt_ylabel[4];
    lv_obj_t *vt_xlabel[4];
    lv_obj_t *vt_page_lbl;      /* "3 / 3"                                  */

    /* ── Legacy aliases (spirometry.c accesses these by name) ── */
    lv_obj_t *fev1_val;          /* same as res_fev1_act                    */
    lv_obj_t *obj1;              /* unused, kept for ABI                    */
    lv_obj_t *obj2;              /* FEV1 progress bar (results)             */
    lv_obj_t *obj3;              /* FEV1 % label                            */
    lv_obj_t *obj4;              /* FVC value label                         */
    lv_obj_t *obj5;              /* FVC progress bar                        */
    lv_obj_t *obj6;              /* ratio value label                       */
    lv_obj_t *obj7;              /* ratio progress bar                      */
    lv_obj_t *obj8;              /* PEF value label                         */
    lv_obj_t *obj9;              /* PEF progress bar                        */
    lv_obj_t *obj10;             /* unused                                  */
    lv_obj_t *obj11;             /* unused                                  */
    lv_obj_t *te_val;
    lv_obj_t *tpef_val;
    lv_obj_t *fef2575_val;
    lv_obj_t *fef50_val;
    lv_obj_t *sat_label;
    lv_obj_t *validity_label;

    /* ── Aliases kept for History / Patient / Settings screens ── */
    lv_obj_t *history;
    lv_obj_t *patient;
    lv_obj_t *settings;

} objects_t;

extern objects_t objects;

/* ── Screen creation / tick ─────────────────────────────────────────────── */
void create_screens(void);
void create_screen_boot(void);
void create_screen_dashboard(void);
void create_screen_results(void);
void create_screen_fvl(void);
void create_screen_vt(void);

/* Legacy shims */
static inline void create_screen_scr_home(void)  {}
static inline void create_screen_history(void)   {}
static inline void create_screen_patient(void)   {}
static inline void create_screen_settings(void)  {}

void tick_screen(int screen_index);
void tick_screen_by_id(enum ScreensEnum screenId);
void tick_screen_boot(void);
void tick_screen_dashboard(void);
void tick_screen_results(void);
void tick_screen_fvl(void);
void tick_screen_vt(void);

extern ext_font_desc_t fonts[];
extern uint32_t active_theme_index;

/* Live test helpers — still called by spirometry.c during acquisition.
 * The live screen has been removed, so the underlying objects are never
 * created and these resolve to safe no-ops (every body is null-guarded). */
void live_update_flow(float flow_lps, float vol_l, uint32_t elapsed_ms);
void live_set_coaching(const char *msg);
void live_push_sample(float flow_lps);  /* appends one point to live chart */

#ifdef __cplusplus
}
#endif

#endif /* EEZ_LVGL_UI_SCREENS_H */
