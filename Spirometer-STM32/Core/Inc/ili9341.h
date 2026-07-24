/*
 * ili9341.h
 *
 *  Author: Kotetsu Yamamoto
 *
 * GUI_WIDTH / GUI_HEIGHT corrected to portrait (240×320) to match
 * LCD_direction(ROTATE_0) / MADCTL 0x48 in ILI9341_Init().
 * Previous values (320×240) were landscape leftovers.
 */

#ifndef INC_ILI9341_H_
#define INC_ILI9341_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* Portrait — matches LCD_direction(ROTATE_0) / MADCTL 0x48 */
#define GUI_WIDTH   240
#define GUI_HEIGHT  320

void ILI9341_Init(void);
void ILI9341_SetWindow(uint16_t start_x, uint16_t start_y,
                       uint16_t end_x,   uint16_t end_y);
void ILI9341_DrawBitmap(uint16_t w, uint16_t h, uint8_t *s);
void ILI9341_DrawBitmapDMA(uint16_t w, uint16_t h, uint8_t *s);
void ILI9341_WritePixel(uint16_t x, uint16_t y, uint16_t color);
void ILI9341_EndOfDrawBitmap(void);
void LCD_WR_REG(uint8_t data);

#ifdef __cplusplus
}
#endif

#endif /* INC_ILI9341_H_ */
