/*
 * touch_cal.h — 4-point on-screen touch calibration
 *
 * Call touch_cal_run() once in main() AFTER lv_port_disp_init() and
 * xpt2046_init(), but BEFORE registering the LVGL indev and calling ui_init().
 *
 *   xpt2046_init();
 *   touch_cal_run();          // ← blocks until 4 taps collected
 *   lv_indev_t *indev = lv_indev_create();
 *   lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
 *   lv_indev_set_read_cb(indev, lv_touchpad_read);
 *   ui_init();
 *   spiro_init();
 */
#ifndef TOUCH_CAL_H
#define TOUCH_CAL_H

#ifdef __cplusplus
extern "C" {
#endif

void touch_cal_run(void);

#ifdef __cplusplus
}
#endif

#endif /* TOUCH_CAL_H */
