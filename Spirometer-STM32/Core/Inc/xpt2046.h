/*
 * xpt2046.h  — XPT2046 resistive touch driver (SPI2, median-of-5, runtime cal)
 */
#ifndef XPT2046_H
#define XPT2046_H

#include "main.h"
#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Public API ─────────────────────────────────────────────────────────── */

/** Initialise CS line high, nothing else needed. */
void xpt2046_init(void);

/**
 * Write runtime calibration constants.
 * Called by touch_cal_run() after the 4-corner tap sequence.
 * Safe defaults are compiled in; call this before the main UI loads.
 */
void xpt2046_set_cal(int32_t x_min, int32_t x_max,
                     int32_t y_min, int32_t y_max);

/**
 * Read raw (un-calibrated) ADC values.
 * Used by touch_cal_run() during calibration.
 * Returns false if the panel is not pressed (IRQ high).
 */
bool xpt2046_read_raw_point(uint16_t *raw_x, uint16_t *raw_y);

/**
 * Read calibrated, screen-mapped touch coordinates.
 * Returns false when no touch is detected.
 */
bool xpt2046_get_touch(int16_t *x, int16_t *y);

/**
 * LVGL input-device read callback.
 * Register with: lv_indev_set_read_cb(indev, lv_touchpad_read);
 */
void lv_touchpad_read(lv_indev_t *indev, lv_indev_data_t *data);

#ifdef __cplusplus
}
#endif

#endif /* XPT2046_H */
