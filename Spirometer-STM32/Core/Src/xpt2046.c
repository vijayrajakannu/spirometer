/*
 * xpt2046.c  — XPT2046 resistive touch driver
 *
 *  SPI bus  : SPI2 (hspi2, soft-CS)
 *  Sampling : median-of-5 per axis — rejects single-shot spike noise
 *  Calib    : runtime values written by touch_cal_run(); safe defaults
 *             compile in so the driver is usable before calibration.
 *
 * Channel command bytes (SER/DFR=0, 12-bit, power-down between):
 *   0x90 → Y position  (labelled "touch Y" in hardware, maps to screen X)
 *   0xD0 → X position  (labelled "touch X" in hardware, maps to screen Y)
 *
 * The axis swap + Y inversion below matches MADCTL = 0x48 (ROTATE_0).
 * If you change the display orientation, update xpt2046_get_touch().
 */

#include "xpt2046.h"
#include "main.h"

/* ── Hardware handles ───────────────────────────────────────────────────── */
extern SPI_HandleTypeDef hspi2;

#define TOUCH_CS_PORT   TOUCH_CS_GPIO_Port
#define TOUCH_CS_PIN    TOUCH_CS_Pin
#define TOUCH_IRQ_PORT  TOUCH_IRQ_GPIO_Port
#define TOUCH_IRQ_PIN   TOUCH_IRQ_Pin

/* ── Screen geometry ────────────────────────────────────────────────────── */
#define SCREEN_W  240
#define SCREEN_H  320

/* ── Runtime calibration ────────────────────────────────────────────────── */
/*
 * Safe defaults — these will map the entire ADC range linearly.
 * They produce wildly wrong screen coordinates on most panels, but
 * touch_cal_run() overwrites them before the main UI is shown, so the
 * defaults are only ever seen during the calibration screen itself (where
 * the LVGL indev is not yet registered and cannot misinterpret them).
 */
static int32_t cal_x_min = 200;
static int32_t cal_x_max = 3800;
static int32_t cal_y_min = 200;
static int32_t cal_y_max = 3800;

void xpt2046_set_cal(int32_t x_min, int32_t x_max,
                     int32_t y_min, int32_t y_max)
{
    cal_x_min = x_min;
    cal_x_max = x_max;
    cal_y_min = y_min;
    cal_y_max = y_max;
}

/* ── Public init ─────────────────────────────────────────────────────────── */
void xpt2046_init(void)
{
    /* De-select the touch chip; SPI2 is shared with nothing else, but
     * keeping CS high at startup avoids a false read if the MCU boots
     * with the pin floating.                                              */
    HAL_GPIO_WritePin(TOUCH_CS_PORT, TOUCH_CS_PIN, GPIO_PIN_SET);
}

/* ── Low-level single 12-bit ADC read ──────────────────────────────────── */
static uint16_t xpt2046_read_raw(uint8_t cmd)
{
    uint8_t tx[3] = { cmd, 0x00, 0x00 };
    uint8_t rx[3] = { 0x00, 0x00, 0x00 };

    HAL_GPIO_WritePin(TOUCH_CS_PORT, TOUCH_CS_PIN, GPIO_PIN_RESET);
    HAL_SPI_TransmitReceive(&hspi2, tx, rx, 3, HAL_MAX_DELAY);
    HAL_GPIO_WritePin(TOUCH_CS_PORT, TOUCH_CS_PIN, GPIO_PIN_SET);

    /* XPT2046 returns 12 bits left-justified in the 15 LSBs of a 16-bit
     * transfer; shift right by 3 to get the 12-bit value (0–4095).      */
    return ((uint16_t)(rx[1] << 8) | rx[2]) >> 3;
}

/* ── Median-of-5 — ~5× more stable than a plain average ─────────────────
 *
 * Takes 5 samples of one channel and returns the median (sorted index 2).
 * A single outlier caused by EMI or finger motion cannot affect the median,
 * whereas it would pull a mean by up to 20 %.
 *
 * Cost: 5 SPI transfers × ~3 bytes @ 2.6 MHz ≈ 45 µs per axis (negligible).
 */
static uint16_t xpt2046_median5(uint8_t cmd)
{
    uint16_t s[5];
    for (int i = 0; i < 5; i++)
        s[i] = xpt2046_read_raw(cmd);

    /* In-place insertion sort — trivial for 5 elements */
    for (int i = 1; i < 5; i++) {
        uint16_t key = s[i];
        int      j   = i - 1;
        while (j >= 0 && s[j] > key) {
            s[j + 1] = s[j];
            j--;
        }
        s[j + 1] = key;
    }
    return s[2];   /* median */
}

/* ── Raw read exposed to calibration routine ────────────────────────────── */
/*
 * Returns false when the panel is NOT being touched, true (with raw_x/raw_y
 * filled) when it is.
 *
 * Touch is gated on the XPT2046 PENIRQ (T_IRQ) line:
 *   - idle  → pulled high by the GPIO pull-up (gpio.c sets PA3 = PULLUP)  → false
 *   - press → the panel pulls T_IRQ low                                   → true
 *
 * This gate is REQUIRED: touch_cal.c's averaging loop runs
 * `while (xpt2046_read_raw_point(...))` and the LVGL indev reports release
 * from this return value.  If this always returned true (as a previous
 * version did with the IRQ check commented out), the calibration averaging
 * loop never terminates and the UI locks on the first calibration target.
 *
 * Only the PRE-check is used.  A post-sample re-check is intentionally
 * avoided: the XPT2046 holds PENIRQ low/disabled during a position
 * conversion and only re-enables it at end-of-conversion, so reading the
 * line immediately after the burst can give a false "released" result.
 */
bool xpt2046_read_raw_point(uint16_t *raw_x, uint16_t *raw_y)
{
    if (HAL_GPIO_ReadPin(TOUCH_IRQ_PORT, TOUCH_IRQ_PIN) == GPIO_PIN_SET)
        return false;   /* T_IRQ high → no touch */

    *raw_y = xpt2046_median5(0xD0);
    *raw_x = xpt2046_median5(0x90);

    return true;
}

/* ── Calibrated, screen-mapped touch read ──────────────────────────────── */
bool xpt2046_get_touch(int16_t *x, int16_t *y)
{
    uint16_t raw_x, raw_y;
    /* Respect the PENIRQ gate: no touch → report released */
    if (!xpt2046_read_raw_point(&raw_x, &raw_y))
        return false;

    // XPT2046 returns ~0 or ~4095 when not touched (no pressure)
    // Valid touch range is roughly 200–3800
    if (raw_x < 100 || raw_x > 3950 || raw_y < 100 || raw_y > 3950)
        return false;

    /* Clamp to calibrated range before mapping to prevent negative results
     * from integer truncation when the stylus hits the very edge.        */
    if (raw_x < (uint16_t)cal_x_min) raw_x = (uint16_t)cal_x_min;
    if (raw_x > (uint16_t)cal_x_max) raw_x = (uint16_t)cal_x_max;
    if (raw_y < (uint16_t)cal_y_min) raw_y = (uint16_t)cal_y_min;
    if (raw_y > (uint16_t)cal_y_max) raw_y = (uint16_t)cal_y_max;

    /* Linear map to screen pixels.
     * Y axis is inverted: increasing raw_y → decreasing screen Y.
     * This matches MADCTL 0x48 (ROTATE_0, BGR).                          */
    *x = (int16_t)((int32_t)(raw_x - cal_x_min) * SCREEN_W
                   / (cal_x_max - cal_x_min));

    *y = (int16_t)(SCREEN_H - 1
                   - (int32_t)(raw_y - cal_y_min) * SCREEN_H
                     / (cal_y_max - cal_y_min));

    return true;
}

/* ── LVGL input-device read callback ────────────────────────────────────── */
void lv_touchpad_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;

    int16_t x = 0, y = 0;

    if (xpt2046_get_touch(&x, &y)) {
        data->point.x = x;
        data->point.y = y;
        data->state   = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}
