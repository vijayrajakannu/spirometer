/*
 * ili9341.c  — ILI9341 SPI display driver, ROTATE_0 portrait 240×320
 *
 * NOTE: ConvHL() is intentionally NOT called in ILI9341_DrawBitmap or
 * ILI9341_DrawBitmapDMA. lv_port_disp.c sets LV_COLOR_FORMAT_RGB565_SWAPPED,
 * so LVGL already renders pixels in the byte order ILI9341 expects over SPI.
 * Calling ConvHL() would swap bytes a second time → wrong colours.
 */

#include "ili9341.h"
#include "main.h"

typedef enum {
    ROTATE_0,
    ROTATE_90,
    ROTATE_180,
    ROTATE_270
} LCD_Horizontal_t;

extern void Error_Handler(void);
extern SPI_HandleTypeDef hspi1;

void ILI9341_Reset(void);
void ILI9341_SoftReset(void);
void LCD_WR_REG(uint8_t data);
static void LCD_WR_DATA(uint8_t data);
static void LCD_direction(LCD_Horizontal_t direction);
static void RESET_L(void);
static void RESET_H(void);
static void CS_L(void);
static void DC_L(void);
static void DC_H(void);

void sendSPI(uint8_t *data, int size)
{
    HAL_SPI_Transmit(&hspi1, data, size, HAL_MAX_DELAY);
}

void Delay(uint16_t ms)
{
    HAL_Delay(ms);
}

static void RESET_L(void) { HAL_GPIO_WritePin(RESET_GPIO_Port, RESET_Pin, GPIO_PIN_RESET); }
static void RESET_H(void) { HAL_GPIO_WritePin(RESET_GPIO_Port, RESET_Pin, GPIO_PIN_SET);   }
static void CS_L(void)    { HAL_GPIO_WritePin(CS_GPIO_Port,    CS_Pin,    GPIO_PIN_RESET); }
static void DC_L(void)    { HAL_GPIO_WritePin(DC_GPIO_Port,    DC_Pin,    GPIO_PIN_RESET); }
static void DC_H(void)    { HAL_GPIO_WritePin(DC_GPIO_Port,    DC_Pin,    GPIO_PIN_SET);   }

void ILI9341_Init(void)
{
    ILI9341_Reset();
    ILI9341_SoftReset();

    LCD_WR_REG(0xCB); LCD_WR_DATA(0x39); LCD_WR_DATA(0x2C); LCD_WR_DATA(0x00); LCD_WR_DATA(0x34); LCD_WR_DATA(0x02);
    LCD_WR_REG(0xCF); LCD_WR_DATA(0x00); LCD_WR_DATA(0xC1); LCD_WR_DATA(0x30);
    LCD_WR_REG(0xE8); LCD_WR_DATA(0x85); LCD_WR_DATA(0x00); LCD_WR_DATA(0x78);
    LCD_WR_REG(0xEA); LCD_WR_DATA(0x00); LCD_WR_DATA(0x00);
    LCD_WR_REG(0xED); LCD_WR_DATA(0x64); LCD_WR_DATA(0x03); LCD_WR_DATA(0x12); LCD_WR_DATA(0x81);
    LCD_WR_REG(0xF7); LCD_WR_DATA(0x20);
    LCD_WR_REG(0xC0); LCD_WR_DATA(0x10);
    LCD_WR_REG(0xC1); LCD_WR_DATA(0x10);
    LCD_WR_REG(0xC5); LCD_WR_DATA(0x3E); LCD_WR_DATA(0x28);
    LCD_WR_REG(0xC7); LCD_WR_DATA(0x86);
    LCD_WR_REG(0x36); LCD_WR_DATA(0x48);
    LCD_WR_REG(0x3A); LCD_WR_DATA(0x55);
    LCD_WR_REG(0xB1); LCD_WR_DATA(0x00); LCD_WR_DATA(0x18);
    LCD_WR_REG(0xB6); LCD_WR_DATA(0x08); LCD_WR_DATA(0x82); LCD_WR_DATA(0x27);
    LCD_WR_REG(0xF2); LCD_WR_DATA(0x00);
    LCD_WR_REG(0x26); LCD_WR_DATA(0x01);
    LCD_WR_REG(0xE0);
    LCD_WR_DATA(0x0F); LCD_WR_DATA(0x31); LCD_WR_DATA(0x2B); LCD_WR_DATA(0x0C);
    LCD_WR_DATA(0x0E); LCD_WR_DATA(0x08); LCD_WR_DATA(0x4E); LCD_WR_DATA(0xF1);
    LCD_WR_DATA(0x37); LCD_WR_DATA(0x07); LCD_WR_DATA(0x10); LCD_WR_DATA(0x03);
    LCD_WR_DATA(0x0E); LCD_WR_DATA(0x09); LCD_WR_DATA(0x00);
    LCD_WR_REG(0xE1);
    LCD_WR_DATA(0x00); LCD_WR_DATA(0x0E); LCD_WR_DATA(0x14); LCD_WR_DATA(0x03);
    LCD_WR_DATA(0x11); LCD_WR_DATA(0x07); LCD_WR_DATA(0x31); LCD_WR_DATA(0xC1);
    LCD_WR_DATA(0x48); LCD_WR_DATA(0x08); LCD_WR_DATA(0x0F); LCD_WR_DATA(0x0C);
    LCD_WR_DATA(0x31); LCD_WR_DATA(0x36); LCD_WR_DATA(0x0F);
    LCD_WR_REG(0x11);
    Delay(120);
    LCD_WR_REG(0x29);
    LCD_WR_DATA(0x2C);

    LCD_direction(ROTATE_0);
}

void ILI9341_SetWindow(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    LCD_WR_REG(0x2a);
    LCD_WR_DATA(x1 >> 8); LCD_WR_DATA(x1 & 0xFF);
    LCD_WR_DATA(x2 >> 8); LCD_WR_DATA(x2 & 0xFF);
    LCD_WR_REG(0x2b);
    LCD_WR_DATA(y1 >> 8); LCD_WR_DATA(y1 & 0xFF);
    LCD_WR_DATA(y2 >> 8); LCD_WR_DATA(y2 & 0xFF);
}

void ILI9341_WritePixel(uint16_t x, uint16_t y, uint16_t color)
{
    uint8_t data[2] = { color >> 8, color & 0xFF };
    ILI9341_SetWindow(x, y, x, y);
    LCD_WR_REG(0x2c);
    DC_H();
    sendSPI(data, 2);
}

void ILI9341_DrawBitmap(uint16_t w, uint16_t h, uint8_t *s)
{
    LCD_WR_REG(0x2c);
    DC_H();
    sendSPI(s, w * h * 2);
}

void ILI9341_DrawBitmapDMA(uint16_t w, uint16_t h, uint8_t *s)
{
    LCD_WR_REG(0x2c);
    DC_H();
    HAL_SPI_Transmit_DMA(&hspi1, s, w * h * 2);
}

void ILI9341_EndOfDrawBitmap(void) {}

void ILI9341_Reset(void)
{
    RESET_L(); Delay(100);
    RESET_H(); Delay(100);
    CS_L();
}

void ILI9341_SoftReset(void)
{
    uint8_t cmd = 0x01;
    DC_L();
    sendSPI(&cmd, 1);
}

void LCD_WR_REG(uint8_t data)
{
    DC_L();
    sendSPI(&data, 1);
}

static void LCD_WR_DATA(uint8_t data)
{
    DC_H();
    sendSPI(&data, 1);
}

static void LCD_direction(LCD_Horizontal_t direction)
{
    switch (direction) {
    case ROTATE_0:   LCD_WR_REG(0x36); LCD_WR_DATA(0x48); break;
    case ROTATE_90:  LCD_WR_REG(0x36); LCD_WR_DATA(0x28); break;
    case ROTATE_180: LCD_WR_REG(0x36); LCD_WR_DATA(0x88); break;
    case ROTATE_270: LCD_WR_REG(0x36); LCD_WR_DATA(0xE8); break;
    }
}
