/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body — spirometry UI + flow engine
  ******************************************************************************
  */
/* USER CODE END Header */

#include "main.h"
#include "adc.h"
#include "dma.h"
#include "spi.h"
#include "usart.h"
#include "gpio.h"

/* USER CODE BEGIN Includes */
#include <stdio.h>          /* printf / snprintf */
#include "lvgl.h"
#include "lv_port_disp.h"
#include "xpt2046.h"
#include "touch_cal.h"
#include "ui/ui.h"
#include "ui/screens.h"
#include "spirometry.h"
#include <stdlib.h>
/* USER CODE END Includes */

void SystemClock_Config(void);

/* USER CODE BEGIN 0 */

/*
 * Redirect printf → USART1 (PA9 TX, 115200 8N1).
 * Every call to printf / puts / fprintf(stdout) flows through here.
 * Cost: ~87 µs per byte at 115200 baud — fine for debug, avoid in hot path.
 */
int __io_putchar(int ch)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
    return ch;
}

/*
 * LOG(fmt, ...) — timestamped single-line log macro.
 * Output example:  [  1234] touch raw x=1820 y=2310
 * The %6lu zero-pads the tick to keep columns aligned.
 */
#define LOG(fmt, ...)  \
    printf("[%6lu] " fmt "\r\n", HAL_GetTick(), ##__VA_ARGS__)

/* USER CODE END 0 */

int main(void)
{
  HAL_Init();
  SystemClock_Config();

  MX_GPIO_Init();
  MX_DMA_Init();
  MX_SPI1_Init();
  MX_SPI2_Init();
  MX_ADC1_Init();
  MX_USART1_UART_Init();

  /* USER CODE BEGIN 2 */

  /* ── First log line — proves UART is alive ────────────────────────── */
  LOG("=== BOOT ===");
  LOG("USART1 115200 8N1 OK");

  /* ── Step 1: LVGL + display ─────────────────────────────────────────── */
  LOG("lv_init...");
  lv_init();
  lv_tick_set_cb(HAL_GetTick);

  LOG("lv_port_disp_init...");
  lv_port_disp_init();
  LOG("display OK — ILI9341 ROTATE_0 240x320");

  /* ── Step 2: Touch hardware ──────────────────────────────────────────── */
  LOG("xpt2046_init...");
  xpt2046_init();

  /*
   * Quick IRQ sanity check — read T_IRQ once BEFORE calibration.
   * Expected when NOT touching: GPIO_PIN_SET (1 = not pressed).
   * If it prints 0 here with no finger on the panel, T_IRQ is stuck low
   * (floating, shorted, or PULLUP fighting the module's own pull-up).
   */
  uint8_t irq_idle = HAL_GPIO_ReadPin(TOUCH_IRQ_GPIO_Port, TOUCH_IRQ_Pin);
  LOG("T_IRQ idle = %u  (expected 1 = not pressed)", irq_idle);
  if (irq_idle == 0) {
      LOG("WARNING: T_IRQ is LOW with no touch — check wiring or gpio.c pull setting");
  }

  /* ── Step 3: Calibration ─────────────────────────────────────────────── */
  /* Set ENABLE_TOUCH_CAL to 0 during bring-up to skip the 4-tap calibration.
   * The XPT2046 driver then uses its built-in default mapping — touch will be
   * approximate but fully functional for exercising the UI. */
#ifndef ENABLE_TOUCH_CAL
#define ENABLE_TOUCH_CAL 0
#endif
#if ENABLE_TOUCH_CAL
  LOG("touch_cal_run — waiting for 4 corner taps...");
  touch_cal_run();
  LOG("calibration done");
#else
  LOG("touch calibration SKIPPED (ENABLE_TOUCH_CAL=0, using default cal)");
#endif

  /* ── Step 4: Register LVGL indev ────────────────────────────────────── */
  LOG("registering touch indev...");
  lv_indev_t *touch_indev = lv_indev_create();
  lv_indev_set_type(touch_indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(touch_indev, lv_touchpad_read);
  /* Note: lv_conf.h has LV_USE_GESTURE_RECOGNITION=0, so LVGL's built-in
   * multi-touch gesture system is already disabled at compile time.
   * Swipe-to-navigate is blocked in screens.c via gesture_consume_cb
   * (lv_event_stop_processing) registered on every screen root, plus
   * make_inert_container() clearing SCROLLABLE/CLICKABLE/GESTURE_BUBBLE
   * on all non-interactive containers. */
  LOG("indev registered");

  /* ── Step 5: UI ──────────────────────────────────────────────────────── */
  LOG("ui_init...");
  ui_init();
  LOG("ui_init done");

  /* ── Step 6: Spirometry engine ───────────────────────────────────────── */
  LOG("spiro_init...");
  spiro_init();
  LOG("spiro_init done");

  LOG("=== ENTERING MAIN LOOP ===");

  /* USER CODE END 2 */

  /* ── Main loop ───────────────────────────────────────────────────────── */
  /* USER CODE BEGIN WHILE */

  /*
   * Touch probe variables — log every new touch coordinate once per press.
   * Removed from production build by defining NDEBUG or deleting this block.
   */
  static int16_t last_logged_x = -1;
  static int16_t last_logged_y = -1;
  static bool    was_pressed   = false;
  uint32_t       loop_count    = 0;

  while (1)
  {
    /* USER CODE END WHILE */
    /* USER CODE BEGIN 3 */

    lv_timer_handler();
    ui_tick();          /* drives per-screen tick logic (boot progress bar,
                           dashboard refresh, …). Without this the boot screen
                           never finishes and the dashboard is never shown. */
    spiro_process();
    HAL_Delay(2);

    /*
     * Touch logging — fires once on press (coordinates) and once on release.
     * At 2 ms per loop this adds ~0 overhead when not touching.
     * Comment out this entire block once touch is confirmed working.
     */
    {
        int16_t  tx = 0, ty = 0;
        uint16_t rx = 0, ry = 0;
        bool pressed = xpt2046_get_touch(&tx, &ty);

        if (pressed && !was_pressed) {
            /* New press — log raw AND calibrated coords so the mapping can
             * be verified: tap the corners and check
             *   top-left      → screen≈(  0,  0)
             *   top-right     → screen≈(239,  0)
             *   bottom-left   → screen≈(  0,319)
             *   bottom-right  → screen≈(239,319)
             * If a corner is mirrored/swapped, the axis handling in
             * xpt2046_get_touch() needs adjusting for this panel. */
            xpt2046_read_raw_point(&rx, &ry);
            LOG("TOUCH PRESS  screen=(%4d,%4d) raw=(%4u,%4u)", tx, ty, rx, ry);
            last_logged_x = tx;
            last_logged_y = ty;
            was_pressed = true;
        } else if (pressed && (tx != last_logged_x || ty != last_logged_y)) {
            /* Drag — log only if position changed significantly */
            if (abs(tx - last_logged_x) > 4 || abs(ty - last_logged_y) > 4) {
                LOG("TOUCH DRAG   screen=(%4d,%4d)", tx, ty);
                last_logged_x = tx;
                last_logged_y = ty;
            }
        } else if (!pressed && was_pressed) {
            LOG("TOUCH RELEASE screen=(%4d,%4d)", last_logged_x, last_logged_y);
            was_pressed = false;
        }
    }

    /*
     * Periodic heartbeat — proves the loop is running.
     * Prints every 5 000 iterations ≈ every 10 seconds.
     * Remove once system is confirmed stable.
     */
    loop_count++;
    if (loop_count % 5000 == 0) {
        LOG("heartbeat tick=%lu", HAL_GetTick());
    }
  }
  /* USER CODE END 3 */
}

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

  RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                   | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
    Error_Handler();
}

void Error_Handler(void)
{
  __disable_irq();
  /* Log before halting so you can see which init step failed */
  LOG("!!! ERROR_HANDLER called at tick=%lu !!!", HAL_GetTick());
  while (1) {}
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  (void)file; (void)line;
}
#endif
