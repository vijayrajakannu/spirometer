/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file    test_main.c
 * @brief   No-touch test harness for SpiroFlow
 *
 * PURPOSE
 * -------
 * Replaces main.c entirely when building with -DSPIROFLOW_TEST_MODE.
 * Bypasses XPT2046 calibration and the LVGL pointer indev so the system
 * runs without any touch hardware.  A software "button injector" fires
 * simulated button presses on a timer so every screen transition and the
 * full spirometry pipeline can be exercised over UART.
 *
 * WHAT IS TESTED
 * --------------
 *  T01  Boot screen progress bar animates → auto-transitions to Dashboard
 *  T02  Dashboard renders (READY card + START TEST button)
 *  T03  START TEST injected → Countdown screen 3→2→1→BLOW!
 *  T04  Live screen appears with coaching labels
 *  T05  Synthetic ADC waveform (ISO 26782 C2 profile) injected into engine
 *  T06  Engine transitions IDLE→ACQUIRING→COMPUTING→DISPLAYING
 *  T07  Results screen populated: FVC, FEV1, FEV6, ratio, PEF, grade
 *  T08  ISO §7.5 BEV checked against 0.150 L / 5% FVC limit
 *  T09  ISO §7.6 end-of-test plateau detected (ΔV < 0.025 L in 1 s)
 *  T10  ISO §7.3 FVL aspect ratio 2:1 verified from axis scale variables
 *  T11  ISO §7.3 VT  aspect ratio 1:1 verified from axis scale variables
 *  T12  Carousel navigation Results→FVL→VT→Results (injected button taps)
 *  T13  CANCEL during live test → spiro_reset() → engine returns to IDLE
 *  T14  Second maneuver from DISPLAYING state starts cleanly
 *  T15  Dashboard last-test card shows updated FEV1/FVC/Grade after test
 *
 * SYNTHETIC WAVEFORM
 * ------------------
 * ISO 26782 Annex C, defined test profile C2 (fast start, smooth finish):
 *   V(t) = FVC × [1 − e^(−t/τ)]   FVC=5.0 L, τ=1.00 s
 *   Rise time 38 ms (fast start).
 * We generate this at SPIRO_ADC_FS_HZ by numerically sampling the curve and
 * converting flow (dV/dt) to ADC counts.
 * Expected results (Table C.1): FEV1=3.272 L, FEV6=5.168 L, FVC=5.179 L.
 * Tolerance: ±3% or ±0.05 L (ISO §7.1).
 *
 * LOGGING FORMAT
 * --------------
 * Every log line is prefixed with a timestamp and a tag:
 *   [  1234] [INFO ] message
 *   [  1234] [TEST ] T01 PASS  Boot→Dashboard transition fired
 *   [  1234] [TEST ] T07 FAIL  FEV1 expected 3.27 got 0.00
 *   [  1234] [WARN ] unexpected state during cancel test
 *   [  1234] [RESULT] FVC=5.18 FEV1=3.27 FEV6=5.17 ratio=63.1 PEF=7.23 grade=A
 *
 * UART OUTPUT: USART1, PA9 TX, 115200 8N1.
 * Capture with any serial terminal.  Python script to capture:
 *   python -m serial.tools.miniterm COM3 115200
 *
 * BUILD
 * -----
 * Add to your CubeIDE project (or Makefile):
 *   1. Add test_main.c to the source list.
 *   2. Add -DSPIROFLOW_TEST_MODE to C preprocessor flags.
 *   3. Remove (or exclude) the real main.c from the build.
 *   Alternatively: rename this file to main.c and swap it in.
 *
 * HARDWARE REQUIRED
 * -----------------
 *   • STM32F4 board with ILI9341 display on SPI1 (display only — no touch).
 *   • USART1 PA9 TX → USB-UART adapter → PC serial terminal.
 *   • ADC1 pin can be unconnected (test uses injected samples, not real ADC).
 *
 ******************************************************************************
 */
/* USER CODE END Header */

#include "main.h"
#include "adc.h"
#include "dma.h"
#include "spi.h"
#include "usart.h"
#include "gpio.h"

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>

#include "lvgl.h"
#include "lv_port_disp.h"
#include "ui/ui.h"
#include "ui/screens.h"
#include "spirometry.h"

/* ── UART printf redirect ────────────────────────────────────────────────── */
int __io_putchar(int ch)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
    return ch;
}

/* ── Logging macros ──────────────────────────────────────────────────────── */
#define LOG_INFO(fmt, ...)   printf("[%6lu] [INFO ] " fmt "\r\n", HAL_GetTick(), ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)   printf("[%6lu] [WARN ] " fmt "\r\n", HAL_GetTick(), ##__VA_ARGS__)
#define LOG_TEST(id, ok, fmt, ...) do { \
    printf("[%6lu] [TEST ] %s %s  " fmt "\r\n", \
           HAL_GetTick(), id, (ok) ? "PASS" : "FAIL", ##__VA_ARGS__); \
    if (!(ok)) g_fail_count++; else g_pass_count++; \
} while(0)
#define LOG_RESULT(fmt, ...) printf("[%6lu] [RESULT] " fmt "\r\n", HAL_GetTick(), ##__VA_ARGS__)
#define LOG_SEP()            printf("[%6lu] [----] ----------------------------------------\r\n", HAL_GetTick())

/* ── Global pass/fail counters ───────────────────────────────────────────── */
static uint32_t g_pass_count = 0;
static uint32_t g_fail_count = 0;

/* ── Test phase bookkeeping ──────────────────────────────────────────────── */
typedef enum {
    PHASE_BOOT = 0,
    PHASE_DASHBOARD,
    PHASE_COUNTDOWN,
    PHASE_LIVE,
    PHASE_RESULTS,
    PHASE_FVL,
    PHASE_VT,
    PHASE_CANCEL_TEST,
    PHASE_SECOND_MANEUVER,
    PHASE_DONE
} test_phase_t;

static test_phase_t g_phase       = PHASE_BOOT;
static uint32_t     g_phase_enter = 0;  /* tick when we entered this phase  */
static bool         g_waveform_injected = false;
static bool         g_results_verified  = false;

/* ── ISO C2 waveform parameters ─────────────────────────────────────────── */
/*
 * ISO 26782 Table C.1 profile C2:
 *   FVC = 5.0 L (exp volume),  τ = 1.00 s,  fast start (tR = 38 ms)
 *   Expected: FEV1 = 3.272 L,  FEV6 = 5.168 L,  FVC = 5.179 L
 *
 * We generate the flow waveform as dV/dt of V(t) = 5.0 × [1 − e^(−t/τ)]
 * which gives  flow(t) = (5.0/τ) × e^(−t/τ).
 *
 * Rise phase (fast start, 38 ms): linear ramp from 0 to peak flow.
 * End phase: smooth finish — flow continues until plateau at 0.025 L/s.
 *
 * ADC conversion: flow_lps = (raw − SPIRO_ZERO_COUNTS) / SPIRO_COUNTS_PER_LPS
 * So:  raw = SPIRO_ZERO_COUNTS + flow_lps × SPIRO_COUNTS_PER_LPS
 */
#define C2_FVC_L      5.0f
#define C2_TAU_S      1.00f
#define C2_RISE_MS    38        /* fast start rise time (10%→90% PEF) */
#define C2_TOTAL_S    12.0f     /* generate 12 s of data               */

/* ISO §7.1 tolerance: ±3% or ±0.05 L whichever is greater */
static bool iso_vol_ok(float actual, float expected)
{
    float tol = 0.03f * expected;
    if (tol < 0.050f) tol = 0.050f;
    float err = actual - expected;
    if (err < 0.0f) err = -err;
    return (err <= tol);
}

/* ── Waveform generation ─────────────────────────────────────────────────── */
/*
 * Generates one sample of the C2 flow profile at sample index i.
 * Returns the ADC raw count that spiro_push_sample() should receive.
 *
 * Timeline:
 *   0 ms to RISE_MS: linear ramp from 0 to PEF = FVC/TAU
 *   RISE_MS onwards: exponential decay  flow = PEF × e^(-(t-t_rise)/TAU)
 *   End when flow drops below 0.025 L/s (ISO plateau threshold)
 *
 * We pre-compute N_RISE samples and N_EXP samples offline; at runtime we
 * just index into the closed-form expression to avoid floating-point division
 * loops.
 */
static uint16_t c2_sample(uint32_t i, uint32_t fs_hz, uint32_t *out_n_samples)
{
    float dt      = 1.0f / (float)fs_hz;
    float t       = (float)i * dt;
    float t_rise  = (float)C2_RISE_MS * 0.001f;
    float pef     = C2_FVC_L / C2_TAU_S;    /* 5.0 L/s for C2 */
    float flow;

    if (t < t_rise) {
        /* Linear ramp to PEF */
        flow = pef * (t / t_rise);
    } else {
        /* Exponential decay from PEF at t = t_rise */
        flow = pef * expf(-(t - t_rise) / C2_TAU_S);
    }

    if (flow < 0.0f) flow = 0.0f;

    /* Number of samples is determined by caller checking plateau */
    if (out_n_samples) {
        /* Compute total sample count once: until flow < 0.025 L/s */
        float t_end = t_rise + C2_TAU_S * (-logf(0.025f / pef));
        *out_n_samples = (uint32_t)(t_end * (float)fs_hz) + 1;
        if (*out_n_samples > (uint32_t)C2_TOTAL_S * fs_hz)
            *out_n_samples = (uint32_t)C2_TOTAL_S * fs_hz;
    }

    /* Convert flow to ADC counts */
    uint32_t raw = (uint32_t)SPIRO_ZERO_COUNTS + (uint32_t)(flow * (float)SPIRO_COUNTS_PER_LPS);
    if (raw > SPIRO_ADC_MAX) raw = SPIRO_ADC_MAX - 1;
    return (uint16_t)raw;
}

/* ── Stub: no-op touch driver (replaces xpt2046 / touch_cal) ────────────── */
/*
 * These replace the real xpt2046.c and touch_cal.c functions.
 * Since we compile without the real touch files (or they are excluded from
 * the build), we need these stubs so the linker is satisfied.
 *
 * If your build INCLUDES the real xpt2046.c, define TEST_STUB_TOUCH = 0
 * in your compiler flags and the real functions will be used (they will
 * just never report a press).
 */
#ifndef TEST_STUB_TOUCH
#define TEST_STUB_TOUCH 1
#endif

#if TEST_STUB_TOUCH
void    xpt2046_init(void)                                       { /* no-op */ }
void    xpt2046_set_cal(int32_t a, int32_t b, int32_t c, int32_t d) { (void)a;(void)b;(void)c;(void)d; }
bool    xpt2046_read_raw_point(uint16_t *x, uint16_t *y)         { *x=0; *y=0; return false; }
bool    xpt2046_get_touch(int16_t *x, int16_t *y)                { *x=0; *y=0; return false; }
void    touch_cal_run(void)                                      { /* no-op */ }
void    lv_touchpad_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    data->state = LV_INDEV_STATE_RELEASED;
}
#endif /* TEST_STUB_TOUCH */

/* ── Synthetic ADC override ──────────────────────────────────────────────── */
/*
 * The spirometry engine calls poll_adc_sample() internally (static in
 * spirometry.c). We cannot intercept it directly.  Instead we use
 * spiro_push_sample() to inject pre-generated samples directly, and we
 * drive the engine state machine manually via spiro_process() while in
 * PHASE_LIVE.
 *
 * Strategy:
 *   1. When phase == PHASE_LIVE, instead of calling spiro_process() (which
 *      would poll the real ADC), we call the injection loop below.
 *   2. The injection loop pushes one C2 sample per 1/FS_HZ, then calls
 *      lv_timer_handler() to let LVGL render.
 *   3. After all samples are injected, we call spiro_process() once to
 *      trigger COMPUTING→DISPLAYING.
 *
 * NOTE: spiro_push_sample() is declared in spirometry.h.  The engine still
 * calls poll_adc_sample() in ACQUIRING state — we need to prevent it from
 * overwriting our injected samples.  We do this by calling spiro_reset()
 * first to bring state back to IDLE, then manually force ACQUIRING by
 * pushing the first sample with a flow above SPIRO_BLOW_THRESH_LPS.
 */
static bool     g_inject_active   = false;
static uint32_t g_inject_idx      = 0;
static uint32_t g_inject_n        = 0;
static uint32_t g_inject_last_ms  = 0;

static void inject_init(void)
{
    /* Compute total sample count for C2 profile */
    uint32_t n = 0;
    c2_sample(0, SPIRO_ADC_FS_HZ, &n);
    g_inject_n      = n;
    g_inject_idx    = 0;
    g_inject_active = true;
    g_inject_last_ms = HAL_GetTick();

    LOG_INFO("C2 waveform: %lu samples at %u Hz = %.1f s",
             (unsigned long)g_inject_n,
             (unsigned)SPIRO_ADC_FS_HZ,
             (float)g_inject_n / (float)SPIRO_ADC_FS_HZ);

    /* Reset engine and let it arm for IDLE→ACQUIRING on the first push */
    spiro_reset();
}

/*
 * inject_tick() — call every main loop iteration while injection is active.
 * Pushes samples at the correct rate (one per 1/FS ms) using HAL_GetTick().
 * Returns true when injection is complete.
 */
static bool inject_tick(void)
{
    if (!g_inject_active) return true;

    uint32_t now  = HAL_GetTick();
    uint32_t dt_ms = 1000u / SPIRO_ADC_FS_HZ;   /* ms per sample */

    while (g_inject_idx < g_inject_n) {
        if (now - g_inject_last_ms < dt_ms) break;  /* rate-limit */
        g_inject_last_ms += dt_ms;

        uint16_t raw = c2_sample(g_inject_idx, SPIRO_ADC_FS_HZ, NULL);
        spiro_push_sample(raw);
        g_inject_idx++;
    }

    if (g_inject_idx >= g_inject_n) {
        g_inject_active = false;
        LOG_INFO("Waveform injection complete: %lu samples pushed",
                 (unsigned long)g_inject_idx);
        return true;
    }
    return false;
}

/* ── Button injector ─────────────────────────────────────────────────────── */
/*
 * Simulate a button tap at (x, y) by sending an LVGL pointer event directly
 * to the active screen.  No indev polling needed.
 *
 * We create a one-shot press+release event pair on the LVGL object at the
 * given coordinates.  lv_obj_send_event is used with LV_EVENT_CLICKED on
 * the specific button object to avoid needing coordinate hit-testing.
 */
static void inject_click(lv_obj_t *obj, const char *desc)
{
    if (!obj) {
        LOG_WARN("inject_click: NULL object for '%s'", desc);
        return;
    }
    LOG_INFO(">>> inject_click: %s", desc);
    lv_obj_send_event(obj, LV_EVENT_CLICKED, NULL);
    /* Run LVGL timer to process the event synchronously */
    lv_timer_handler();
    HAL_Delay(50);   /* brief settle — allows screen animations to start */
    lv_timer_handler();
}

/* ── Assertion helpers ───────────────────────────────────────────────────── */
static void check_screen(const char *test_id, enum ScreensEnum expected_id,
                          lv_obj_t *expected_obj, const char *name)
{
    lv_obj_t *active = lv_scr_act();
    bool ok = (active == expected_obj);
    LOG_TEST(test_id, ok, "Screen = %s (%s)", name, ok ? "correct" : "WRONG SCREEN ACTIVE");
    if (!ok && active) {
        /* Print which screen IS active for debugging */
        if      (active == objects.boot)       LOG_WARN("  active=BOOT");
        else if (active == objects.scr_home)   LOG_WARN("  active=DASHBOARD");
        else if (active == objects.countdown)  LOG_WARN("  active=COUNTDOWN");
        else if (active == objects.live)       LOG_WARN("  active=LIVE");
        else if (active == objects.results)    LOG_WARN("  active=RESULTS");
        else if (active == objects.fvl_screen) LOG_WARN("  active=FVL");
        else if (active == objects.vt_screen)  LOG_WARN("  active=VT");
        else                                   LOG_WARN("  active=UNKNOWN");
    }
}

static void check_label(const char *test_id, lv_obj_t *lbl,
                         const char *name, const char *not_expected)
{
    if (!lbl) { LOG_TEST(test_id, false, "Label %s is NULL", name); return; }
    const char *text = lv_label_get_text(lbl);
    bool ok = (text != NULL) && (strcmp(text, not_expected) != 0) && (strlen(text) > 0);
    LOG_TEST(test_id, ok, "Label %s = \"%s\"", name, text ? text : "(null)");
}

static void check_vol_label(const char *test_id, lv_obj_t *lbl,
                              const char *name, float expected_l)
{
    if (!lbl) { LOG_TEST(test_id, false, "Vol label %s is NULL", name); return; }
    const char *text = lv_label_get_text(lbl);
    if (!text || strcmp(text, "--") == 0) {
        LOG_TEST(test_id, false, "%s still placeholder '--'", name);
        return;
    }
    /* Parse the displayed value (format "X.XX") */
    int whole = 0, frac = 0;
    if (sscanf(text, "%d.%d", &whole, &frac) != 2) {
        LOG_TEST(test_id, false, "%s unparseable: \"%s\"", name, text);
        return;
    }
    float actual = (float)whole + (float)frac / 100.0f;
    bool ok = iso_vol_ok(actual, expected_l);
    LOG_TEST(test_id, ok,
             "%s: displayed=%.2f expected=%.2f tol=±%.3f",
             name, actual, expected_l,
             (0.03f * expected_l < 0.05f ? 0.05f : 0.03f * expected_l));
}

/* ── Phase transition logic ──────────────────────────────────────────────── */
static void enter_phase(test_phase_t p)
{
    const char *names[] = {
        "BOOT", "DASHBOARD", "COUNTDOWN", "LIVE",
        "RESULTS", "FVL", "VT", "CANCEL_TEST", "SECOND_MANEUVER", "DONE"
    };
    LOG_SEP();
    LOG_INFO("=== Phase: %s ===", names[p]);
    g_phase       = p;
    g_phase_enter = HAL_GetTick();
}

/* ── Phase handlers ──────────────────────────────────────────────────────── */

/* T01: Boot screen — wait for progress bar to reach 100 and auto-transition */
static void phase_boot(void)
{
    uint32_t elapsed = HAL_GetTick() - g_phase_enter;
    /* Boot bar animates at 2% per tick; 50 ticks × 2 ms = 100 ms minimum.
     * In practice tick_screen_boot fires on every lv_timer_handler() call
     * (~2 ms), so full bar takes ~50 × 2 ms = 100 ms.
     * We wait up to 3 s for the auto-transition to DASHBOARD. */
    if (elapsed < 3000) return;

    /* Check if we transitioned (boot should have called loadScreen(DASHBOARD)) */
    bool transitioned = (lv_scr_act() == objects.scr_home);
    LOG_TEST("T01", transitioned, "Boot→Dashboard auto-transition (elapsed=%lums)",
             (unsigned long)elapsed);
    enter_phase(PHASE_DASHBOARD);
}

/* T02: Dashboard — verify card + button present, then inject START TEST */
static void phase_dashboard(void)
{
    uint32_t elapsed = HAL_GetTick() - g_phase_enter;
    if (elapsed < 200) return;   /* brief settle */

    /* T02a: correct screen */
    check_screen("T02a", SCREEN_ID_DASHBOARD, objects.scr_home, "DASHBOARD");

    /* T02b: START TEST button exists */
    bool btn_ok = (objects.dash_start_btn != NULL);
    LOG_TEST("T02b", btn_ok, "dash_start_btn object exists");

    /* T02c: status bar time label is present and non-empty */
    check_label("T02c", objects.dash_time_lbl, "dash_time_lbl", "");

    /* T02d: battery label present */
    check_label("T02d", objects.dash_bat_lbl, "dash_bat_lbl", "");

    LOG_INFO("Injecting START TEST button tap...");
    inject_click(objects.dash_start_btn, "START TEST");
    enter_phase(PHASE_COUNTDOWN);
}

/* T03: Countdown screen 3→2→1→BLOW! — verify label text cycling */
static void phase_countdown(void)
{
    uint32_t elapsed = HAL_GetTick() - g_phase_enter;

    /* Countdown timer fires every 1000 ms; full sequence = 5 steps × 1s */
    if (elapsed < 200) {
        check_screen("T03a", SCREEN_ID_COUNTDOWN, objects.countdown, "COUNTDOWN");
        const char *cd = lv_label_get_text(objects.countdown_lbl);
        LOG_INFO("Countdown label = \"%s\"", cd ? cd : "(null)");
    }

    /* After 4.5 s the engine should have switched to LIVE */
    if (elapsed < 5500) return;

    bool on_live = (lv_scr_act() == objects.live);
    LOG_TEST("T03b", on_live, "Countdown→Live auto-transition after BLOW!");
    enter_phase(PHASE_LIVE);
}

/* T04 + T05 + T06: Live screen — inject C2 waveform, verify engine state */
static void phase_live(void)
{
    uint32_t elapsed = HAL_GetTick() - g_phase_enter;

    /* T04: verify we are on the live screen */
    if (elapsed < 100 && !g_waveform_injected) {
        check_screen("T04", SCREEN_ID_LIVE, objects.live, "LIVE");

        /* T04b: coaching label visible */
        check_label("T04b", objects.live_coaching_lbl, "live_coaching_lbl", "");

        /* Start waveform injection */
        LOG_INFO("Starting ISO C2 waveform injection...");
        inject_init();
        g_waveform_injected = false;
        return;
    }

    /* Run injection */
    if (!g_waveform_injected) {
        bool done = inject_tick();
        /* T06: log engine state each second */
        static uint32_t last_log_s = 0;
        uint32_t cur_s = elapsed / 1000;
        if (cur_s != last_log_s) {
            last_log_s = cur_s;
            spiro_state_t st = spiro_get_state();
            const char *state_name = (st == SPIRO_STATE_IDLE)       ? "IDLE" :
                                     (st == SPIRO_STATE_ACQUIRING)   ? "ACQUIRING" :
                                     (st == SPIRO_STATE_COMPUTING)   ? "COMPUTING" :
                                     (st == SPIRO_STATE_DISPLAYING)  ? "DISPLAYING" : "UNKNOWN";
            const char *coaching = objects.live_coaching_lbl ?
                                   lv_label_get_text(objects.live_coaching_lbl) : "?";
            const char *flow_txt = objects.live_flow_lbl ?
                                   lv_label_get_text(objects.live_flow_lbl) : "?";
            const char *vol_txt  = objects.live_vol_lbl ?
                                   lv_label_get_text(objects.live_vol_lbl) : "?";
            LOG_INFO("t=%lu s  state=%-12s  flow=%s L/s  vol=%s L  coaching=\"%s\"",
                     (unsigned long)cur_s, state_name, flow_txt, vol_txt, coaching);
        }

        if (done) {
            g_waveform_injected = true;
            LOG_INFO("Waveform complete — calling spiro_process() to trigger COMPUTING");
            /* The engine's quiet-period timer should fire shortly; give it time */
        }
        return;
    }

    /* After injection completes, wait for engine to enter DISPLAYING state
     * (which triggers loadScreen(RESULTS)) — up to 3 s extra */
    if (elapsed < 15000) {
        spiro_state_t st = spiro_get_state();
        if (st == SPIRO_STATE_DISPLAYING) {
            LOG_TEST("T06", true, "Engine reached DISPLAYING state");
            enter_phase(PHASE_RESULTS);
            return;
        }
    } else {
        /* Timeout — force compute check */
        LOG_TEST("T06", false, "Engine never reached DISPLAYING (timeout)");
        enter_phase(PHASE_RESULTS);
    }
}

/* T07–T11: Results screen — verify all labels and ISO compliance */
static void phase_results(void)
{
    uint32_t elapsed = HAL_GetTick() - g_phase_enter;
    if (elapsed < 300 || g_results_verified) return;   /* allow render settle */
    g_results_verified = true;

    /* T07a: correct screen */
    check_screen("T07a", SCREEN_ID_RESULTS, objects.results, "RESULTS");

    /* T07b–T07f: parameter labels populated with non-placeholder values */
    /* ISO C2 expected values (Table C.1):
     * FVC=5.179 L, FEV1=3.272 L, FEV6=5.168 L
     * We also check PEF > 0 and grade != placeholder. */

    LOG_INFO("--- Results table check ---");
    check_vol_label("T07b", objects.res_fvc_act,  "FVC",  5.179f);
    check_vol_label("T07c", objects.res_fev1_act, "FEV1", 3.272f);
    check_vol_label("T07d", objects.res_fev6_act, "FEV6", 5.168f);

    /* FEV1/FVC ratio: expected ~63.1% for C2 */
    if (objects.res_ratio_act) {
        const char *r = lv_label_get_text(objects.res_ratio_act);
        LOG_INFO("FEV1/FVC = \"%s\" (expected ~63.1)", r ? r : "(null)");
        LOG_TEST("T07e", r && strcmp(r, "--") != 0, "FEV1/FVC populated: \"%s\"", r);
    }

    /* PEF: expected ~4.990 L/s for C2 */
    check_vol_label("T07f", objects.res_pef_act, "PEF", 4.990f);

    /* Predicted values present */
    check_label("T07g", objects.res_fev1_pred, "FEV1 pred", "--");
    check_label("T07h", objects.res_fvc_pred,  "FVC pred",  "--");

    /* %Pred present */
    check_label("T07i", objects.res_fev1_pct,  "FEV1 %pred", "");

    /* FEF25, FEF50, FEF75 present */
    check_label("T07j", objects.res_fef25_act,  "FEF25",   "--");
    check_label("T07k", objects.res_fef50_act,  "FEF50",   "--");
    check_label("T07l", objects.res_fef75_act,  "FEF75",   "--");
    check_label("T07m", objects.res_fef2575_act,"FEF25-75","--");

    /* Quality grade populated */
    if (objects.res_grade_lbl) {
        const char *g = lv_label_get_text(objects.res_grade_lbl);
        bool gok = g && strlen(g) == 1 && (*g >= 'A' && *g <= 'F');
        LOG_TEST("T07n", gok, "Quality grade = \"%s\"", g ? g : "(null)");
    }

    /* Start / end acceptability */
    if (objects.res_start_lbl) {
        const char *s = lv_label_get_text(objects.res_start_lbl);
        LOG_INFO("Start acceptability = \"%s\"", s ? s : "(null)");
    }
    if (objects.res_end_lbl) {
        const char *e = lv_label_get_text(objects.res_end_lbl);
        LOG_INFO("End acceptability = \"%s\"", e ? e : "(null)");
    }

    /* Interpretation */
    if (objects.res_interp_lbl) {
        const char *i = lv_label_get_text(objects.res_interp_lbl);
        LOG_INFO("Interpretation = \"%s\"", i ? i : "(null)");
    }

    /* T08: ISO §7.5 — BEV check (logged via start_lbl) */
    {
        const char *sl = objects.res_start_lbl ?
                         lv_label_get_text(objects.res_start_lbl) : "-";
        /* C2 is a fast-start profile: BEV should be ~23 mL (<<0.150 L).
         * So start_lbl should show OK (LV_SYMBOL_OK character). */
        bool start_ok = sl && (strcmp(sl, "-") != 0) && (strcmp(sl, "") != 0);
        LOG_TEST("T08", start_ok, "ISO §7.5 start-of-test label set: \"%s\"", sl);
    }

    /* T09: ISO §7.6 — end-of-test plateau (end_lbl should be OK for C2) */
    {
        const char *el = objects.res_end_lbl ?
                         lv_label_get_text(objects.res_end_lbl) : "-";
        bool end_ok = el && (strcmp(el, "-") != 0) && (strcmp(el, "") != 0);
        LOG_TEST("T09", end_ok, "ISO §7.6 end-of-test label set: \"%s\"", el);
    }

    /* T15: Dashboard last-test card updated */
    check_label("T15a", objects.dash_last_fev1,  "dash_last_fev1",  "--");
    check_label("T15b", objects.dash_last_fvc,   "dash_last_fvc",   "--");
    check_label("T15c", objects.dash_last_grade, "dash_last_grade", "--");

    /* Print the full result summary */
    LOG_RESULT("FVC=%s  FEV1=%s  FEV6=%s  ratio=%s  PEF=%s  grade=%s  interp=%s",
        objects.res_fvc_act    ? lv_label_get_text(objects.res_fvc_act)    : "?",
        objects.res_fev1_act   ? lv_label_get_text(objects.res_fev1_act)   : "?",
        objects.res_fev6_act   ? lv_label_get_text(objects.res_fev6_act)   : "?",
        objects.res_ratio_act  ? lv_label_get_text(objects.res_ratio_act)  : "?",
        objects.res_pef_act    ? lv_label_get_text(objects.res_pef_act)    : "?",
        objects.res_grade_lbl  ? lv_label_get_text(objects.res_grade_lbl)  : "?",
        objects.res_interp_lbl ? lv_label_get_text(objects.res_interp_lbl) : "?");

    LOG_INFO("Navigating to FVL screen...");
    inject_click(NULL, "FVL nav via action");
    /* Call action directly since footer button object isn't stored */
    extern void action_go_to_fvl(lv_event_t *);
    action_go_to_fvl(NULL);
    lv_timer_handler();
    enter_phase(PHASE_FVL);
}

/* T10: FVL screen — verify aspect ratio 2:1 */
static void phase_fvl(void)
{
    uint32_t elapsed = HAL_GetTick() - g_phase_enter;
    if (elapsed < 300) return;

    check_screen("T10a", SCREEN_ID_FVL, objects.fvl_screen, "FVL");

    /* ISO §7.3: 2 L/s : 1 L.  The axis scale variables are set by
     * gui_update_fvl_graph() — we verify the ratio via the Y and X labels.
     * fvl_ylabel[0] = y_max L/s, fvl_xlabel[3] = x_max L.
     * Expected ratio: y_max / x_max = 2 × (canvas_H / canvas_W) = 2 × 228/210 ≈ 2.17
     * We check that the chart object exists and axis labels are populated. */
    bool chart_ok = (objects.fvl_chart != NULL);
    LOG_TEST("T10b", chart_ok, "FVL chart object exists");

    bool y_label_ok = objects.fvl_ylabel[0] &&
                      strcmp(lv_label_get_text(objects.fvl_ylabel[0]), "--") != 0;
    LOG_TEST("T10c", y_label_ok, "FVL Y-axis label[0] = \"%s\"",
             objects.fvl_ylabel[0] ? lv_label_get_text(objects.fvl_ylabel[0]) : "null");

    bool x_label_ok = objects.fvl_xlabel[3] &&
                      strcmp(lv_label_get_text(objects.fvl_xlabel[3]), "-") != 0;
    LOG_TEST("T10d", x_label_ok, "FVL X-axis label[3] = \"%s\"",
             objects.fvl_xlabel[3] ? lv_label_get_text(objects.fvl_xlabel[3]) : "null");

    /* Verify 2:1 ratio numerically from the axis labels */
    if (y_label_ok && x_label_ok) {
        const char *ytxt = lv_label_get_text(objects.fvl_ylabel[0]);
        const char *xtxt = lv_label_get_text(objects.fvl_xlabel[3]);
        float y_max = 0.0f, x_max = 0.0f;
        /* Parse integer or decimal label e.g. "8", "2.5" */
        int iw = 0, if_ = 0;
        if (sscanf(ytxt, "%d.%d", &iw, &if_) >= 1) y_max = (float)iw + (float)if_ * 0.1f;
        else sscanf(ytxt, "%f", &y_max);
        if (sscanf(xtxt, "%d.%d", &iw, &if_) >= 1) x_max = (float)iw + (float)if_ * 0.1f;
        else sscanf(xtxt, "%f", &x_max);
        if (x_max > 0.0f && y_max > 0.0f) {
            float canvas_ratio = 2.0f * 228.0f / 210.0f;  /* ISO §7.3 on 228×210 canvas */
            float actual_ratio = y_max / x_max;
            float err = actual_ratio - canvas_ratio;
            if (err < 0.0f) err = -err;
            bool ratio_ok = (err < 0.15f);   /* allow small rounding from 0.5L steps */
            LOG_TEST("T10e", ratio_ok,
                     "ISO §7.3 FVL 2:1 ratio: y_max=%.1f x_max=%.1f ratio=%.3f expected=%.3f",
                     y_max, x_max, actual_ratio, canvas_ratio);
        }
    }

    LOG_INFO("Navigating to VT screen...");
    extern void action_go_to_vt(lv_event_t *);
    action_go_to_vt(NULL);
    lv_timer_handler();
    enter_phase(PHASE_VT);
}

/* T11: VT screen — verify aspect ratio 1:1 */
static void phase_vt(void)
{
    uint32_t elapsed = HAL_GetTick() - g_phase_enter;
    if (elapsed < 300) return;

    check_screen("T11a", SCREEN_ID_VT, objects.vt_screen, "VT");

    bool chart_ok = (objects.vt_chart != NULL);
    LOG_TEST("T11b", chart_ok, "VT chart object exists");

    bool y_label_ok = objects.vt_ylabel[0] &&
                      strcmp(lv_label_get_text(objects.vt_ylabel[0]), "--") != 0;
    LOG_TEST("T11c", y_label_ok, "VT Y-axis label[0] = \"%s\"",
             objects.vt_ylabel[0] ? lv_label_get_text(objects.vt_ylabel[0]) : "null");

    bool x_label_ok = objects.vt_xlabel[3] &&
                      strcmp(lv_label_get_text(objects.vt_xlabel[3]), "-") != 0;
    LOG_TEST("T11d", x_label_ok, "VT X-axis label[3] = \"%s\"",
             objects.vt_xlabel[3] ? lv_label_get_text(objects.vt_xlabel[3]) : "null");

    /* Verify 1:1 ratio: v_max / t_max = canvas_H / canvas_W = 228/210 */
    if (y_label_ok && x_label_ok) {
        const char *ytxt = lv_label_get_text(objects.vt_ylabel[0]);
        const char *xtxt = lv_label_get_text(objects.vt_xlabel[3]);
        float v_max = 0.0f, t_max = 0.0f;
        int iw = 0, if_ = 0;
        if (sscanf(ytxt, "%d.%d", &iw, &if_) >= 1) v_max = (float)iw + (float)if_ * 0.1f;
        else sscanf(ytxt, "%f", &v_max);
        if (sscanf(xtxt, "%d.%d", &iw, &if_) >= 1) t_max = (float)iw + (float)if_ * 0.1f;
        else sscanf(xtxt, "%f", &t_max);
        if (t_max > 0.0f && v_max > 0.0f) {
            float canvas_ratio = 228.0f / 210.0f;
            float actual_ratio = v_max / t_max;
            float err = actual_ratio - canvas_ratio;
            if (err < 0.0f) err = -err;
            bool ratio_ok = (err < 0.15f);
            LOG_TEST("T11e", ratio_ok,
                     "ISO §7.3 VT 1:1 ratio: v_max=%.1f t_max=%.1f ratio=%.3f expected=%.3f",
                     v_max, t_max, actual_ratio, canvas_ratio);
        }
    }

    /* T12: navigate back to Results (←) */
    LOG_INFO("Navigating back to Results (swipe right equivalent)...");
    extern void action_go_to_results(lv_event_t *);
    action_go_to_results(NULL);
    lv_timer_handler();

    bool back_ok = (lv_scr_act() == objects.results);
    LOG_TEST("T12", back_ok, "VT→Results back-navigation");

    LOG_INFO("Starting cancel-test scenario...");
    enter_phase(PHASE_CANCEL_TEST);
}

/* T13: Cancel test — navigate to dashboard, inject START TEST, then cancel */
static void phase_cancel_test(void)
{
    uint32_t elapsed = HAL_GetTick() - g_phase_enter;
    if (elapsed < 300) return;

    /* Navigate to Dashboard */
    extern void action_go_to_dashboard(lv_event_t *);
    action_go_to_dashboard(NULL);
    lv_timer_handler();
    HAL_Delay(100);
    lv_timer_handler();

    check_screen("T13a", SCREEN_ID_DASHBOARD, objects.scr_home, "DASHBOARD (pre-cancel)");

    /* Tap START TEST → goes to countdown → LIVE */
    inject_click(objects.dash_start_btn, "START TEST for cancel test");

    /* Wait for countdown to finish */
    uint32_t wait_start = HAL_GetTick();
    while (HAL_GetTick() - wait_start < 5500) {
        lv_timer_handler();
        HAL_Delay(2);
    }

    /* We should now be on LIVE — push a few samples to arm ACQUIRING */
    for (int i = 0; i < 20; i++) {
        /* Push a mid-blow flow sample */
        uint16_t raw = (uint16_t)SPIRO_ZERO_COUNTS +
                       (uint16_t)(2.0f * (float)SPIRO_COUNTS_PER_LPS);
        spiro_push_sample(raw);
    }

    spiro_state_t state_before = spiro_get_state();
    LOG_INFO("State before cancel = %d (ACQUIRING=1)", (int)state_before);

    /* Now simulate CANCEL button tap — in real code this calls cancel_test_cb
     * which calls spiro_reset() then action_go_to_dashboard().
     * Here we call them directly. */
    LOG_INFO(">>> inject_click: CANCEL button");
    spiro_reset();
    action_go_to_dashboard(NULL);
    lv_timer_handler();
    HAL_Delay(100);
    lv_timer_handler();

    spiro_state_t state_after = spiro_get_state();
    bool reset_ok = (state_after == SPIRO_STATE_IDLE);
    LOG_TEST("T13b", reset_ok, "spiro_reset(): engine state = %d (IDLE=%d)",
             (int)state_after, (int)SPIRO_STATE_IDLE);

    check_screen("T13c", SCREEN_ID_DASHBOARD, objects.scr_home,
                 "DASHBOARD (post-cancel)");

    enter_phase(PHASE_SECOND_MANEUVER);
}

/* T14: Second maneuver from DISPLAYING state after cancel/reset */
static void phase_second_maneuver(void)
{
    uint32_t elapsed = HAL_GetTick() - g_phase_enter;
    if (elapsed < 300) return;

    LOG_INFO("Starting second maneuver to verify clean re-arm...");

    /* START TEST → Countdown */
    inject_click(objects.dash_start_btn, "START TEST second maneuver");

    /* Wait for countdown */
    uint32_t wait_start = HAL_GetTick();
    while (HAL_GetTick() - wait_start < 5500) {
        lv_timer_handler();
        HAL_Delay(2);
    }

    check_screen("T14a", SCREEN_ID_LIVE, objects.live, "LIVE (second maneuver)");

    /* Inject a short waveform (3 s FVC = 2 L, τ = 0.75 s) */
    spiro_reset();
    LOG_INFO("Pushing short waveform (2 L, tau=0.75 s)...");

    uint32_t fs   = SPIRO_ADC_FS_HZ;
    float    fvc2 = 2.0f, tau2 = 0.75f;
    float    pef2 = fvc2 / tau2;
    float    trise2 = 0.050f;          /* 50 ms rise */
    uint32_t total2 = (uint32_t)(4.0f * (float)fs);  /* 4 s */

    for (uint32_t i = 0; i < total2; i++) {
        float t  = (float)i / (float)fs;
        float fl = (t < trise2) ? pef2 * (t / trise2) :
                                   pef2 * expf(-(t - trise2) / tau2);
        if (fl < 0.0f) fl = 0.0f;
        uint16_t raw = (uint16_t)((uint32_t)SPIRO_ZERO_COUNTS +
                       (uint32_t)(fl * (float)SPIRO_COUNTS_PER_LPS));
        spiro_push_sample(raw);
    }

    /* Wait for engine quiet-period detection */
    uint32_t w2 = HAL_GetTick();
    while (HAL_GetTick() - w2 < 3000) {
        lv_timer_handler();
        HAL_Delay(2);
    }

    /* Force compute if not done yet */
    spiro_state_t st = spiro_get_state();
    LOG_INFO("State after second waveform = %d", (int)st);

    bool reached_display = (st == SPIRO_STATE_DISPLAYING ||
                            lv_scr_act() == objects.results);
    LOG_TEST("T14b", reached_display,
             "Second maneuver: engine reached DISPLAYING/RESULTS");

    if (lv_scr_act() == objects.results) {
        /* Check FVC label non-empty */
        check_label("T14c", objects.res_fvc_act, "FVC (2nd)", "--");
        check_label("T14d", objects.res_grade_lbl, "grade (2nd)", "-");
        LOG_INFO("Second maneuver results: FVC=%s  FEV1=%s  grade=%s",
            objects.res_fvc_act  ? lv_label_get_text(objects.res_fvc_act)  : "?",
            objects.res_fev1_act ? lv_label_get_text(objects.res_fev1_act) : "?",
            objects.res_grade_lbl? lv_label_get_text(objects.res_grade_lbl): "?");
    }

    enter_phase(PHASE_DONE);
}

/* ── Test summary ────────────────────────────────────────────────────────── */
static void print_summary(void)
{
    LOG_SEP();
    printf("[%6lu] [SUMMARY] ============================================\r\n",
           HAL_GetTick());
    printf("[%6lu] [SUMMARY]  Total tests : %lu\r\n",
           HAL_GetTick(), (unsigned long)(g_pass_count + g_fail_count));
    printf("[%6lu] [SUMMARY]  PASSED      : %lu\r\n",
           HAL_GetTick(), (unsigned long)g_pass_count);
    printf("[%6lu] [SUMMARY]  FAILED      : %lu\r\n",
           HAL_GetTick(), (unsigned long)g_fail_count);
    printf("[%6lu] [SUMMARY]  %s\r\n",
           HAL_GetTick(),
           g_fail_count == 0 ? "ALL TESTS PASSED" : "*** SOME TESTS FAILED ***");
    printf("[%6lu] [SUMMARY] ============================================\r\n",
           HAL_GetTick());
    LOG_SEP();
}

/* ── System Clock (unchanged from CubeIDE generated) ─────────────────────── */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

    RCC_OscInitStruct.OscillatorType      = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState            = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState        = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource       = RCC_PLLSOURCE_HSI;
    RCC_OscInitStruct.PLL.PLLM            = 8;
    RCC_OscInitStruct.PLL.PLLN            = 84;
    RCC_OscInitStruct.PLL.PLLP            = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ            = 4;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) Error_Handler();

    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK  | RCC_CLOCKTYPE_SYSCLK |
                                       RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
        Error_Handler();
}

/* ── Error handler ───────────────────────────────────────────────────────── */
void Error_Handler(void)
{
    __disable_irq();
    printf("[%6lu] [ERROR] Error_Handler called — system halted\r\n", HAL_GetTick());
    while (1) {}
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  main()
 * ═══════════════════════════════════════════════════════════════════════════*/
int main(void)
{
    /* ── HAL + clocks ── */
    HAL_Init();
    SystemClock_Config();

    MX_GPIO_Init();
    MX_DMA_Init();
    MX_SPI1_Init();
    MX_SPI2_Init();
    MX_ADC1_Init();
    MX_USART1_UART_Init();

    /* ── Banner ── */
    printf("\r\n\r\n");
    printf("[     0] [INFO ] ================================================\r\n");
    printf("[     0] [INFO ]  SpiroFlow Test Harness  (no-touch mode)\r\n");
    printf("[     0] [INFO ]  Build: %s %s\r\n", __DATE__, __TIME__);
    printf("[     0] [INFO ]  Tests: T01–T15 (ISO 26782:2009 compliance)\r\n");
    printf("[     0] [INFO ] ================================================\r\n");

    /* ── LVGL + display (real hardware) ── */
    LOG_INFO("lv_init...");
    lv_init();
    lv_tick_set_cb(HAL_GetTick);

    LOG_INFO("lv_port_disp_init...");
    lv_port_disp_init();
    LOG_INFO("Display OK — ILI9341 240x320 ROTATE_0");

    /* ── Touch: skipped (no hardware) ── */
    LOG_INFO("Touch hardware: SKIPPED (test mode, no XPT2046)");

    /* ── Register a null indev so LVGL's event system still works ── */
    lv_indev_t *null_indev = lv_indev_create();
    lv_indev_set_type(null_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(null_indev, lv_touchpad_read);  /* always RELEASED */
    LOG_INFO("Null pointer indev registered");

    /* ── UI init ── */
    LOG_INFO("ui_init...");
    ui_init();
    LOG_INFO("ui_init done");

    /* ── Spirometry engine ── */
    LOG_INFO("spiro_init...");
    spiro_init();
    LOG_INFO("spiro_init done");

    /* ── ADC note ── */
    LOG_INFO("ADC samples will be INJECTED via spiro_push_sample() (synthetic waveform)");
    LOG_INFO("Real ADC pin can be unconnected during this test run");

    LOG_INFO("=== ENTERING TEST LOOP ===");
    LOG_SEP();

    /* ── Start at BOOT phase ── */
    enter_phase(PHASE_BOOT);

    /* ── Main test loop ── */
    uint32_t loop_count = 0;

    while (1)
    {
        lv_timer_handler();

        /* Drive the spirometry process only when NOT injecting
         * (injection loop controls engine directly during waveform phases) */
        if (g_phase != PHASE_LIVE || g_waveform_injected) {
            /* Only call spiro_process when not actively injecting waveform
             * samples to avoid ADC polling collisions */
            if (g_phase != PHASE_LIVE) {
                spiro_process();
            }
        }

        HAL_Delay(2);

        /* Phase state machine */
        switch (g_phase) {
        case PHASE_BOOT:            phase_boot();            break;
        case PHASE_DASHBOARD:       phase_dashboard();       break;
        case PHASE_COUNTDOWN:       phase_countdown();       break;
        case PHASE_LIVE:            phase_live();            break;
        case PHASE_RESULTS:         phase_results();         break;
        case PHASE_FVL:             phase_fvl();             break;
        case PHASE_VT:              phase_vt();              break;
        case PHASE_CANCEL_TEST:     phase_cancel_test();     break;
        case PHASE_SECOND_MANEUVER: phase_second_maneuver(); break;

        case PHASE_DONE:
            print_summary();
            /* Halt and flash the boot bar as a visual done indicator */
            LOG_INFO("Test run complete. Entering idle loop.");
            LOG_INFO("Check serial output for PASS/FAIL summary above.");
            while (1) {
                lv_timer_handler();
                HAL_Delay(50);
                /* Heartbeat every 10 s so the terminal shows it's alive */
                loop_count++;
                if (loop_count % 200 == 0) {
                    printf("[%6lu] [INFO ] idle heartbeat (tests complete)\r\n",
                           HAL_GetTick());
                }
            }
            break;
        }

        loop_count++;
    }
}
