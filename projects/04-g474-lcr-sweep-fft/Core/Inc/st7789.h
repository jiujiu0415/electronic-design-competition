/**
 * st7789.h — ST7789VW 2.4" TFT LCD 驱动头文件 (STM32G474)
 *
 * 接口: 4-wire SPI2 (Transmit Only Master, 8-bit, CPOL=Low, CPHA=1 Edge)
 * 控制: PB0=TFT_DC, PB1=TFT_CS, PA8=TFT_RST
 * 分辨率: 320×240 (横屏模式, MADCTL=MV|MX)
 * 像素格式: RGB565 (16-bit/pixel)
 *
 * 参考: ST7789VW 数据手册 + GMT024-01 模块规格书
 */

#ifndef __ST7789_H
#define __ST7789_H

#include "stm32g4xx_hal.h"
#include <stdint.h>

/* ── 屏幕尺寸 (横屏模式) ── */
#define ST7789_WIDTH   320
#define ST7789_HEIGHT  240

/* ── RGB565 常用颜色 ── */
#define RGB565(r, g, b)  ((((uint16_t)(r) >> 3) << 11) | \
                           (((uint16_t)(g) >> 2) << 5)  | \
                            ((uint16_t)(b) >> 3))

#define COLOR_BLACK       0x0000
#define COLOR_WHITE       0xFFFF
#define COLOR_RED         0xF800
#define COLOR_GREEN       0x07E0
#define COLOR_BLUE        0x001F
#define COLOR_YELLOW      0xFFE0
#define COLOR_CYAN        0x07FF
#define COLOR_MAGENTA     0xF81F
#define COLOR_GRAY        0x8410
#define COLOR_DARKGRAY    0x4208
#define COLOR_LIGHTGRAY   0xC618
#define COLOR_ORANGE      0xFC00
#define COLOR_DARKBLUE    0x0010

/* ── API ── */
void ST7789_Init(void);
void ST7789_SetWindow(uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye);
void ST7789_FillScreen(uint16_t color);
void ST7789_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void ST7789_DrawPixel(uint16_t x, uint16_t y, uint16_t color);
void ST7789_DrawHLine(uint16_t x, uint16_t y, uint16_t len, uint16_t color);
void ST7789_DrawVLine(uint16_t x, uint16_t y, uint16_t len, uint16_t color);
void ST7789_DrawLine(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color);
void ST7789_DrawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void ST7789_DrawChar(uint16_t x, uint16_t y, char c, uint16_t color, uint16_t bg);
void ST7789_DrawString(uint16_t x, uint16_t y, const char *str,
                       uint16_t color, uint16_t bg);
void ST7789_DrawStringN(uint16_t x, uint16_t y, const char *str,
                        uint16_t n, uint16_t color, uint16_t bg);

#endif /* __ST7789_H */
