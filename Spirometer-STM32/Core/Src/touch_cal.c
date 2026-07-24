/*
 * touch_cal.c — 4-point on-screen touch calibration
 *
 * BUGS FIXED vs original:
 *
 * FIX 1 (FATAL) — draw_crosshair() only deleted the ring (cross_canvas).
 *   The horizontal arm (h) and vertical arm (v) lv_obj children were
 *   created fresh every call but never deleted — 2 leaked widget objects
 *   per call, 8 per calibration run, silently exhausting the LVGL pool.
 *   FIX: wrap all three elements in a single parent lv_obj (cross_group).
 *   Deleting cross_group deletes all three children in one call.
 *
 * FIX 2 (TIMING) — wait_tap() first-sample race condition.
 *   The "finger down" detection loop exited on the first true return from
 *   xpt2046_read_raw_point() and stored its value in (rx, ry).  If the IRQ
 *   line bounced and the very next poll returned false, n stayed 0 and the
 *   fallback `if (n==0) { sum_x=rx; sum_y=ry; }` used a single noisy sample.
 *   FIX: after detecting press, wait 50 ms for the contact to stabilise,
 *   THEN enter the averaging loop.  The first accumulated sample is now
 *   a settled reading.  The n==0 fallback is kept as a true last resort.
 */

#include "touch_cal.h"
#include "xpt2046.h"
#include "lvgl.h"

/* ── Layout ──────────────────────────────────────────────────────────────── */
#define SCREEN_W   240
#define SCREEN_H   320
#define MARGIN      20
#define CROSS_R     12
#define CROSS_T      2
#define RING_R      18

/* ── Colours ─────────────────────────────────────────────────────────────── */
#define COL_BG      lv_color_hex(0x0b0e14)
#define COL_TEXT    lv_color_hex(0x00e5a0)
#define COL_DIM     lv_color_hex(0x3d5070)
#define COL_CROSS   lv_color_hex(0x00e5a0)
#define COL_DONE    lv_color_hex(0x00d4ff)

/* ── Corners ─────────────────────────────────────────────────────────────── */
typedef struct { int16_t sx; int16_t sy; const char *label; } Corner;

static const Corner corners[4] = {
    { MARGIN,            MARGIN,            "Top-Left"     },
    { SCREEN_W - MARGIN, MARGIN,            "Top-Right"    },
    { MARGIN,            SCREEN_H - MARGIN, "Bottom-Left"  },
    { SCREEN_W - MARGIN, SCREEN_H - MARGIN, "Bottom-Right" },
};

/* ── LVGL objects ────────────────────────────────────────────────────────── */
static lv_obj_t *cal_screen   = NULL;
static lv_obj_t *instr_label  = NULL;
static lv_obj_t *step_label   = NULL;
static lv_obj_t *cross_group  = NULL;   /* BUG FIX: one group tracks all 3 shapes */

/* ── draw_crosshair ──────────────────────────────────────────────────────── */
static void draw_crosshair(lv_obj_t *parent, int16_t cx, int16_t cy,
                            lv_color_t col)
{
    /* BUG FIX: delete the whole group (h + v + ring) not just the ring */
    if (cross_group) {
        lv_obj_del(cross_group);
        cross_group = NULL;
    }

    /* One invisible group owns all three shape children.
     * Positioned at (0,0) in the parent; children use absolute coords.  */
    cross_group = lv_obj_create(parent);
    lv_obj_remove_style_all(cross_group);
    lv_obj_set_pos(cross_group, 0, 0);
    lv_obj_set_size(cross_group, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_opa(cross_group, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cross_group, 0, 0);
    lv_obj_set_style_shadow_width(cross_group, 0, 0);
    lv_obj_clear_flag(cross_group, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    /* Horizontal arm */
    lv_obj_t *h = lv_obj_create(cross_group);
    lv_obj_remove_style_all(h);
    lv_obj_set_pos(h, cx - CROSS_R, cy - CROSS_T / 2);
    lv_obj_set_size(h, CROSS_R * 2, CROSS_T);
    lv_obj_set_style_bg_color(h, col, 0);
    lv_obj_set_style_bg_opa(h, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(h, 0, 0);
    lv_obj_set_style_shadow_width(h, 0, 0);
    lv_obj_clear_flag(h, LV_OBJ_FLAG_CLICKABLE);

    /* Vertical arm */
    lv_obj_t *v = lv_obj_create(cross_group);
    lv_obj_remove_style_all(v);
    lv_obj_set_pos(v, cx - CROSS_T / 2, cy - CROSS_R);
    lv_obj_set_size(v, CROSS_T, CROSS_R * 2);
    lv_obj_set_style_bg_color(v, col, 0);
    lv_obj_set_style_bg_opa(v, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(v, 0, 0);
    lv_obj_set_style_shadow_width(v, 0, 0);
    lv_obj_clear_flag(v, LV_OBJ_FLAG_CLICKABLE);

    /* Ring */
    lv_obj_t *ring = lv_obj_create(cross_group);
    lv_obj_remove_style_all(ring);
    lv_obj_set_pos(ring, cx - RING_R, cy - RING_R);
    lv_obj_set_size(ring, RING_R * 2, RING_R * 2);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(ring, col, 0);
    lv_obj_set_style_border_width(ring, 2, 0);
    lv_obj_set_style_border_opa(ring, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(ring, RING_R, 0);
    lv_obj_set_style_shadow_width(ring, 0, 0);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_CLICKABLE);

    (void)h; (void)v; (void)ring;   /* owned by cross_group */
}

/* ── build_cal_screen ────────────────────────────────────────────────────── */
static void build_cal_screen(void)
{
    cal_screen = lv_obj_create(NULL);
    lv_obj_set_size(cal_screen, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(cal_screen, COL_BG, 0);
    lv_obj_set_style_bg_opa(cal_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(cal_screen, 0, 0);
    lv_obj_set_style_radius(cal_screen, 0, 0);
    lv_obj_set_style_pad_all(cal_screen, 0, 0);
    lv_obj_set_style_shadow_width(cal_screen, 0, 0);
    lv_obj_remove_flag(cal_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(cal_screen);
    lv_obj_set_style_text_color(title, COL_TEXT, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_opa(title, LV_OPA_TRANSP, 0);
    lv_label_set_text(title, "TOUCH CALIBRATION");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, SCREEN_H / 2 - 40);

    instr_label = lv_label_create(cal_screen);
    lv_obj_set_style_text_color(instr_label, COL_DIM, 0);
    lv_obj_set_style_text_font(instr_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_opa(instr_label, LV_OPA_TRANSP, 0);
    lv_label_set_text(instr_label, "Tap the target");
    lv_obj_align(instr_label, LV_ALIGN_TOP_MID, 0, SCREEN_H / 2 - 18);

    step_label = lv_label_create(cal_screen);
    lv_obj_set_style_text_color(step_label, COL_DIM, 0);
    lv_obj_set_style_text_font(step_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_opa(step_label, LV_OPA_TRANSP, 0);
    lv_label_set_text(step_label, "1 / 4");
    lv_obj_align(step_label, LV_ALIGN_TOP_MID, 0, SCREEN_H / 2 + 6);

    lv_scr_load(cal_screen);
}

/* ── wait_tap ────────────────────────────────────────────────────────────── */
static void wait_tap(uint16_t *out_raw_x, uint16_t *out_raw_y)
{
    /* Wait for IRQ to go low (finger touches panel) */
    while (!xpt2046_read_raw_point(out_raw_x, out_raw_y)) {
        lv_timer_handler();
        HAL_Delay(5);
    }

    /*
     * BUG FIX: settle delay.
     *
     * After the first detected press, wait 50 ms before averaging.
     * The resistive panel takes ~10–30 ms to stabilise after initial
     * contact — reading immediately captures the approach bounce.
     * This small delay means the averaging loop only sees settled
     * samples, giving a much more accurate centroid.
     */
    HAL_Delay(50);

    /* Average readings while finger is held.
     * Bounded to AVG_MAX_SAMPLES (~0.64 s at 10 ms/sample) so a stuck-low
     * T_IRQ or a very long press can never spin here forever — the loop
     * still exits early as soon as the finger lifts (read returns false). */
    #define AVG_MAX_SAMPLES 64u
    uint32_t sum_x = 0, sum_y = 0, n = 0;
    uint16_t cx, cy;
    while (n < AVG_MAX_SAMPLES && xpt2046_read_raw_point(&cx, &cy)) {
        sum_x += cx;
        sum_y += cy;
        n++;
        lv_timer_handler();
        HAL_Delay(10);
    }
    #undef AVG_MAX_SAMPLES

    if (n > 0) {
        *out_raw_x = (uint16_t)(sum_x / n);
        *out_raw_y = (uint16_t)(sum_y / n);
    }
    /* else: out_raw_x/y already hold the first valid reading from the
     * detection loop above — acceptable last resort.                    */

    /* Debounce: block until a new tap is allowed */
    uint32_t t0 = HAL_GetTick();
    while (HAL_GetTick() - t0 < 400) {
        lv_timer_handler();
        HAL_Delay(5);
    }
}

/* ── touch_cal_run ───────────────────────────────────────────────────────── */
void touch_cal_run(void)
{
    build_cal_screen();
    lv_timer_handler();

    uint16_t raw_x[4], raw_y[4];
    char step_buf[8];

    for (int i = 0; i < 4; i++) {
        lv_snprintf(step_buf, sizeof(step_buf), "%d / 4", i + 1);
        lv_label_set_text(step_label, step_buf);
        lv_label_set_text(instr_label, corners[i].label);

        draw_crosshair(cal_screen, corners[i].sx, corners[i].sy, COL_CROSS);

        lv_timer_handler();
        HAL_Delay(20);

        wait_tap(&raw_x[i], &raw_y[i]);

        draw_crosshair(cal_screen, corners[i].sx, corners[i].sy, COL_DONE);
        lv_timer_handler();
        HAL_Delay(200);
    }

    /*
     * Derive calibration constants.
     *
     * corners[0] = Top-Left     raw_x[0], raw_y[0]
     * corners[1] = Top-Right    raw_x[1], raw_y[1]
     * corners[2] = Bottom-Left  raw_x[2], raw_y[2]
     * corners[3] = Bottom-Right raw_x[3], raw_y[3]
     *
     * raw_x → screen X (left edge = small raw, right edge = large raw)
     * raw_y → screen Y inverted (top edge = large raw, bottom = small raw)
     */
    int32_t x_min = ((int32_t)raw_x[0] + raw_x[2]) / 2;
    int32_t x_max = ((int32_t)raw_x[1] + raw_x[3]) / 2;
    int32_t y_max = ((int32_t)raw_y[0] + raw_y[1]) / 2;
    int32_t y_min = ((int32_t)raw_y[2] + raw_y[3]) / 2;

    /* Safety swap for mirrored panels */
    if (x_min > x_max) { int32_t t = x_min; x_min = x_max; x_max = t; }
    if (y_min > y_max) { int32_t t = y_min; y_min = y_max; y_max = t; }

    /* Extrapolate to full screen edges (targets were inset by MARGIN) */
    int32_t x_span = x_max - x_min;
    int32_t y_span = y_max - y_min;
    x_min -= x_span * MARGIN / (SCREEN_W - 2 * MARGIN);
    x_max += x_span * MARGIN / (SCREEN_W - 2 * MARGIN);
    y_min -= y_span * MARGIN / (SCREEN_H - 2 * MARGIN);
    y_max += y_span * MARGIN / (SCREEN_H - 2 * MARGIN);

    xpt2046_set_cal(x_min, x_max, y_min, y_max);

    lv_label_set_text(instr_label, "Calibration done!");
    lv_label_set_text(step_label, "");
    lv_timer_handler();
    HAL_Delay(800);

    lv_obj_del(cal_screen);
    cal_screen   = NULL;
    cross_group  = NULL;   /* was a child of cal_screen, already freed */
}
