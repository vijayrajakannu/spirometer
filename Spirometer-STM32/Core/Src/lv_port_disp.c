/*
 * lv_port_disp.c — LVGL 9 display port, ILI9341, SPI1, portrait 240×320
 *
 * FIX: changed #include "ILI9341.h" → #include "ili9341.h"
 *   The file on disk is lowercase.  On Linux (STM32CubeIDE default) the
 *   filesystem is case-sensitive so "ILI9341.h" is a compile error:
 *   fatal error: ILI9341.h: No such file or directory
 *   On Windows it silently resolves the wrong case, masking the bug.
 */

#include "lv_port_disp.h"
#include "ili9341.h"       /* ← was "ILI9341.h" — wrong case on Linux */

/* Display resolution — ROTATE_0 portrait */
#define MY_DISP_HOR_RES  240
#define MY_DISP_VER_RES  320

/*
 * Draw buffers: 2 bytes per pixel (RGB565).
 * 10 rows × 240 px × 2 B = 4 800 B each, 9 600 B total.
 */
#define DISP_BUF_ROWS  10
#define DISP_BUF_SIZE  (MY_DISP_HOR_RES * DISP_BUF_ROWS * 2U)

static lv_display_t *disp;

static uint8_t buf_1[DISP_BUF_SIZE];
static uint8_t buf_2[DISP_BUF_SIZE];

static void disp_init(void);
static void disp_flush(lv_display_t *disp_drv, const lv_area_t *area,
                       uint8_t *px_map);

void lv_port_disp_init(void)
{
    disp_init();

    disp = lv_display_create(MY_DISP_HOR_RES, MY_DISP_VER_RES);

    /*
     * RGB565_SWAPPED: LVGL stores pixels low-byte-first in the buffer.
     * SPI clocks bytes MSB-first, so the low byte hits the wire first
     * and ILI9341 receives it as the high byte — correct big-endian
     * RGB565 without any runtime byte-swap.
     * REQUIREMENT: LV_DRAW_SW_SUPPORT_RGB565_SWAPPED = 1 in lv_conf.h
     */
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565_SWAPPED);

    lv_display_set_buffers(disp, buf_1, buf_2, sizeof(buf_1),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_display_set_flush_cb(disp, disp_flush);
}

static void disp_init(void)
{
    ILI9341_Init();
}

static void disp_flush(lv_display_t *disp_drv, const lv_area_t *area,
                       uint8_t *px_map)
{
    int32_t w = area->x2 - area->x1 + 1;
    int32_t h = area->y2 - area->y1 + 1;

    ILI9341_SetWindow(area->x1, area->y1, area->x2, area->y2);
    ILI9341_DrawBitmap((uint16_t)w, (uint16_t)h, px_map);

    lv_display_flush_ready(disp_drv);
}
