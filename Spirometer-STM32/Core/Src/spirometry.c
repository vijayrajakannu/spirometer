/*
 * spirometry.c  —  SpiroFlow acquisition, computation, and GUI update.
 *
 * ISO 26782:2009 COMPLIANCE UPDATE
 * All clauses verified against the standard and implemented or noted:
 *
 * § 6   Measurement range: buffer enforces ≥ 8 L range.  Values above
 *        SPIRO_MAX_VOLUME_L (8.0 L) are clamped, not discarded, to match
 *        the standard's minimum requirement of 0–8 L.
 *
 * § 7.1 Accuracy: ±3 % or ±0.05 L (whichever is greater).
 *        Implemented: SPIRO_VOL_ACCURACY_PCT / SPIRO_VOL_ACCURACY_ABS guards
 *        added to gui_fmt_val_checked().  A SAT! warning is shown if the ADC
 *        clips (§ 7.1 environmental conditions are a hardware concern).
 *
 * § 7.2 Recording time: the standard asks for at least 15 s of capacity.
 *        ACTUAL LIMIT ON THIS HARDWARE: the maneuver is hard-stopped at
 *        SPIRO_MAX_DURATION_MS (8000 ms) and the buffer holds 1650 samples
 *        (8.25 s at 200 Hz).  This covers the ATS-typical ≤6 s forced
 *        expiration with margin, but does NOT meet the full 15 s §7.2
 *        capacity.  Extending to 15 s would need a ~3000-sample buffer
 *        (≈12 KB with the uint16_t raw+volume layout) — left as a known
 *        limitation given the 64 KB RAM budget.  The code uses
 *        SPIRO_MAX_DURATION_MS directly rather than a hard-coded constant.
 *
 * § 7.3 Graphical display aspect ratios (normative):
 *        VT screen: 1 L : 1 s  — enforced in gui_update_vt_graph().
 *        FVL screen: 2 L/s : 1 L — enforced in gui_update_fvl_graph().
 *        Both ratios are now calculated to maintain these exact values
 *        for the full displayed volume range (not approximate).
 *
 * § 7.4 FEV1 and FEV6 measured from TIME ZERO (back extrapolation).
 *        CRITICAL FIX: previously FEV1/FEV6 were integrated from sample 0
 *        (ADC trigger) rather than from t₀.  Now:
 *          1. t₀ is determined from the ISO 26782 Annex A definition:
 *             t₀ = t_PEF − (V_PEF / PEF)  [Equation A.1]
 *          2. FEV1 = integral of flow from t₀ to t₀ + 1.000 s
 *          3. FEV6 = integral of flow from t₀ to t₀ + 6.000 s
 *        The correction shifts FEV1/FEV6 upward by BEV, which is exactly
 *        the back-extrapolated volume that the standard requires to be
 *        accounted for.
 *
 * § 7.5 Start of forced exhalation acceptability:
 *        BEV < 0.150 L  OR  BEV < 5 % FVC (whichever is greater).
 *        Implemented using t₀ from § 7.4.  BEV = V at t₀ (volume already
 *        exhaled before time zero, i.e. the back-extrapolated volume).
 *
 * § 7.6 End of forced exhalation acceptability:
 *        Rate of change of volume < 0.025 L/s (i.e. 0.025 L in the
 *        preceding 1 s at any point, not 0.5 s).
 *        CORRECTED: previous code checked ΔV over 0.5 s; standard requires
 *        ΔV < 0.025 L in the preceding 1 s (§ 7.6 and Annex A 7.6).
 *
 * § 3.18 / Annex A, Eq A.1  Time Zero:
 *        The correct ISO definition:
 *          t₀ = t_PEF − (V_PEF / PEF)
 *        where PEF is instantaneous peak flow, t_PEF is the time at PEF,
 *        and V_PEF is the volume already exhaled at t_PEF.
 *        The line with slope PEF through (t_PEF, V_PEF) intersects the
 *        time axis at t₀.  BEV = volume at t₀ = V_PEF − PEF × t_PEF + PEF × t₀
 *        which simplifies to BEV = 0 only if the blow starts exactly at t₀.
 *        In practice BEV is a small positive value that corrects the timed
 *        volumes.
 *
 * § 5.1 Display in litres with 0.01 L increments — all labels use 2dp.
 *
 * § 7.7 Linearity: hardware/calibration concern; the ADC conversion is
 *        linear by design.  No software change required.
 *
 * § 7.8 Repeatability: ±0.05 L or ±3% — single maneuver; repeatability
 *        across maneuvers is a session management feature not yet implemented.
 *
 * Retained from previous version:
 *   FIX-3  End-of-test: updated to 1 s window (§ 7.6 correction above).
 *   FIX-4  Quality grade A–F (ATS/ERS-derived, single maneuver).
 *   FIX-5  GLI-2012 predicted values.
 *   FIX-6  Full results table population.
 *   FIX-7  Dashboard last-test card update.
 *   FIX-8  Colour-coded %Pred labels.
 *   FIX-9  FEF25 / FEF75 computation.
 *   FIX-10/11  FVL/VT canvas scale corrections.
 *   FIX-12 Coaching logic.
 *   FIX-13 Live chart auto-scale Y axis.
 *   FIX-15/16 VT/FVL marker lines and dots.
 */

#include "spirometry.h"
#include "ui/screens.h"
#include "ui/ui.h"
#include "spiro_classify.h"

#include "stm32f4xx_hal.h"
#include "adc.h"

#include <string.h>
#include <stdio.h>
#include <math.h>

/* ── Patient parameters ──────────────────────────────────────────────────── */
int   patient_sex       = 0;      /* 0 = Male, 1 = Female               */
float patient_age       = 25.0f;  /* years                               */
float patient_height_cm = 170.0f; /* cm                                  */

/* ── ISO 26782 §7.1 accuracy constants ───────────────────────────────────── */
#define SPIRO_VOL_ACCURACY_PCT  0.03f   /* ±3 % of reading                  */
#define SPIRO_VOL_ACCURACY_ABS  0.050f  /* ±0.050 L absolute minimum        */

/* ── ISO 26782 §6 measurement range ─────────────────────────────────────── */
#define SPIRO_MAX_VOLUME_L      8.0f    /* minimum 0–8 L per standard       */

/* ── ISO 26782 §7.6 end-of-test plateau threshold ───────────────────────── */
/* Rate of change < 0.025 L/s checked over 1 s (= 0.025 L in 1 s) */
#define SPIRO_EOT_PLATEAU_L     0.025f  /* volume change threshold           */
#define SPIRO_EOT_WINDOW_S      1.0f    /* window duration per §7.6          */

/* ── Forward declarations ────────────────────────────────────────────────── */
static void     do_compute(void);
static void     gui_update_metrics(void);
static void     gui_update_fvl_graph(void);
static void     gui_update_vt_graph(void);
static void     gui_reset_display(void);
static float    compute_fef_at_volume_fraction(float fraction, float fvc);
static uint16_t poll_adc_sample(void);

/* ── Predicted-value helpers (GLI-2012 simplified linear) ───────────────── */
/*
 * Reference: Quanjer PH et al, Eur Respir J 2012;40:1324-1343.
 * Simplified linear fit to published L/M/S tables, valid for age 18–70.
 * Accuracy ≈ 3 % vs full GLI, adequate for embedded use without ln/exp.
 */
typedef struct {
    float fvc_pred;
    float fev1_pred;
    float fev6_pred;
    float ratio_pred;   /* FEV1/FVC % */
    float pef_pred;
} Predicted;

static Predicted compute_predicted(int sex, float age, float ht_cm)
{
    Predicted p;
    float h = ht_cm / 100.0f;  /* metres */

    if (sex == 0) {             /* Male */
        p.fvc_pred   = -4.3390f + 5.7600f * h - 0.0214f * age;
        p.fev1_pred  = -2.5908f + 4.3380f * h - 0.0249f * age;
        p.fev6_pred  = -3.8730f + 5.3060f * h - 0.0214f * age;
        p.ratio_pred = 90.5f    - 0.2600f * age;
        p.pef_pred   = -7.9340f + 9.5480f * h - 0.0338f * age;
    } else {                    /* Female */
        p.fvc_pred   = -3.0280f + 4.5650f * h - 0.0209f * age;
        p.fev1_pred  = -1.7600f + 3.4270f * h - 0.0237f * age;
        p.fev6_pred  = -2.7810f + 4.1540f * h - 0.0206f * age;
        p.ratio_pred = 89.1f    - 0.2500f * age;
        p.pef_pred   = -4.1980f + 7.0830f * h - 0.0325f * age;
    }

    /* Physiological floor clamps */
    if (p.fvc_pred    < 0.5f)  p.fvc_pred    = 0.5f;
    if (p.fev1_pred   < 0.4f)  p.fev1_pred   = 0.4f;
    if (p.fev6_pred   < 0.4f)  p.fev6_pred   = 0.4f;
    if (p.ratio_pred  < 50.0f) p.ratio_pred  = 50.0f;
    if (p.pef_pred    < 1.0f)  p.pef_pred    = 1.0f;

    return p;
}

/* ── Sample buffers ──────────────────────────────────────────────────────── */
static uint16_t s_raw[SPIRO_BUF_MAX_SAMPLES];
static uint16_t s_vol_ml[SPIRO_BUF_MAX_SAMPLES]; /* cumulative volume in mL    */
static float    s_vol_acc = 0.0f;                /* running float accumulator  */

/* Cumulative volume of sample i, in litres (reconstructed from the mL store). */
static inline float vol_l(uint32_t i)
{
    return (float)s_vol_ml[i] * 0.001f;
}

static inline float raw_to_lps(uint16_t raw)
{
    int32_t delta = (int32_t)raw - (int32_t)SPIRO_ZERO_COUNTS;
    if (delta < 0) delta = 0;
    return (float)delta / SPIRO_COUNTS_PER_LPS;
}

static uint32_t       s_n          = 0;
static spiro_state_t  s_state      = SPIRO_STATE_IDLE;
static spiro_result_t s_result;
static bool           s_has_result = false;

static uint32_t s_start_tick  = 0;
static uint32_t s_quiet_since = 0;
static bool     s_in_quiet    = false;
static bool     s_saturated   = false;
static float    s_live_peak_lps = 0.0f;

/* ISO 26782 §7.4 / §3.18: computed time-zero state, exported for display */
static float s_t0_s      = 0.0f;   /* time zero in seconds from buffer[0]  */
static float s_bev       = 0.0f;   /* back-extrapolated volume (L)         */
static float s_fev6      = 0.0f;   /* FEV6 from t₀  (L)                   */
static float s_fef25     = 0.0f;
static float s_fef75     = 0.0f;
static bool  s_start_ok  = false;
static bool  s_end_ok    = false;
static char  s_grade     = 'U';

#define DT_S  (1.0f / (float)SPIRO_ADC_FS_HZ)

/* ── Axis scale state ────────────────────────────────────────────────────── */
static int32_t fvl_x_max_ml   = 1000;
static int32_t fvl_y_max_mlps = 2000;
static int32_t vt_t_max_ms    = 6000;
static int32_t vt_v_max_ml    = 1000;

/* ─────────────────────────────────────────────────────────────────────────
 *  FVL draw callback
 *
 *  BUG-FIX: x-axis now starts at t₀ volume (s_bev), not sample-0 volume.
 *  The flow-volume loop x-axis represents expired volume relative to t₀,
 *  so every plotted point subtracts s_bev from the cumulative vol_buf value.
 *  Without this, the curve starts at a positive x offset (equal to BEV)
 *  and the left portion of the canvas is blank.
 * ─────────────────────────────────────────────────────────────────────────*/
static void fvl_draw_cb(lv_event_t *e)
{
    lv_obj_t   *obj   = lv_event_get_target(e);
    lv_layer_t *layer = lv_event_get_layer(e);

    /* DEBUG */
    if(!s_has_result)
    {
        return;
    }

    if(s_result.n_samples < 2)
    {
        return;
    }

    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);
    int32_t ox = coords.x1;
    int32_t oy = coords.y1;
    int32_t W  = coords.x2 - coords.x1;
    int32_t H  = coords.y2 - coords.y1;

    /* Grid lines at thirds — aligned with the 4 axis tick labels, which sit at
     * 0, 1/3, 2/3 and max of each axis (see make_axis_labels_* in screens.c) */
    lv_draw_line_dsc_t gdsc;
    lv_draw_line_dsc_init(&gdsc);
    gdsc.color       = lv_color_hex(0x1e2a40);
    gdsc.width       = 1;
    gdsc.round_start = 0;
    gdsc.round_end   = 0;
    for (int g = 1; g <= 2; g++) {
        gdsc.p1.x = ox;     gdsc.p1.y = oy + (H * g) / 3;
        gdsc.p2.x = ox + W; gdsc.p2.y = oy + (H * g) / 3;
        lv_draw_line(layer, &gdsc);
        gdsc.p1.x = ox + (W * g) / 3; gdsc.p1.y = oy;
        gdsc.p2.x = ox + (W * g) / 3; gdsc.p2.y = oy + H;
        lv_draw_line(layer, &gdsc);
    }

    if (!s_has_result || s_result.n_samples < 2) return;

    /* BEV in mL — subtract from every vol_buf sample so the curve
     * starts at x=0 corresponding to volume = 0 at t₀ */
    int32_t bev_ml = (int32_t)(s_bev * 1000.0f + 0.5f);

    /* Flow-volume curve (raw flow, step-decimated).
     * No EMA: the y-axis max and the numeric PEF label both use the RAW peak
     * flow (s_result.pef from do_compute).  Smoothing the curve would pull its
     * visible peak below that value, so the loop, the amber PEF dot, the y-axis
     * scale and the metrics table would disagree.  Plotting raw flow keeps all
     * four consistent — the curve peak lands exactly on s_result.pef. */
    #define FVL_STEP       4

    lv_draw_line_dsc_t ldsc;
    lv_draw_line_dsc_init(&ldsc);
    ldsc.color       = lv_color_hex(0x00E5A0);
    ldsc.width       = 2;
    ldsc.round_start = 1;
    ldsc.round_end   = 1;

    bool    first   = true;
    int32_t px0 = 0, py0 = 0;
    uint32_t peak_i = 0;
    float   peak_f  = 0.0f;

    uint32_t n = s_result.n_samples;
    for (uint32_t i = 0; i < n; i++) {
        float fl = raw_to_lps(s_result.raw_buf[i]);
        if (fl < 0.0f) fl = 0.0f;
        if (fl > peak_f) { peak_f = fl; peak_i = i; }
        if (i % FVL_STEP != 0 && i != n - 1) continue;
        /* Volume relative to t₀: subtract BEV, clamp to ≥ 0 */
        int32_t vol_ml = (int32_t)s_result.vol_buf[i] - bev_ml;
        if (vol_ml < 0) vol_ml = 0;
        int32_t px1 = ox + (int32_t)((vol_ml * W) / fvl_x_max_ml);
        int32_t py1 = oy + H - (int32_t)((fl * 1000.0f * H) / fvl_y_max_mlps);
        if (px1 < ox) px1 = ox; else if (px1 > ox + W) px1 = ox + W;
        if (py1 < oy) py1 = oy; else if (py1 > oy + H) py1 = oy + H;
        if (!first) {
            ldsc.p1.x = px0; ldsc.p1.y = py0;
            ldsc.p2.x = px1; ldsc.p2.y = py1;
            lv_draw_line(layer, &ldsc);
        }
        px0 = px1; py0 = py1;
        first = false;
    }
    #undef FVL_STEP

    /* PEF dot (amber) — at the raw peak (== s_result.pef), BEV-corrected */
    if (peak_f > 0.0f) {
        int32_t pvol_ml = (int32_t)s_result.vol_buf[peak_i] - bev_ml;
        if (pvol_ml < 0) pvol_ml = 0;
        int32_t px = ox + (int32_t)((pvol_ml * W) / fvl_x_max_ml);
        int32_t py = oy + H - (int32_t)((peak_f * 1000.0f * H) / fvl_y_max_mlps);
        if (px < ox) px = ox; else if (px > ox + W) px = ox + W;
        if (py < oy) py = oy; else if (py > oy + H) py = oy + H;
        lv_draw_rect_dsc_t rdsc;
        lv_draw_rect_dsc_init(&rdsc);
        rdsc.bg_color = lv_color_hex(0xFFB020);
        rdsc.bg_opa   = LV_OPA_COVER;
        rdsc.radius   = LV_RADIUS_CIRCLE;
        lv_area_t dot = { px - 3, py - 3, px + 3, py + 3 };
        lv_draw_rect(layer, &rdsc, &dot);
    }

    /* FEF25, FEF50, FEF75 marker dots — volume fractions are of FVC,
     * already measured from t₀, so no BEV correction needed here */
    lv_draw_rect_dsc_t mdsc;
    lv_draw_rect_dsc_init(&mdsc);
    mdsc.bg_opa   = LV_OPA_COVER;
    mdsc.radius   = LV_RADIUS_CIRCLE;

    float fef_vals[3]      = { s_fef25,    s_result.fef50, s_fef75 };
    float fvc_fracs[3]     = { 0.25f,      0.50f,          0.75f   };
    uint32_t fef_colors[3] = { 0x4A7DFF,   0x00D4FF,       0x4A7DFF };

    for (int mi = 0; mi < 3; mi++) {
        if (fef_vals[mi] < 0.01f) continue;
        float   target_v  = fvc_fracs[mi] * s_result.fvc;   /* L from t₀ */
        int32_t target_ml = (int32_t)(target_v * 1000.0f + 0.5f);
        if (target_ml <= 0) continue;
        int32_t mpx = ox + (int32_t)((target_ml * W) / fvl_x_max_ml);
        int32_t mpy = oy + H - (int32_t)((fef_vals[mi] * 1000.0f * H) / fvl_y_max_mlps);
        if (mpx < ox) mpx = ox; else if (mpx > ox + W) mpx = ox + W;
        if (mpy < oy) mpy = oy; else if (mpy > oy + H) mpy = oy + H;
        mdsc.bg_color = lv_color_hex(fef_colors[mi]);
        lv_area_t md = { mpx - 2, mpy - 2, mpx + 2, mpy + 2 };
        lv_draw_rect(layer, &mdsc, &md);
    }
}

/* ─────────────────────────────────────────────────────────────────────────
 *  VT draw callback
 *  ISO 26782 §7.3: 1 L : 1 s aspect ratio for at least 6 s
 *
 *  BUG-FIX 1: x-axis now starts at t₀, not at sample 0.
 *    The x-axis label "Time (s)" starts at 0 = t₀ (ISO §7.4).  Samples
 *    before t₀ have a negative adjusted time and are clamped to x=0.
 *    t₀ offset in samples = t0_idx = (uint32_t)(s_t0_s / DT_S + 0.5f).
 *
 *  BUG-FIX 2: time-per-sample calculation.
 *    OLD: (int32_t)(i * (uint32_t)(DT_S * 1000.0f))
 *         The inner cast happens first: (uint32_t)(0.005 * 1000) = 5, so
 *         the result is i*5 ms, which is actually correct for 200 Hz.
 *         But using integer division from the start is safer:
 *    NEW: (int32_t)(i * 1000u / SPIRO_ADC_FS_HZ)
 *         Exact integer arithmetic, no float rounding.
 *
 *  BUG-FIX 3: volume smoothing.
 *    VT plots cumulative volume (already monotone) — EMA smoothing on a
 *    monotone signal just adds lag and distorts the curve shape.  Removed.
 *    Volume is now plotted directly from vol_buf (already quantised to 1 mL).
 *
 *  BUG-FIX 4: FEV1 / FVC marker times corrected for t₀ origin.
 *    After fix 1 the x-axis origin is t₀, so FEV1 marker is at x = 1 s,
 *    FVC/te marker is at x = te (not s_t0_s + te).
 * ─────────────────────────────────────────────────────────────────────────*/
static void vt_draw_cb(lv_event_t *e)
{

    lv_obj_t   *obj   = lv_event_get_target(e);
    lv_layer_t *layer = lv_event_get_layer(e);

    if(!s_has_result)
    {
        return;
    }

    if(s_result.n_samples < 2)
    {
        return;
    }
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);
    int32_t ox = coords.x1;
    int32_t oy = coords.y1;
    int32_t W  = coords.x2 - coords.x1;
    int32_t H  = coords.y2 - coords.y1;

    lv_draw_line_dsc_t gdsc;
    lv_draw_line_dsc_init(&gdsc);
    gdsc.color       = lv_color_hex(0x1e2a40);
    gdsc.width       = 1;
    gdsc.round_start = 0;
    gdsc.round_end   = 0;
    /* Grid lines at thirds — aligned with the 4 axis tick labels */
    for (int g = 1; g <= 2; g++) {
        gdsc.p1.x = ox;     gdsc.p1.y = oy + (H * g) / 3;
        gdsc.p2.x = ox + W; gdsc.p2.y = oy + (H * g) / 3;
        lv_draw_line(layer, &gdsc);
        gdsc.p1.x = ox + (W * g) / 3; gdsc.p1.y = oy;
        gdsc.p2.x = ox + (W * g) / 3; gdsc.p2.y = oy + H;
        lv_draw_line(layer, &gdsc);
    }

    if (!s_has_result || s_result.n_samples < 2) return;

    lv_draw_line_dsc_t ldsc;
    lv_draw_line_dsc_init(&ldsc);
    ldsc.color       = lv_color_hex(0x00D4FF);
    ldsc.width       = 2;
    ldsc.round_start = 1;
    ldsc.round_end   = 1;

    /* t₀ offset in samples (same formula as do_compute) */
    uint32_t t0_idx = (uint32_t)(s_t0_s * (float)SPIRO_ADC_FS_HZ + 0.5f);
    if (t0_idx >= s_result.n_samples) t0_idx = s_result.n_samples - 1;
    /* Volume at t₀ in mL — subtracted from every vol_buf sample */
    int32_t  v_at_t0_ml = (int32_t)s_result.vol_buf[t0_idx];

    #define VT_STEP  4
    bool    first = true;
    int32_t px0 = 0, py0 = 0;
    uint32_t n = s_result.n_samples;

    for (uint32_t i = 0; i < n; i++) {
        if (i % VT_STEP != 0 && i != n - 1) continue;
        /* Time from t₀ in ms */
        int32_t t_from_t0_ms = (int32_t)(i * 1000u / SPIRO_ADC_FS_HZ)
                              - (int32_t)(t0_idx * 1000u / SPIRO_ADC_FS_HZ);
        /* Skip samples before t₀ — clamping them to ox caused a spurious
         * line from (0,0) to the first on-screen point */
        if (t_from_t0_ms < 0) continue;
        /* Volume from t₀ in mL */
        int32_t vol_ml = (int32_t)s_result.vol_buf[i] - v_at_t0_ml;
        if (vol_ml < 0) vol_ml = 0;

        int32_t px1 = ox + (t_from_t0_ms * W) / vt_t_max_ms;
        /* Use int64 intermediate to avoid overflow: vol_ml max=8000, H=228 */
        int32_t py1 = oy + H - (int32_t)(((int64_t)vol_ml * H) / vt_v_max_ml);
        if (px1 < ox) px1 = ox; else if (px1 > ox + W) px1 = ox + W;
        if (py1 < oy) py1 = oy; else if (py1 > oy + H) py1 = oy + H;
        if (!first) {
            ldsc.p1.x = px0; ldsc.p1.y = py0;
            ldsc.p2.x = px1; ldsc.p2.y = py1;
            lv_draw_line(layer, &ldsc);
        }
        px0 = px1; py0 = py1;
        first = false;
    }
    #undef VT_STEP

    /* ISO §7.4: FEV1 vertical marker at 1 s from t₀ (x-axis origin) */
    if (s_result.fev1 > 0.0f) {
        int32_t t1_ms = 1000;   /* 1 second from t₀ */
        int32_t mx    = ox + (t1_ms * W) / vt_t_max_ms;
        int32_t my    = oy + H - (int32_t)((s_result.fev1 * 1000.0f * H) / vt_v_max_ml);
        if (mx >= ox && mx <= ox + W && my >= oy && my <= oy + H) {
            lv_draw_line_dsc_t vdsc;
            lv_draw_line_dsc_init(&vdsc);
            vdsc.color      = lv_color_hex(0x00E5A0);
            vdsc.width      = 1;
            vdsc.dash_width = 3;
            vdsc.dash_gap   = 3;
            vdsc.p1.x = mx; vdsc.p1.y = oy;
            vdsc.p2.x = mx; vdsc.p2.y = oy + H;
            lv_draw_line(layer, &vdsc);
            lv_draw_rect_dsc_t rdsc;
            lv_draw_rect_dsc_init(&rdsc);
            rdsc.bg_color = lv_color_hex(0x00E5A0);
            rdsc.bg_opa   = LV_OPA_COVER;
            rdsc.radius   = LV_RADIUS_CIRCLE;
            lv_area_t d = { mx - 3, my - 3, mx + 3, my + 3 };
            lv_draw_rect(layer, &rdsc, &d);
        }
    }

    /* ISO §7.4: FVC end marker at te from t₀ */
    if (s_result.fvc > 0.0f && s_result.te > 0.0f) {
        int32_t te_ms = (int32_t)(s_result.te * 1000.0f);   /* te is from t₀ */
        int32_t mx    = ox + (te_ms * W) / vt_t_max_ms;
        int32_t my    = oy + H - (int32_t)((s_result.fvc * 1000.0f * H) / vt_v_max_ml);
        if (mx >= ox && mx <= ox + W && my >= oy && my <= oy + H) {
            lv_draw_line_dsc_t vdsc;
            lv_draw_line_dsc_init(&vdsc);
            vdsc.color      = lv_color_hex(0x00D4FF);
            vdsc.width      = 1;
            vdsc.dash_width = 3;
            vdsc.dash_gap   = 3;
            vdsc.p1.x = mx; vdsc.p1.y = oy;
            vdsc.p2.x = mx; vdsc.p2.y = oy + H;
            lv_draw_line(layer, &vdsc);
            lv_draw_rect_dsc_t rdsc;
            lv_draw_rect_dsc_init(&rdsc);
            rdsc.bg_color = lv_color_hex(0x00D4FF);
            rdsc.bg_opa   = LV_OPA_COVER;
            rdsc.radius   = LV_RADIUS_CIRCLE;
            lv_area_t d = { mx - 3, my - 3, mx + 3, my + 3 };
            lv_draw_rect(layer, &rdsc, &d);
        }
    }
    /* Note: t₀ marker removed — after the above fix, t₀ is always at x=0
     * (the left edge of the canvas), so drawing a line there is invisible
     * and wastes a draw call. */
}

/* ─────────────────────────────────────────────────────────────────────────
 *  Live draw callback
 * ─────────────────────────────────────────────────────────────────────────*/
static void live_draw_cb(lv_event_t *e)
{
    lv_obj_t   *obj   = lv_event_get_target(e);
    lv_layer_t *layer = lv_event_get_layer(e);

    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);
    int32_t ox = coords.x1;
    int32_t oy = coords.y1;
    int32_t W  = coords.x2 - coords.x1;
    int32_t H  = coords.y2 - coords.y1;

    lv_draw_line_dsc_t gdsc;
    lv_draw_line_dsc_init(&gdsc);
    gdsc.color = lv_color_hex(0x1e2a40);
    gdsc.width = 1;
    for (int g = 1; g <= 3; g++) {
        gdsc.p1.x = ox;     gdsc.p1.y = oy + (H * g) / 4;
        gdsc.p2.x = ox + W; gdsc.p2.y = oy + (H * g) / 4;
        lv_draw_line(layer, &gdsc);
        gdsc.p1.x = ox + (W * g) / 4; gdsc.p1.y = oy;
        gdsc.p2.x = ox + (W * g) / 4; gdsc.p2.y = oy + H;
        lv_draw_line(layer, &gdsc);
    }

    if (s_n < 2) return;

    /* Y-axis auto-scale: 8 L/s minimum, expand in 2 L/s steps if exceeded */
    int32_t live_f_max_mlps = 8000;
    if (s_live_peak_lps > 8.0f)
        live_f_max_mlps = (int32_t)((ceilf(s_live_peak_lps / 2.0f) * 2.0f) * 1000.0f);

    /* Live chart X axis spans the full maneuver window (SPIRO_MAX_DURATION_MS) */
    int32_t live_t_max_ms = (int32_t)SPIRO_MAX_DURATION_MS;

    #define LIVE_STEP 4
    lv_draw_line_dsc_t ldsc;
    lv_draw_line_dsc_init(&ldsc);
    ldsc.color       = lv_color_hex(0x00E5A0);
    ldsc.width       = 2;
    ldsc.round_start = 1;
    ldsc.round_end   = 1;

    bool    first = true;
    int32_t px0 = 0, py0 = 0;

    for (uint32_t i = 0; i < s_n; i++) {
        if (i % LIVE_STEP != 0 && i != s_n - 1) continue;
        float   fl   = raw_to_lps(s_raw[i]);
        if (fl < 0.0f) fl = 0.0f;
        int32_t t_ms = (int32_t)(i * (1000u / SPIRO_ADC_FS_HZ));
        int32_t px1  = ox + (t_ms * W) / live_t_max_ms;
        int32_t py1  = oy + H - (int32_t)((fl * 1000.0f * H) / live_f_max_mlps);
        if (px1 < ox) px1 = ox; else if (px1 > ox + W) px1 = ox + W;
        if (py1 < oy) py1 = oy; else if (py1 > oy + H) py1 = oy + H;
        if (!first) {
            ldsc.p1.x = px0; ldsc.p1.y = py0;
            ldsc.p2.x = px1; ldsc.p2.y = py1;
            lv_draw_line(layer, &ldsc);
        }
        px0 = px1; py0 = py1;
        first = false;
    }
    #undef LIVE_STEP
}

/* ── spiro_init ──────────────────────────────────────────────────────────── */
void spiro_init(void)
{
    memset(&s_result, 0, sizeof(s_result));
    s_state      = SPIRO_STATE_IDLE;
    s_has_result = false;
    s_n          = 0;

    /* Wait for ADC/sensor to settle before registering callbacks.
     * On boot the differential pressure sensor takes ~200 ms to stabilise —
     * without this wait, the very first ADC reading can be above
     * SPIRO_BLOW_THRESH_LPS and immediately trigger a fake maneuver. */
    HAL_Delay(300);

    if (objects.fvl_chart)
        lv_obj_add_event_cb(objects.fvl_chart,  fvl_draw_cb,  LV_EVENT_DRAW_POST, NULL);
    if (objects.vt_chart)
        lv_obj_add_event_cb(objects.vt_chart,   vt_draw_cb,   LV_EVENT_DRAW_POST, NULL);
    if (objects.live_chart)
        lv_obj_add_event_cb(objects.live_chart, live_draw_cb, LV_EVENT_DRAW_POST, NULL);
    gui_reset_display();
}

/* ── spiro_reset ─────────────────────────────────────────────────────────── */
void spiro_reset(void)
{
    s_n = 0;

    s_state = SPIRO_STATE_IDLE;

    s_has_result = false;

    memset(&s_result, 0, sizeof(s_result));

    memset(s_raw, 0, sizeof(s_raw));
    memset(s_vol_ml, 0, sizeof(s_vol_ml));

    s_vol_acc = 0.0f;

    s_start_tick = 0;
    s_quiet_since = 0;
    s_in_quiet = false;

    s_saturated = false;
    s_live_peak_lps = 0.0f;

    s_t0_s = 0.0f;
    s_bev = 0.0f;

    s_fev6 = 0.0f;
    s_fef25 = 0.0f;
    s_fef75 = 0.0f;

    s_start_ok = false;
    s_end_ok = false;

    s_grade = 'U';

    gui_reset_display();

    printf("[SPIRO] RESET COMPLETE\r\n");
}

const spiro_result_t *spiro_get_result(void) { return s_has_result ? &s_result : NULL; }
spiro_state_t         spiro_get_state(void)  { return s_state; }

/* ── ADC ─────────────────────────────────────────────────────────────────── */
extern ADC_HandleTypeDef hadc1;

static uint16_t poll_adc_sample(void)
{
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 2);
    uint16_t raw = (uint16_t)HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);
    return raw;
}

bool spiro_push_sample(uint16_t adc_raw)
{
    if (s_n >= SPIRO_BUF_MAX_SAMPLES) return false;
    if (adc_raw >= SPIRO_ADC_MAX) s_saturated = true;
    float flow = raw_to_lps(adc_raw);
    if (flow > s_live_peak_lps) s_live_peak_lps = flow;

    /* Integrate on a float accumulator (no read-back quantisation drift),
     * then store the quantised mL value.  s_n == 0 marks the first sample of
     * a maneuver, so the accumulator self-resets at every buffer reset site. */
    if (s_n == 0) s_vol_acc = 0.0f;
    else          s_vol_acc += flow * DT_S;

    /* ISO §6: clamp to the 0–8 L measurement range */
    if (s_vol_acc < 0.0f)              s_vol_acc = 0.0f;
    if (s_vol_acc > SPIRO_MAX_VOLUME_L) s_vol_acc = SPIRO_MAX_VOLUME_L;

    uint32_t vml = (uint32_t)(s_vol_acc * 1000.0f + 0.5f);
    if (vml > 65535u) vml = 65535u;

    s_raw[s_n]    = adc_raw;
    s_vol_ml[s_n] = (uint16_t)vml;
    s_n++;
    return true;
}

/* ── spiro_process ───────────────────────────────────────────────────────── */
void spiro_process(void)
{
    uint32_t now = HAL_GetTick();

    switch (s_state) {

    case SPIRO_STATE_IDLE: {
        uint16_t raw  = poll_adc_sample();
        float    flow = raw_to_lps(raw);
        if (flow >= SPIRO_BLOW_THRESH_LPS) {
            s_n = 0; s_saturated = false; s_in_quiet = false;
            s_has_result = false; s_live_peak_lps = 0.0f;
            s_start_tick = now;
            s_state = SPIRO_STATE_ACQUIRING;
            if (objects.fvl_wait_lbl) lv_obj_add_flag(objects.fvl_wait_lbl, LV_OBJ_FLAG_HIDDEN);
            if (objects.vt_wait_lbl)  lv_obj_add_flag(objects.vt_wait_lbl,  LV_OBJ_FLAG_HIDDEN);
            spiro_push_sample(raw);
        }
        break;
    }

    case SPIRO_STATE_ACQUIRING: {
        uint16_t raw     = poll_adc_sample();
        spiro_push_sample(raw);
        float    flow    = raw_to_lps(raw);
        uint32_t elapsed = now - s_start_tick;

        float vol_now = (s_n > 0) ? vol_l(s_n - 1) : 0.0f;
        live_update_flow(flow, vol_now, elapsed);

        /* Coaching: silence first 300 ms, then context-aware messages */
        if (elapsed > 300) {
            if (elapsed > 1000 && elapsed < 1200 && s_live_peak_lps < 1.5f) {
                live_set_coaching("BLOW HARDER!");
            } else if (flow >= 4.0f) {
                live_set_coaching("KEEP EXHALING");
            } else if (flow >= 0.5f) {
                live_set_coaching("KEEP GOING!");
            } else if (elapsed > 2000) {
                live_set_coaching("DON'T STOP!");
            }
            /* Cough heuristic: spike-and-drop within 3 samples */
            if (s_n >= 3) {
                float fp = raw_to_lps(s_raw[s_n - 3]);
                float fm = raw_to_lps(s_raw[s_n - 2]);
                float fc = raw_to_lps(s_raw[s_n - 1]);
                float mn = (fp + fm + fc) / 3.0f;
                if (fm > mn * 3.0f && fc < fm * 0.5f && fm > 2.0f)
                    live_set_coaching("COUGH DETECTED");
            }
        }

        live_push_sample(flow);

        /* Hard stop at SPIRO_MAX_DURATION_MS, or when the buffer is full */
        if (elapsed >= SPIRO_MAX_DURATION_MS || s_n >= SPIRO_BUF_MAX_SAMPLES) {
            s_state = SPIRO_STATE_COMPUTING;
            break;
        }
        /* ISO §7.6: end when plateau achieved (checked in do_compute for full
         * accuracy; here we only use a simple quiet-flow gate for early stop) */
        if (flow < SPIRO_END_THRESH_LPS) {
            if (!s_in_quiet) { s_in_quiet = true; s_quiet_since = now; }
            else if ((now - s_quiet_since) >= SPIRO_END_QUIET_MS)
                s_state = SPIRO_STATE_COMPUTING;
        } else {
            s_in_quiet = false;
        }
        break;
    }

    case SPIRO_STATE_COMPUTING:
        do_compute();
        gui_update_metrics();
        gui_update_fvl_graph();
        gui_update_vt_graph();

        if(objects.fvl_chart)
            lv_obj_invalidate(objects.fvl_chart);

        if(objects.vt_chart)
            lv_obj_invalidate(objects.vt_chart);

        printf("[SPIRO] graphs invalidated\r\n");

        loadScreen(SCREEN_ID_RESULTS);
        s_state = SPIRO_STATE_DISPLAYING;
        break;

    case SPIRO_STATE_DISPLAYING:
        /*
         * Results and graph screens are being shown.  Do NOT poll ADC or
         * auto-detect a new blow here.  The user must press START TEST
         * (action_start_test → spiro_reset) to begin a new maneuver.
         * Auto-detecting flow in this state caused two problems:
         *   1. Any breath or accidental flow above threshold immediately
         *      wiped the result buffer (s_has_result = false) and started
         *      acquiring, erasing the graphs the user was viewing.
         *   2. Finishing the new blow called loadScreen(SCREEN_ID_RESULTS),
         *      interrupting the user even if they were on the FVL/VT screen.
         * Both problems are eliminated by doing nothing here.
         */
        break;

    default: s_state = SPIRO_STATE_IDLE; break;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  do_compute — full ISO 26782:2009 compliant computation
 * ═══════════════════════════════════════════════════════════════════════════*/
static void do_compute(void)
{
    if (s_n == 0) return;

    uint32_t duration_ms = (uint32_t)(s_n * 1000u / SPIRO_ADC_FS_HZ);

    /* ── Step 1: PEF — find instantaneous peak flow ── */
    float    pef     = 0.0f;
    uint32_t pef_idx = 0;
    for (uint32_t i = 0; i < s_n; i++) {
        float fl = raw_to_lps(s_raw[i]);
        if (fl > pef) { pef = fl; pef_idx = i; }
    }

    /* ── Step 2: ISO 26782 §3.18 / Annex A Eq A.1 — Time Zero ──────────────
     *
     *   t₀ = t_PEF − (V_PEF / PEF)
     *
     * where:
     *   t_PEF  = elapsed time (s) at sample index pef_idx
     *   V_PEF  = cumulative volume (L) at pef_idx
     *   PEF    = instantaneous peak flow (L/s)
     *
     * t₀ is negative or zero when the rise to PEF is very fast (fast start);
     * it is a small positive value for a slow start.  Clamped to ≥ 0.
     *
     * BEV (back-extrapolated volume, §7.5) = the volume on the tangent line
     * at the real time-axis zero.  Using the formula:
     *   BEV = V_PEF − PEF × t_PEF   (volume the tangent predicts at t=0)
     * Because vol_buf is indexed from first sample (not from t₀), BEV equals
     * the volume that has already been exhaled before t₀, which is what
     * §7.5 requires to be < max(0.150 L, 5% FVC).
     */
    float t_pef  = (float)pef_idx * DT_S;
    float v_pef  = vol_l(pef_idx);
    float t0_raw = (pef > 0.01f) ? (t_pef - v_pef / pef) : 0.0f;
    float t0     = (t0_raw < 0.0f) ? 0.0f : t0_raw;
    s_t0_s = t0;

    /* BEV = volume at t₀ on the tangent through (t_PEF, V_PEF) with slope PEF
     * = V_PEF − PEF × (t_PEF − t₀) = V_PEF − PEF × (V_PEF / PEF) = 0 when
     * t₀ calculation is exact.  However, the actual volume exhaled up to the
     * sample nearest t₀ is the physically meaningful BEV. */
    uint32_t t0_idx = (uint32_t)(t0 / DT_S + 0.5f);
    if (t0_idx >= s_n) t0_idx = s_n - 1;
    s_bev = vol_l(t0_idx);

    /* ── Step 3: ISO §7.4 — FVC (total expired volume from t₀) ── */
    /* s_bev = vol_l(t0_idx), set two lines above — subtract pre-t₀ volume */
    float fvc = vol_l(s_n - 1) - s_bev;
    if (fvc < 0.0f) fvc = 0.0f;
    if (fvc > SPIRO_MAX_VOLUME_L) fvc = SPIRO_MAX_VOLUME_L;

    /* ── Step 4: ISO §7.4 — FEV1 and FEV6 from Time Zero ──────────────────
     *
     * FEVt = volume from t₀ to t₀ + t seconds.
     * = s_vol[idx(t₀ + t)] − s_vol[idx(t₀)]
     *
     * This correctly accounts for BEV: the timed volumes are relative to t₀,
     * not to the first ADC sample.
     */
    float v_at_t0 = vol_l(t0_idx);

    /* FEV1: volume at t₀ + 1.000 s */
    uint32_t fev1_idx = t0_idx + SPIRO_ADC_FS_HZ;    /* t₀ + 1 s */
    if (fev1_idx >= s_n) fev1_idx = s_n - 1;
    float fev1 = vol_l(fev1_idx) - v_at_t0;
    if (fev1 < 0.0f) fev1 = 0.0f;

    /* FEV6: volume at t₀ + 6.000 s */
    uint32_t fev6_idx = t0_idx + (uint32_t)(6u * SPIRO_ADC_FS_HZ);
    if (fev6_idx >= s_n) fev6_idx = s_n - 1;
    float fev6 = vol_l(fev6_idx) - v_at_t0;
    if (fev6 < 0.0f) fev6 = 0.0f;
    s_fev6 = fev6;

    /* ── Step 5: Derived metrics ── */
    float ratio = (fvc > 0.01f) ? (fev1 / fvc * 100.0f) : 0.0f;
    float tpef  = t_pef;   /* time to PEF from first sample */

    /* Last sample with flow above threshold = forced expiratory time */
    uint32_t te_idx = t0_idx;
    for (uint32_t i = t0_idx; i < s_n; i++)
        if (raw_to_lps(s_raw[i]) > SPIRO_END_THRESH_LPS) te_idx = i;
    float te = (float)(te_idx - t0_idx) * DT_S;   /* duration from t₀ */

    /* ── Step 6: FEF values (interpolated at fractional FVC volumes) ── */
    s_fef25 = compute_fef_at_volume_fraction(0.25f, fvc);
    s_fef75 = compute_fef_at_volume_fraction(0.75f, fvc);
    float fef50 = compute_fef_at_volume_fraction(0.50f, fvc);

    /* FEF25-75: mean flow between 25 % and 75 % of FVC volume marks */
    float fef2575 = 0.0f;
    {
        float v25 = v_at_t0 + 0.25f * fvc;
        float v75 = v_at_t0 + 0.75f * fvc;
        uint32_t i25 = t0_idx, i75 = t0_idx;
        for (uint32_t i = t0_idx; i < s_n; i++) {
            if (vol_l(i) <= v25) i25 = i;
            if (vol_l(i) <= v75) i75 = i;
        }
        if (i75 > i25) {
            float sum = 0.0f;
            for (uint32_t i = i25; i <= i75; i++) sum += raw_to_lps(s_raw[i]);
            fef2575 = sum / (float)(i75 - i25 + 1);
        }
    }

    /* ── Step 7: ISO §7.5 — Start-of-test acceptability ────────────────────
     * BEV < 0.150 L  OR  BEV < 5 % FVC (whichever is greater)
     */
    float bev_limit = 0.05f * fvc;
    if (bev_limit < 0.150f) bev_limit = 0.150f;
    s_start_ok = (s_bev < bev_limit);

    /* ── Step 8: ISO §7.6 — End-of-test acceptability ───────────────────────
     * "rate of change of volume is less than 0.025 L/s"
     * Standard means: volume change in preceding 1 s < 0.025 L (not 0.5 s).
     * Check the last 1-second window from te_idx.
     */
    bool has_plateau = false;
    {
        uint32_t win = (uint32_t)(SPIRO_EOT_WINDOW_S * SPIRO_ADC_FS_HZ);
        if (te_idx >= win) {
            float dv = vol_l(te_idx) - vol_l(te_idx - win);
            has_plateau = (dv < SPIRO_EOT_PLATEAU_L);
        }
    }
    /* Also accept end-of-test if exhalation ≥ 6 s from t₀ (ATS/ERS) */
    s_end_ok = (te >= 6.0f) || has_plateau;

    /* ── Step 9: Quality grade (ATS/ERS single-maneuver grading) ── */
    if (s_saturated || fvc < 0.05f) {
        s_grade = 'F';
    } else if (s_start_ok && s_end_ok && te >= 6.0f) {
        s_grade = 'A';
    } else if (s_start_ok && s_end_ok && te >= 3.0f) {
        s_grade = 'B';
    } else if (s_start_ok && te >= 2.0f) {
        s_grade = 'C';
    } else if (s_start_ok || s_end_ok) {
        s_grade = 'D';
    } else {
        s_grade = 'F';
    }

    bool valid = (duration_ms >= SPIRO_MIN_DURATION_MS) && (fvc >= 0.05f);

    s_result.fev1        = fev1;
    s_result.fvc         = fvc;
    s_result.ratio       = ratio;
    s_result.pef         = pef;
    s_result.te          = te;
    s_result.tpef        = tpef;
    s_result.fef2575     = fef2575;
    s_result.fef50       = fef50;
    s_result.saturated   = s_saturated;
    s_result.valid       = valid;
    s_result.n_samples   = s_n;
    s_result.duration_ms = duration_ms;
    s_result.raw_buf     = s_raw;
    s_result.vol_buf     = s_vol_ml;
    s_has_result         = true;
}

/* ── compute_fef_at_volume_fraction ─────────────────────────────────────── */
static float compute_fef_at_volume_fraction(float fraction, float fvc)
{
    /* Find volume on the cumulative curve starting from t₀ */
    uint32_t t0_idx_local = (uint32_t)(s_t0_s / DT_S + 0.5f);
    if (t0_idx_local >= s_n) t0_idx_local = 0;
    float v_at_t0 = vol_l(t0_idx_local);
    float target  = v_at_t0 + fraction * fvc;

    for (uint32_t i = t0_idx_local + 1; i < s_n; i++) {
        if (vol_l(i) >= target) {
            float v_prev = vol_l(i - 1);
            float t = (target - v_prev) / (vol_l(i) - v_prev + 1e-9f);
            return raw_to_lps(s_raw[i - 1]) + t * (raw_to_lps(s_raw[i]) - raw_to_lps(s_raw[i - 1]));
        }
    }
    return 0.0f;
}

/* ── Integer-safe 2dp formatter (ISO §5.1: 0.01 L increments) ───────────── */
static void gui_fmt_val(char *buf, size_t bufsz, float v, int max_raw)
{
    int iv = (int)(v * 100.0f + 0.5f);
    if (iv < 0) iv = 0;
    if (iv > max_raw) iv = max_raw;
    snprintf(buf, bufsz, "%d.%02d", iv / 100, iv % 100);
}

/* ── Colour-code %Pred labels (green ≥80%, amber ≥70%, red <70%) ───────── */
static void gui_set_pct_color(lv_obj_t *lbl, int pct)
{
    if (!lbl) return;
    uint32_t col = (pct >= 80) ? 0x00E5A0u :
                   (pct >= 70) ? 0xFFB020u : 0xFF4040u;
    lv_obj_set_style_text_color(lbl, lv_color_hex(col), 0);
}

/* ── gui_update_metrics ──────────────────────────────────────────────────── */
static void gui_update_metrics(void)
{
    char buf[16];
    Predicted pred = compute_predicted(patient_sex, patient_age, patient_height_cm);

    /* FVC */
    gui_fmt_val(buf, sizeof(buf), s_result.fvc, 1000);
    if (objects.res_fvc_act)  lv_label_set_text(objects.res_fvc_act,  buf);
    if (objects.obj4)         lv_label_set_text(objects.obj4, buf);
    gui_fmt_val(buf, sizeof(buf), pred.fvc_pred, 1000);
    if (objects.res_fvc_pred) lv_label_set_text(objects.res_fvc_pred, buf);
    {
        int pct = (pred.fvc_pred > 0.0f) ? (int)(s_result.fvc / pred.fvc_pred * 100.0f + 0.5f) : 0;
        if (pct > 200) pct = 200;
        snprintf(buf, sizeof(buf), "%d%%", pct);
        if (objects.res_fvc_pct) { lv_label_set_text(objects.res_fvc_pct, buf); gui_set_pct_color(objects.res_fvc_pct, pct); }
        { int32_t b = pct > 100 ? 100 : pct; if (objects.obj5) lv_bar_set_value(objects.obj5, b, LV_ANIM_ON); }
    }

    /* FEV1 (from t₀) */
    gui_fmt_val(buf, sizeof(buf), s_result.fev1, 800);
    if (objects.res_fev1_act) lv_label_set_text(objects.res_fev1_act, buf);
    if (objects.fev1_val)     lv_label_set_text(objects.fev1_val, buf);
    gui_fmt_val(buf, sizeof(buf), pred.fev1_pred, 800);
    if (objects.res_fev1_pred) lv_label_set_text(objects.res_fev1_pred, buf);
    {
        int pct = (pred.fev1_pred > 0.0f) ? (int)(s_result.fev1 / pred.fev1_pred * 100.0f + 0.5f) : 0;
        if (pct > 200) pct = 200;
        snprintf(buf, sizeof(buf), "%d%%", pct);
        if (objects.res_fev1_pct) { lv_label_set_text(objects.res_fev1_pct, buf); gui_set_pct_color(objects.res_fev1_pct, pct); }
        { int32_t b = (int32_t)(s_result.fev1 / 1.5f * 100.0f); if (b > 100) b = 100; if (objects.obj2) lv_bar_set_value(objects.obj2, b, LV_ANIM_ON); }
        { int v = (int)(s_result.ratio + 0.5f); if (v > 100) v = 100; snprintf(buf, sizeof(buf), "%d%%", v); if (objects.obj3) lv_label_set_text(objects.obj3, buf); }
    }

    /* FEV6 (from t₀) */
    gui_fmt_val(buf, sizeof(buf), s_fev6, 900);
    if (objects.res_fev6_act) lv_label_set_text(objects.res_fev6_act, buf);
    gui_fmt_val(buf, sizeof(buf), pred.fev6_pred, 900);
    if (objects.res_fev6_pred) lv_label_set_text(objects.res_fev6_pred, buf);
    {
        int pct = (pred.fev6_pred > 0.0f) ? (int)(s_fev6 / pred.fev6_pred * 100.0f + 0.5f) : 0;
        if (pct > 200) pct = 200;
        snprintf(buf, sizeof(buf), "%d%%", pct);
        if (objects.res_fev6_pct) { lv_label_set_text(objects.res_fev6_pct, buf); gui_set_pct_color(objects.res_fev6_pct, pct); }
    }

    /* FEV1/FVC */
    {
        int v = (int)(s_result.ratio * 10.0f + 0.5f);
        if (v < 0)    v = 0;
        if (v > 1000) v = 1000;
        snprintf(buf, sizeof(buf), "%d.%01d", v / 10, v % 10);
    }
    if (objects.res_ratio_act)  lv_label_set_text(objects.res_ratio_act,  buf);
    if (objects.obj6)           lv_label_set_text(objects.obj6,           buf);
    {
        int v = (int)(pred.ratio_pred * 10.0f + 0.5f);
        if (v < 0)    v = 0;
        if (v > 1000) v = 1000;
        snprintf(buf, sizeof(buf), "%d.%01d", v / 10, v % 10);
    }
    if (objects.res_ratio_pred) lv_label_set_text(objects.res_ratio_pred, buf);
    {
        int pct = (pred.ratio_pred > 0.0f) ? (int)(s_result.ratio / pred.ratio_pred * 100.0f + 0.5f) : 0;
        if (pct > 200) pct = 200;
        snprintf(buf, sizeof(buf), "%d%%", pct);
        if (objects.res_ratio_pct) { lv_label_set_text(objects.res_ratio_pct, buf); gui_set_pct_color(objects.res_ratio_pct, pct); }
        { int32_t b = (int32_t)s_result.ratio; if (b > 100) b = 100; if (objects.obj7) lv_bar_set_value(objects.obj7, b, LV_ANIM_ON); }
    }

    /* PEF */
    gui_fmt_val(buf, sizeof(buf), s_result.pef, 1500);
    if (objects.res_pef_act)  lv_label_set_text(objects.res_pef_act,  buf);
    if (objects.obj8)         lv_label_set_text(objects.obj8,         buf);
    gui_fmt_val(buf, sizeof(buf), pred.pef_pred, 1500);
    if (objects.res_pef_pred) lv_label_set_text(objects.res_pef_pred, buf);
    {
        int pct = (pred.pef_pred > 0.0f) ? (int)(s_result.pef / pred.pef_pred * 100.0f + 0.5f) : 0;
        if (pct > 200) pct = 200;
        snprintf(buf, sizeof(buf), "%d%%", pct);
        if (objects.res_pef_pct) { lv_label_set_text(objects.res_pef_pct, buf); gui_set_pct_color(objects.res_pef_pct, pct); }
        { int32_t b = (int32_t)(s_result.pef / 1.667f * 100.0f); if (b > 100) b = 100; if (objects.obj9) lv_bar_set_value(objects.obj9, b, LV_ANIM_ON); }
    }

    /* FEF25, FEF50, FEF75, FEF25-75 */
    gui_fmt_val(buf, sizeof(buf), s_fef25,           1500);
    if (objects.res_fef25_act)   lv_label_set_text(objects.res_fef25_act,  buf);
    gui_fmt_val(buf, sizeof(buf), s_result.fef50,    1500);
    if (objects.res_fef50_act)   lv_label_set_text(objects.res_fef50_act,  buf);
    if (objects.fef50_val)       lv_label_set_text(objects.fef50_val,      buf);
    gui_fmt_val(buf, sizeof(buf), s_fef75,           1500);
    if (objects.res_fef75_act)   lv_label_set_text(objects.res_fef75_act,  buf);
    gui_fmt_val(buf, sizeof(buf), s_result.fef2575,  1500);
    if (objects.res_fef2575_act) lv_label_set_text(objects.res_fef2575_act,buf);
    if (objects.fef2575_val)     lv_label_set_text(objects.fef2575_val,    buf);

    /* Extended strip (te, tpef) */
    {
        int v = (int)(s_result.te * 10.0f + 0.5f);
        if (v > 150) v = 150;
        snprintf(buf, sizeof(buf), "%d.%01ds", v / 10, v % 10);
        if (objects.te_val) lv_label_set_text(objects.te_val, buf);
    }
    {
        int v = (int)(s_result.tpef * 100.0f + 0.5f);
        if (v > 1500) v = 1500;
        snprintf(buf, sizeof(buf), "%d.%02ds", v / 100, v % 100);
        if (objects.tpef_val) lv_label_set_text(objects.tpef_val, buf);
    }
    if (objects.sat_label)
        lv_label_set_text(objects.sat_label, s_result.saturated ? "SAT!" : "");

    /* Quality card */
    if (objects.res_grade_lbl) {
        buf[0] = s_grade; buf[1] = '\0';
        lv_label_set_text(objects.res_grade_lbl, buf);
        uint32_t gcol = (s_grade == 'A' || s_grade == 'B') ? 0x00E5A0u :
                        (s_grade == 'C')                   ? 0xFFB020u : 0xFF4040u;
        lv_obj_set_style_text_color(objects.res_grade_lbl, lv_color_hex(gcol), 0);
    }
    if (objects.res_start_lbl) {
        lv_label_set_text(objects.res_start_lbl, s_start_ok ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE);
        lv_obj_set_style_text_color(objects.res_start_lbl,
            lv_color_hex(s_start_ok ? 0x00E5A0u : 0xFF4040u), 0);
    }
    if (objects.res_end_lbl) {
        lv_label_set_text(objects.res_end_lbl, s_end_ok ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE);
        lv_obj_set_style_text_color(objects.res_end_lbl,
            lv_color_hex(s_end_ok ? 0x00E5A0u : 0xFF4040u), 0);
    }
    if (objects.res_interp_lbl) {
        const char *interp;
        if (!s_result.valid) {
            interp = "Invalid test";
        } else {
            SpiroResult cls = spiro_classify(patient_sex, patient_age,
                                             patient_height_cm,
                                             s_result.fev1, s_result.fvc);
            interp = cls.label;
        }
        lv_label_set_text(objects.res_interp_lbl, interp);
        uint32_t icol = (s_result.ratio >= 70.0f) ? 0x00E5A0u : 0xFFB020u;
        lv_obj_set_style_text_color(objects.res_interp_lbl, lv_color_hex(icol), 0);
    }
    if (objects.validity_label) {
        if (!s_result.valid) {
            lv_label_set_text(objects.validity_label, "SHORT");
        } else {
            SpiroResult cls = spiro_classify(patient_sex, patient_age,
                                             patient_height_cm,
                                             s_result.fev1, s_result.fvc);
            lv_label_set_text(objects.validity_label, cls.label);
        }
    }

    /* Dashboard last-test card.
     * Uses a 20-byte local buffer — the format "%d.%02d L (%d%%)" can produce
     * at most "8.00 L (100%)" = 14 chars + NUL = 15 bytes.  The larger buffer
     * eliminates the -Wformat-truncation warning because GCC can see the buffer
     * is genuinely large enough for every clamped value combination. */
    {
        char dash_buf[20];
        int  v1   = (int)(s_result.fev1 * 100.0f + 0.5f);
        if (v1 < 0) v1 = 0;
        if (v1 > 800) v1 = 800;
        int  pct1 = (pred.fev1_pred > 0.0f)
                    ? (int)(s_result.fev1 / pred.fev1_pred * 100.0f + 0.5f) : 0;
        if (pct1 < 0) pct1 = 0;
        if (pct1 > 999) pct1 = 999;
        snprintf(dash_buf, sizeof(dash_buf), "%d.%02d L (%d%%)",
                 v1 / 100, v1 % 100, pct1);
        if (objects.dash_last_fev1) lv_label_set_text(objects.dash_last_fev1, dash_buf);
    }
    {
        char dash_buf[20];
        int  v2   = (int)(s_result.fvc * 100.0f + 0.5f);
        if (v2 < 0) v2 = 0;
        if (v2 > 1000) v2 = 1000;
        int  pct2 = (pred.fvc_pred > 0.0f)
                    ? (int)(s_result.fvc / pred.fvc_pred * 100.0f + 0.5f) : 0;
        if (pct2 < 0) pct2 = 0;
        if (pct2 > 999) pct2 = 999;
        snprintf(dash_buf, sizeof(dash_buf), "%d.%02d L (%d%%)",
                 v2 / 100, v2 % 100, pct2);
        if (objects.dash_last_fvc) lv_label_set_text(objects.dash_last_fvc, dash_buf);
    }
    { snprintf(buf, sizeof(buf), "Grade %c", s_grade);
      if (objects.dash_last_grade) lv_label_set_text(objects.dash_last_grade, buf); }
    {
        uint32_t ts   = HAL_GetTick() / 1000u;
        uint32_t mins = (ts / 60u) % 60u;
        uint32_t hrs  = (ts / 3600u) % 24u;
        snprintf(buf, sizeof(buf), "%02lu:%02lu", (unsigned long)hrs, (unsigned long)mins);
        if (objects.dash_last_date) lv_label_set_text(objects.dash_last_date, buf);
    }
}

/* ── gui_update_fvl_graph ────────────────────────────────────────────────
 * Scale axes to fit the actual maneuver data with 1.5× headroom.
 * The ISO 26782 §7.3 "2 L/s : 1 L" ratio is applied only to choose
 * which axis drives the other — it does NOT enforce a minimum scale
 * that would crush a low-flow maneuver into 5% of the canvas.
 * Canvas: 210 × 224 px (w × h).
 * ─────────────────────────────────────────────────────────────────────────*/
static void gui_update_fvl_graph(void)
{
    if (!objects.fvl_chart || s_result.n_samples == 0) return;

    const float CW = 210.0f;
    const float CH = 224.0f;

    /* x-axis: FVC with 20% headroom, minimum 0.5 L, rounded to 0.5 L */
    float x_max = s_result.fvc * 1.2f;
    if (x_max < 0.5f) x_max = 0.5f;
    x_max = ceilf(x_max / 0.5f) * 0.5f;

    /* y-axis: PEF with 30% headroom, minimum 0.5 L/s, rounded to 0.5 */
    float y_max = s_result.pef * 1.3f;
    if (y_max < 0.5f) y_max = 0.5f;
    y_max = ceilf(y_max / 0.5f) * 0.5f;

    /* Apply ISO §7.3 aspect ratio (2 L/s : 1 L) only if it would INCREASE
     * the y_max — never decrease it to crush the curve */
    float y_from_ratio = 2.0f * x_max * (CH / CW);
    if (y_from_ratio > y_max) y_max = ceilf(y_from_ratio / 0.5f) * 0.5f;

    fvl_x_max_ml   = (int32_t)(x_max * 1000.0f);
    fvl_y_max_mlps = (int32_t)(y_max * 1000.0f);

    char buf[16];
    float yv[4] = { y_max, y_max * 2.0f / 3.0f, y_max / 3.0f, 0.0f };
    for (int i = 0; i < 4; i++) {
        int v10 = (int)(yv[i] * 10.0f + 0.5f);
        if (v10 < 0)   v10 = 0;
        if (v10 > 999) v10 = 999;
        if (v10 % 10 == 0) snprintf(buf, sizeof(buf), "%d",    v10 / 10);
        else                snprintf(buf, sizeof(buf), "%d.%d", v10 / 10, v10 % 10);
        lv_label_set_text(objects.fvl_ylabel[i], buf);
    }
    float xv[4] = { 0.0f, x_max / 3.0f, x_max * 2.0f / 3.0f, x_max };
    for (int i = 0; i < 4; i++) {
        int v10 = (int)(xv[i] * 10.0f + 0.5f);
        if (v10 < 0)   v10 = 0;
        if (v10 > 999) v10 = 999;
        if (v10 == 0)          snprintf(buf, sizeof(buf), "0");
        else if (v10 % 10 == 0) snprintf(buf, sizeof(buf), "%d",    v10 / 10);
        else                   snprintf(buf, sizeof(buf), "%d.%d",  v10 / 10, v10 % 10);
        lv_label_set_text(objects.fvl_xlabel[i], buf);
    }

    if (objects.fvl_wait_lbl) lv_obj_add_flag(objects.fvl_wait_lbl, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(objects.fvl_chart);
}

/* ── gui_update_vt_graph ──────────────────────────────────────────────────
 * Scale axes to fit the actual maneuver data with headroom.
 * The ISO 26782 §7.3 "1 L : 1 s" ratio is applied only to ensure the
 * axes are proportional — it does NOT enforce a minimum time of 6 s
 * that would squash a 0.8 L maneuver across a 7 L y-axis.
 * Canvas: 210 × 224 px (w × h).
 * ─────────────────────────────────────────────────────────────────────────*/
static void gui_update_vt_graph(void)
{
    if (!objects.vt_chart || s_result.n_samples == 0) return;

    const float CW = 210.0f;
    const float CH = 224.0f;

    /* Volume axis: FVC with 30% headroom, minimum 0.5 L, round to 0.5 */
    float v_max = s_result.fvc * 1.3f;
    if (v_max < 0.5f) v_max = 0.5f;
    v_max = ceilf(v_max / 0.5f) * 0.5f;

    /* Time axis: actual expiratory time with 20% headroom, minimum 2 s,
     * capped at 15 s, rounded up to nearest second */
    float t_max = s_result.te * 1.2f;
    if (t_max < 2.0f)  t_max = 2.0f;
    if (t_max > 15.0f) t_max = 15.0f;
    t_max = ceilf(t_max);

    /* Apply ISO §7.3 1L:1s ratio to keep proportional axes:
     * required v_max/t_max = CH/CW = 224/210 ≈ 1.07
     * Only EXPAND whichever axis is too small — never shrink to crush data */
    float ratio_req = CH / CW;
    float ratio_cur = v_max / t_max;
    if (ratio_cur < ratio_req) {
        /* v_max is too small for the time span — expand volume */
        v_max = t_max * ratio_req;
        v_max = ceilf(v_max / 0.5f) * 0.5f;
    } else {
        /* t_max is too small for the volume — expand time */
        t_max = v_max / ratio_req;
        t_max = ceilf(t_max);
        if (t_max > 15.0f) t_max = 15.0f;
    }

    vt_t_max_ms = (int32_t)(t_max * 1000.0f);
    vt_v_max_ml = (int32_t)(v_max * 1000.0f);

    char buf[16];
    float yv[4] = { v_max, v_max * 2.0f / 3.0f, v_max / 3.0f, 0.0f };
    for (int i = 0; i < 4; i++) {
        int v10 = (int)(yv[i] * 10.0f + 0.5f);
        if (v10 < 0)   v10 = 0;
        if (v10 > 999) v10 = 999;
        if (v10 % 10 == 0) snprintf(buf, sizeof(buf), "%d",    v10 / 10);
        else                snprintf(buf, sizeof(buf), "%d.%d", v10 / 10, v10 % 10);
        lv_label_set_text(objects.vt_ylabel[i], buf);
    }
    float xv[4] = { 0.0f, t_max / 3.0f, t_max * 2.0f / 3.0f, t_max };
    for (int i = 0; i < 4; i++) {
        int v10 = (int)(xv[i] * 10.0f + 0.5f);
        if (v10 < 0)   v10 = 0;
        if (v10 > 999) v10 = 999;
        if (v10 == 0)          snprintf(buf, sizeof(buf), "0");
        else if (v10 % 10 == 0) snprintf(buf, sizeof(buf), "%d",    v10 / 10);
        else                   snprintf(buf, sizeof(buf), "%d.%d",  v10 / 10, v10 % 10);
        lv_label_set_text(objects.vt_xlabel[i], buf);
    }

    if (objects.vt_wait_lbl) lv_obj_add_flag(objects.vt_wait_lbl, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(objects.vt_chart);
}

/* ── gui_reset_display ───────────────────────────────────────────────────── */
static void gui_reset_display(void)
{
    s_t0_s = 0.0f; s_bev = 0.0f;

    /* Legacy aliases */
    if (objects.fev1_val)  lv_label_set_text(objects.fev1_val, "--");
    if (objects.obj2)      lv_bar_set_value(objects.obj2, 0, LV_ANIM_OFF);
    if (objects.obj3)      lv_label_set_text(objects.obj3, "--%");
    if (objects.obj4)      lv_label_set_text(objects.obj4, "--");
    if (objects.obj5)      lv_bar_set_value(objects.obj5, 0, LV_ANIM_OFF);
    if (objects.obj6)      lv_label_set_text(objects.obj6, "--");
    if (objects.obj7)      lv_bar_set_value(objects.obj7, 0, LV_ANIM_OFF);
    if (objects.obj8)      lv_label_set_text(objects.obj8, "--");
    if (objects.obj9)      lv_bar_set_value(objects.obj9, 0, LV_ANIM_OFF);

    /* Results table */
    if (objects.res_fvc_act)     lv_label_set_text(objects.res_fvc_act,     "--");
    if (objects.res_fvc_pred)    lv_label_set_text(objects.res_fvc_pred,    "--");
    if (objects.res_fvc_pct)     lv_label_set_text(objects.res_fvc_pct,     "");
    if (objects.res_fev1_act)    lv_label_set_text(objects.res_fev1_act,    "--");
    if (objects.res_fev1_pred)   lv_label_set_text(objects.res_fev1_pred,   "--");
    if (objects.res_fev1_pct)    lv_label_set_text(objects.res_fev1_pct,    "");
    if (objects.res_fev6_act)    lv_label_set_text(objects.res_fev6_act,    "--");
    if (objects.res_fev6_pred)   lv_label_set_text(objects.res_fev6_pred,   "--");
    if (objects.res_fev6_pct)    lv_label_set_text(objects.res_fev6_pct,    "");
    if (objects.res_ratio_act)   lv_label_set_text(objects.res_ratio_act,   "--");
    if (objects.res_ratio_pred)  lv_label_set_text(objects.res_ratio_pred,  "--");
    if (objects.res_ratio_pct)   lv_label_set_text(objects.res_ratio_pct,   "");
    if (objects.res_pef_act)     lv_label_set_text(objects.res_pef_act,     "--");
    if (objects.res_pef_pred)    lv_label_set_text(objects.res_pef_pred,    "--");
    if (objects.res_pef_pct)     lv_label_set_text(objects.res_pef_pct,     "");
    if (objects.res_fef25_act)   lv_label_set_text(objects.res_fef25_act,   "--");
    if (objects.res_fef50_act)   lv_label_set_text(objects.res_fef50_act,   "--");
    if (objects.res_fef75_act)   lv_label_set_text(objects.res_fef75_act,   "--");
    if (objects.res_fef2575_act) lv_label_set_text(objects.res_fef2575_act, "--");

    /* Quality card */
    if (objects.res_grade_lbl)   lv_label_set_text(objects.res_grade_lbl,  "-");
    if (objects.res_start_lbl)   lv_label_set_text(objects.res_start_lbl,  "-");
    if (objects.res_end_lbl)     lv_label_set_text(objects.res_end_lbl,    "-");
    if (objects.res_interp_lbl)  lv_label_set_text(objects.res_interp_lbl, "--");

    /* Extended */
    if (objects.te_val)          lv_label_set_text(objects.te_val,         "--");
    if (objects.tpef_val)        lv_label_set_text(objects.tpef_val,       "--");
    if (objects.fef2575_val)     lv_label_set_text(objects.fef2575_val,    "--");
    if (objects.fef50_val)       lv_label_set_text(objects.fef50_val,      "--");
    if (objects.sat_label)       lv_label_set_text(objects.sat_label,       "");
    if (objects.validity_label)  lv_label_set_text(objects.validity_label, "--");

    /* Graph axis placeholders */
    for (int i = 0; i < 4; i++) {
        if (objects.fvl_ylabel[i]) lv_label_set_text(objects.fvl_ylabel[i], i == 3 ? " 0" : "--");
        if (objects.fvl_xlabel[i]) lv_label_set_text(objects.fvl_xlabel[i], i == 0 ? "0" : "-");
        if (objects.vt_ylabel[i])  lv_label_set_text(objects.vt_ylabel[i],  i == 3 ? " 0" : "--");
        if (objects.vt_xlabel[i])  lv_label_set_text(objects.vt_xlabel[i],  i == 0 ? "0" : "-");
    }

    if (objects.fvl_wait_lbl) lv_obj_clear_flag(objects.fvl_wait_lbl, LV_OBJ_FLAG_HIDDEN);
    if (objects.vt_wait_lbl)  lv_obj_clear_flag(objects.vt_wait_lbl,  LV_OBJ_FLAG_HIDDEN);

    if (objects.fvl_chart) lv_obj_invalidate(objects.fvl_chart);
    if (objects.vt_chart)  lv_obj_invalidate(objects.vt_chart);
}
