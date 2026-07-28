/**
 * st7789.c — ST7789VW 2.4" TFT LCD 驱动实现
 *
 * 横屏模式: 320×240, 左上角原点, RGB565
 *
 * 硬件:
 *   SPI2:   PB13=SCK, PB15=MOSI (8-bit, CPOL=Low, CPHA=1 Edge, 42.5MHz)
 *   GPIO:   PB0=TFT_DC, PB1=TFT_CS, PA8=TFT_RST
 */

#include "st7789.h"
#include "main.h"   /* TFT_DC_Pin, TFT_CS_Pin, TFT_RST_Pin 等 */

extern SPI_HandleTypeDef hspi2;

/* ================================================================
 * 5×7 像素 ASCII 字体 (0x20 ' ' ~ 0x7E '~')
 * 每字符 5 字节，每字节 = 一列像素 (bit0=顶行, bit6=底行, bit7=空)
 * 字符宽度 6px (5 数据列 + 1 空白间距)
 * 共 95 字符 × 5 字节 = 475 字节
 * ================================================================ */
static const uint8_t font5x7[95][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, /*   spc */
    {0x00, 0x00, 0x5F, 0x00, 0x00}, /* ! */
    {0x00, 0x07, 0x00, 0x07, 0x00}, /* " */
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, /* # */
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, /* $ */
    {0x23, 0x13, 0x08, 0x64, 0x62}, /* % */
    {0x36, 0x49, 0x55, 0x22, 0x50}, /* & */
    {0x00, 0x05, 0x03, 0x00, 0x00}, /* ' */
    {0x00, 0x1C, 0x22, 0x41, 0x00}, /* ( */
    {0x00, 0x41, 0x22, 0x1C, 0x00}, /* ) */
    {0x08, 0x2A, 0x1C, 0x2A, 0x08}, /* * */
    {0x08, 0x08, 0x3E, 0x08, 0x08}, /* + */
    {0x00, 0x50, 0x30, 0x00, 0x00}, /* , */
    {0x08, 0x08, 0x08, 0x08, 0x08}, /* - */
    {0x00, 0x60, 0x60, 0x00, 0x00}, /* . */
    {0x20, 0x10, 0x08, 0x04, 0x02}, /* / */
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, /* 0 */
    {0x00, 0x42, 0x7F, 0x40, 0x00}, /* 1 */
    {0x42, 0x61, 0x51, 0x49, 0x46}, /* 2 */
    {0x21, 0x41, 0x45, 0x4B, 0x31}, /* 3 */
    {0x18, 0x14, 0x12, 0x7F, 0x10}, /* 4 */
    {0x27, 0x45, 0x45, 0x45, 0x39}, /* 5 */
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, /* 6 */
    {0x01, 0x71, 0x09, 0x05, 0x03}, /* 7 */
    {0x36, 0x49, 0x49, 0x49, 0x36}, /* 8 */
    {0x06, 0x49, 0x49, 0x29, 0x1E}, /* 9 */
    {0x00, 0x36, 0x36, 0x00, 0x00}, /* : */
    {0x00, 0x56, 0x36, 0x00, 0x00}, /* ; */
    {0x00, 0x08, 0x14, 0x22, 0x41}, /* < */
    {0x14, 0x14, 0x14, 0x14, 0x14}, /* = */
    {0x41, 0x22, 0x14, 0x08, 0x00}, /* > */
    {0x02, 0x01, 0x51, 0x09, 0x06}, /* ? */
    {0x32, 0x49, 0x79, 0x41, 0x3E}, /* @ */
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, /* A */
    {0x7F, 0x49, 0x49, 0x49, 0x36}, /* B */
    {0x3E, 0x41, 0x41, 0x41, 0x22}, /* C */
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, /* D */
    {0x7F, 0x49, 0x49, 0x49, 0x41}, /* E */
    {0x7F, 0x09, 0x09, 0x01, 0x01}, /* F */
    {0x3E, 0x41, 0x41, 0x51, 0x32}, /* G */
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, /* H */
    {0x00, 0x41, 0x7F, 0x41, 0x00}, /* I */
    {0x20, 0x40, 0x41, 0x3F, 0x01}, /* J */
    {0x7F, 0x08, 0x14, 0x22, 0x41}, /* K */
    {0x7F, 0x40, 0x40, 0x40, 0x40}, /* L */
    {0x7F, 0x02, 0x04, 0x02, 0x7F}, /* M */
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, /* N */
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, /* O */
    {0x7F, 0x09, 0x09, 0x09, 0x06}, /* P */
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, /* Q */
    {0x7F, 0x09, 0x19, 0x29, 0x46}, /* R */
    {0x46, 0x49, 0x49, 0x49, 0x31}, /* S */
    {0x01, 0x01, 0x7F, 0x01, 0x01}, /* T */
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, /* U */
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, /* V */
    {0x7F, 0x20, 0x18, 0x20, 0x7F}, /* W */
    {0x63, 0x14, 0x08, 0x14, 0x63}, /* X */
    {0x03, 0x04, 0x78, 0x04, 0x03}, /* Y */
    {0x61, 0x51, 0x49, 0x45, 0x43}, /* Z */
    {0x00, 0x00, 0x7F, 0x41, 0x41}, /* [ */
    {0x02, 0x04, 0x08, 0x10, 0x20}, /* \ */
    {0x41, 0x41, 0x7F, 0x00, 0x00}, /* ] */
    {0x04, 0x02, 0x01, 0x02, 0x04}, /* ^ */
    {0x40, 0x40, 0x40, 0x40, 0x40}, /* _ */
    {0x00, 0x01, 0x02, 0x04, 0x00}, /* ` */
    {0x20, 0x54, 0x54, 0x54, 0x78}, /* a */
    {0x7F, 0x48, 0x44, 0x44, 0x38}, /* b */
    {0x38, 0x44, 0x44, 0x44, 0x20}, /* c */
    {0x38, 0x44, 0x44, 0x48, 0x7F}, /* d */
    {0x38, 0x54, 0x54, 0x54, 0x18}, /* e */
    {0x08, 0x7E, 0x09, 0x01, 0x02}, /* f */
    {0x08, 0x14, 0x54, 0x54, 0x3C}, /* g */
    {0x7F, 0x08, 0x04, 0x04, 0x78}, /* h */
    {0x00, 0x44, 0x7D, 0x40, 0x00}, /* i */
    {0x20, 0x40, 0x44, 0x3D, 0x00}, /* j */
    {0x00, 0x7F, 0x10, 0x28, 0x44}, /* k */
    {0x00, 0x41, 0x7F, 0x40, 0x00}, /* l */
    {0x7C, 0x04, 0x18, 0x04, 0x78}, /* m */
    {0x7C, 0x08, 0x04, 0x04, 0x78}, /* n */
    {0x38, 0x44, 0x44, 0x44, 0x38}, /* o */
    {0x7C, 0x14, 0x14, 0x14, 0x08}, /* p */
    {0x08, 0x14, 0x14, 0x18, 0x7C}, /* q */
    {0x7C, 0x08, 0x04, 0x04, 0x08}, /* r */
    {0x48, 0x54, 0x54, 0x54, 0x20}, /* s */
    {0x04, 0x3F, 0x44, 0x40, 0x20}, /* t */
    {0x3C, 0x40, 0x40, 0x20, 0x7C}, /* u */
    {0x1C, 0x20, 0x40, 0x20, 0x1C}, /* v */
    {0x3C, 0x40, 0x30, 0x40, 0x3C}, /* w */
    {0x44, 0x28, 0x10, 0x28, 0x44}, /* x */
    {0x0C, 0x50, 0x50, 0x50, 0x3C}, /* y */
    {0x44, 0x64, 0x54, 0x4C, 0x44}, /* z */
    {0x00, 0x08, 0x36, 0x41, 0x00}, /* { */
    {0x00, 0x00, 0x7F, 0x00, 0x00}, /* | */
    {0x00, 0x41, 0x36, 0x08, 0x00}, /* } */
    {0x08, 0x04, 0x08, 0x10, 0x08}, /* ~ */
};

/* 字体参数 */
#define FONT_W      6   /* 字符宽: 5px 数据 + 1px 间距 */
#define FONT_H      8   /* 字符高: 7px 字形 + 1px 行距 */
#define CHAR_W      5   /* 字形数据宽度 */

/* 行缓冲区: 一行最多 320 像素 × 2 字节/像素 = 640 字节 */
static uint8_t  line_buf[ST7789_WIDTH * 2];
static uint16_t text_fg = COLOR_WHITE;
static uint16_t text_bg = COLOR_BLACK;

/* ================================================================
 * 底层 SPI 操作
 * ================================================================ */

/*
 * ST7789_SendCmd — 发命令字节 (DC=0)
 */
static void ST7789_SendCmd(uint8_t cmd)
{
    HAL_GPIO_WritePin(TFT_DC_GPIO_Port, TFT_DC_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_RESET);
    HAL_SPI_Transmit(&hspi2, &cmd, 1, 10);
    HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_SET);
}

/*
 * ST7789_SendData — 发数据字节 (DC=1)
 */
static void ST7789_SendData(uint8_t data)
{
    HAL_GPIO_WritePin(TFT_DC_GPIO_Port, TFT_DC_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_RESET);
    HAL_SPI_Transmit(&hspi2, &data, 1, 10);
    HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_SET);
}

/*
 * ST7789_SendDataBulk — 发送多个字节数据 (DC=1, 一次 CS)
 */
static void ST7789_SendDataBulk(const uint8_t *data, uint16_t len)
{
    if (len == 0) return;
    HAL_GPIO_WritePin(TFT_DC_GPIO_Port, TFT_DC_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_RESET);
    HAL_SPI_Transmit(&hspi2, (uint8_t *)data, len, 100);
    HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_SET);
}

/*
 * ST7789_SendData16Bulk — 发送 16-bit 像素数组 (RGB565)
 *
 * 送 16-bit SPI2 (MSB first) → D15-D8 先发, D7-D0 后发
 * 由于 SPI2 配置为 8-bit 模式, 每个 uint16_t 需要拆分发送
 */
static void ST7789_SendData16Bulk(const uint16_t *data, uint16_t len)
{
    if (len == 0) return;
    /* 拼接成字节流: 每像素 2 字节, MSB first */
    uint16_t buf_size = len * 2;
    if (buf_size > sizeof(line_buf)) buf_size = sizeof(line_buf);

    HAL_GPIO_WritePin(TFT_DC_GPIO_Port, TFT_DC_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_RESET);

    uint16_t remaining = len;

    while (remaining > 0)
    {
        uint16_t chunk = remaining;
        uint16_t chunk_bytes = chunk * 2;
        if (chunk_bytes > sizeof(line_buf)) {
            chunk = sizeof(line_buf) / 2;
            chunk_bytes = chunk * 2;
        }

        /* 转换为 MSB-first 字节序 (SPI 直接发 uint16 即为 MSB first) */
        for (uint16_t i = 0; i < chunk; i++)
        {
            line_buf[i * 2]     = (uint8_t)(data[0] >> 8);   /* D15-D8 */
            line_buf[i * 2 + 1] = (uint8_t)(data[0]);        /* D7-D0  */
            data++;
        }

        HAL_SPI_Transmit(&hspi2, line_buf, chunk_bytes, 100);
        remaining -= chunk;
    }

    HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_SET);
}

/* ================================================================
 * 初始化序列
 * ================================================================ */

void ST7789_Init(void)
{
    /* ── ① 硬件复位 ── */
    HAL_GPIO_WritePin(TFT_RST_GPIO_Port, TFT_RST_Pin, GPIO_PIN_RESET);
    HAL_Delay(10);
    HAL_GPIO_WritePin(TFT_RST_GPIO_Port, TFT_RST_Pin, GPIO_PIN_SET);
    HAL_Delay(120);

    /* ── ② 软件复位 ── */
    ST7789_SendCmd(0x01);  /* SWRESET */
    HAL_Delay(150);

    /* ── ③ 退出睡眠 ── */
    ST7789_SendCmd(0x11);  /* SLPOUT */
    HAL_Delay(120);

    /* ── ④ 像素格式: 16-bit RGB565 ── */
    ST7789_SendCmd(0x3A);  /* COLMOD */
    ST7789_SendData(0x55);

    /* ── ⑤ 内存访问控制: 横屏, 左上角原点 ──
     * MV=1 (行列交换: 240×320 → 320×240)
     * MX=1 (列从左到右)
     * RGB=0 (RGB 顺序)
     * → MADCTL = 0x60
     */
    ST7789_SendCmd(0x36);  /* MADCTL */
    ST7789_SendData(0x60);

    /* ── ⑥ 帧速率控制 (可选, 默认亦可) ── */
    ST7789_SendCmd(0xB2);  /* PORCTRL */
    ST7789_SendData(0x0C);
    ST7789_SendData(0x0C);
    ST7789_SendData(0x00);
    ST7789_SendData(0x33);
    ST7789_SendData(0x33);

    ST7789_SendCmd(0xB7);  /* GCTRL */
    ST7789_SendData(0x35);

    /* ── ⑦ VCOM 设置 ── */
    ST7789_SendCmd(0xBB);  /* VCOMS */
    ST7789_SendData(0x19);

    ST7789_SendCmd(0xC0);  /* LCMCTRL */
    ST7789_SendData(0x2C);

    ST7789_SendCmd(0xC2);  /* VDVVRHEN */
    ST7789_SendData(0x01);

    ST7789_SendCmd(0xC3);  /* VRHS */
    ST7789_SendData(0x12);

    ST7789_SendCmd(0xC4);  /* VDVS */
    ST7789_SendData(0x20);

    ST7789_SendCmd(0xC6);  /* FRCTRL2 */
    ST7789_SendData(0x0F);

    ST7789_SendCmd(0xD0);  /* PWCTRL1 */
    ST7789_SendData(0xA4);
    ST7789_SendData(0xA1);

    /* ── ⑧ Gamma 校正 (正/负极性) ── */
    ST7789_SendCmd(0xE0);  /* PVGAMCTRL */
    {
        static const uint8_t pgamma[] = {
            0xD0, 0x04, 0x0D, 0x11, 0x13, 0x2B, 0x3F, 0x54,
            0x4C, 0x18, 0x0D, 0x0B, 0x1F, 0x23
        };
        ST7789_SendDataBulk(pgamma, 14);
    }

    ST7789_SendCmd(0xE1);  /* NVGAMCTRL */
    {
        static const uint8_t ngamma[] = {
            0xD0, 0x04, 0x0C, 0x11, 0x13, 0x2C, 0x3F, 0x44,
            0x51, 0x2F, 0x1F, 0x1F, 0x20, 0x23
        };
        ST7789_SendDataBulk(ngamma, 14);
    }

    /* ── ⑨ 显示反色 (ST7789 通常需要) ── */
    ST7789_SendCmd(0x21);  /* INVON */

    /* ── ⑩ 正常显示 + 开启 ── */
    ST7789_SendCmd(0x13);  /* NORON */
    ST7789_SendCmd(0x29);  /* DISPON */
    HAL_Delay(20);

    /* ── 清屏为黑色 ── */
    ST7789_FillScreen(COLOR_BLACK);
}

/* ================================================================
 * 窗口设置 + 填充
 * ================================================================ */

void ST7789_SetWindow(uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye)
{
    /* CASET: 列地址 */
    ST7789_SendCmd(0x2A);
    ST7789_SendData(xs >> 8);
    ST7789_SendData(xs & 0xFF);
    ST7789_SendData(xe >> 8);
    ST7789_SendData(xe & 0xFF);

    /* RASET: 行地址 */
    ST7789_SendCmd(0x2B);
    ST7789_SendData(ys >> 8);
    ST7789_SendData(ys & 0xFF);
    ST7789_SendData(ye >> 8);
    ST7789_SendData(ye & 0xFF);
}

void ST7789_FillScreen(uint16_t color)
{
    ST7789_FillRect(0, 0, ST7789_WIDTH, ST7789_HEIGHT, color);
}

void ST7789_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if (w == 0 || h == 0) return;

    ST7789_SetWindow(x, y, x + w - 1, y + h - 1);

    /* 构建一行填充数据 (line_buf 一次最多 320 像素 = 640 字节) */
    uint16_t pixels_per_chunk = sizeof(line_buf) / 2;
    if (pixels_per_chunk > w) pixels_per_chunk = w;

    /* 预填一行 */
    for (uint16_t i = 0; i < pixels_per_chunk; i++)
    {
        line_buf[i * 2]     = (uint8_t)(color >> 8);
        line_buf[i * 2 + 1] = (uint8_t)(color);
    }

    uint32_t total = (uint32_t)w * h;
    uint32_t sent  = 0;

    ST7789_SendCmd(0x2C);  /* RAMWR */

    HAL_GPIO_WritePin(TFT_DC_GPIO_Port, TFT_DC_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_RESET);

    while (sent < total)
    {
        uint32_t remain = total - sent;
        uint16_t chunk  = (remain < pixels_per_chunk) ?
                          (uint16_t)remain : pixels_per_chunk;

        HAL_SPI_Transmit(&hspi2, line_buf, chunk * 2, 100);
        sent += chunk;
    }

    HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_SET);
}

/* ================================================================
 * 像素 + 线段绘制
 * ================================================================ */

void ST7789_DrawPixel(uint16_t x, uint16_t y, uint16_t color)
{
    if (x >= ST7789_WIDTH || y >= ST7789_HEIGHT) return;

    ST7789_SetWindow(x, y, x, y);
    ST7789_SendCmd(0x2C);  /* RAMWR */
    ST7789_SendData(color >> 8);
    ST7789_SendData(color & 0xFF);
}

void ST7789_DrawHLine(uint16_t x, uint16_t y, uint16_t len, uint16_t color)
{
    if (x + len > ST7789_WIDTH) len = ST7789_WIDTH - x;
    ST7789_FillRect(x, y, len, 1, color);
}

void ST7789_DrawVLine(uint16_t x, uint16_t y, uint16_t len, uint16_t color)
{
    if (y + len > ST7789_HEIGHT) len = ST7789_HEIGHT - y;
    ST7789_FillRect(x, y, 1, len, color);
}

void ST7789_DrawLine(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
                     uint16_t color)
{
    int16_t dx = (x1 > x0) ? (int16_t)(x1 - x0) : -(int16_t)(x0 - x1);
    int16_t dy = (y1 > y0) ? (int16_t)(y1 - y0) : -(int16_t)(y0 - y1);
    int16_t sx = (x0 < x1) ? 1 : -1;
    int16_t sy = (y0 < y1) ? 1 : -1;
    int16_t err = dx - dy;

    while (1)
    {
        ST7789_DrawPixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int16_t e2 = err * 2;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

void ST7789_DrawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                     uint16_t color)
{
    ST7789_DrawHLine(x, y, w, color);
    ST7789_DrawHLine(x, y + h - 1, w, color);
    ST7789_DrawVLine(x, y, h, color);
    ST7789_DrawVLine(x + w - 1, y, h, color);
}

/* ================================================================
 * 字符 / 字符串绘制
 * ================================================================ */

void ST7789_DrawChar(uint16_t x, uint16_t y, char c,
                     uint16_t color, uint16_t bg)
{
    if (c < 0x20 || c > 0x7E) c = '?';
    if (x + FONT_W > ST7789_WIDTH || y + FONT_H > ST7789_HEIGHT) return;

    const uint8_t *glyph = font5x7[c - 0x20];

    ST7789_SetWindow(x, y, x + FONT_W - 1, y + FONT_H - 1);
    ST7789_SendCmd(0x2C);

    HAL_GPIO_WritePin(TFT_DC_GPIO_Port, TFT_DC_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_RESET);

    /* 逐行扫描输出: 每行 6 像素 = 12 字节 RGB565 */
    for (uint8_t row = 0; row < FONT_H; row++)
    {
        for (uint8_t col = 0; col < FONT_W; col++)
        {
            uint16_t px_color = bg;
            if (col < CHAR_W && row < 7)
            {
                if (glyph[col] & (1 << row))
                    px_color = color;
            }
            line_buf[col * 2]     = (uint8_t)(px_color >> 8);
            line_buf[col * 2 + 1] = (uint8_t)(px_color);
        }
        HAL_SPI_Transmit(&hspi2, line_buf, FONT_W * 2, 10);
    }

    HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_SET);
}

void ST7789_DrawString(uint16_t x, uint16_t y, const char *str,
                       uint16_t color, uint16_t bg)
{
    while (*str)
    {
        if (x + FONT_W > ST7789_WIDTH) break;
        ST7789_DrawChar(x, y, *str, color, bg);
        x += FONT_W;
        str++;
    }
}

void ST7789_DrawStringN(uint16_t x, uint16_t y, const char *str,
                        uint16_t n, uint16_t color, uint16_t bg)
{
    for (uint16_t i = 0; i < n && str[i]; i++)
    {
        if (x + FONT_W > ST7789_WIDTH) break;
        ST7789_DrawChar(x, y, str[i], color, bg);
        x += FONT_W;
    }
}
