/**
 * bode_plot.c — Bode 幅频曲线绘制实现
 *
 * 在 ST7789 320×240 横屏上绘制对数频率 vs dB 增益曲线
 *
 * 坐标变换:
 *   x_px = PLOT_X1 + log10(f / FREQ_MIN) / LOG_RANGE * PLOT_W
 *   y_px = PLOT_Y2 - (gain - PLOT_DB_MIN) / DB_RANGE * PLOT_H
 */

#include "bode_plot.h"
#include <math.h>
#include <stdio.h>

/* 绘图区域尺寸 */
#define PLOT_W  (PLOT_X2 - PLOT_X1)   /* 268 */
#define PLOT_H  (PLOT_Y2 - PLOT_Y1)   /* 204 */

/* X 轴频率范围 */
#define FREQ_MIN     100.0f
#define FREQ_MAX     100000.0f
#define LOG_RANGE    (log10f(FREQ_MAX) - log10f(FREQ_MIN))  /* 3.0 */

/* Y 轴 dB 范围 */
#define DB_RANGE     (PLOT_DB_MAX - PLOT_DB_MIN)             /* 38.0 */

/* 网格线 — 频率标注点 (Hz): 每 10 倍频程内 3 条 */
static const float grid_freqs[] = {
    100.0f, 200.0f, 500.0f,
    1000.0f, 2000.0f, 5000.0f,
    10000.0f, 20000.0f, 50000.0f,
    100000.0f
};
#define GRID_F_COUNT (sizeof(grid_freqs) / sizeof(grid_freqs[0]))

/* ================================================================
 * 坐标变换
 * ================================================================ */

/*
 * freq_to_x — 频率 → 像素 X 坐标
 */
static uint16_t freq_to_x(float freq_hz)
{
    float ratio = log10f(freq_hz / FREQ_MIN) / LOG_RANGE;
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;
    return (uint16_t)(PLOT_X1 + ratio * PLOT_W + 0.5f);
}

/*
 * gain_to_y — 增益 dB → 像素 Y 坐标
 */
static uint16_t gain_to_y(float gain_db)
{
    float ratio = (gain_db - PLOT_DB_MIN) / DB_RANGE;
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;
    /* Y 轴翻转: dB 越高 → 像素 Y 越小 (屏幕上方) */
    return (uint16_t)(PLOT_Y2 - ratio * PLOT_H + 0.5f);
}

/* ================================================================
 * 轴 + 网格 + 标签
 * ================================================================ */

static void draw_axes_and_grid(void)
{
    char buf[16];

    /* ── 绘图区域边框 ── */
    ST7789_DrawRect(PLOT_X1 - 1, PLOT_Y1 - 1,
                    PLOT_W + 2, PLOT_H + 2, PLOT_AXIS);

    /* ── 水平网格线 + Y 轴标签 (dB) ── */
    for (int db = (int)PLOT_DB_MIN; db <= (int)PLOT_DB_MAX; db += 10)
    {
        uint16_t y = gain_to_y((float)db);

        /* 网格线 */
        if (db > PLOT_DB_MIN && db < PLOT_DB_MAX)
            ST7789_DrawHLine(PLOT_X1, y, PLOT_W, PLOT_GRID);

        /* Y 轴标签: 右侧对齐到 PLOT_X1-3 */
        snprintf(buf, sizeof(buf), "%d", db);
        uint8_t len = 0; while (buf[len]) len++;
        ST7789_DrawString(PLOT_X1 - 4 - len * 6, y - 3,
                          buf, PLOT_TEXT, PLOT_BG);
    }

    /* ── 垂直网格线 + X 轴标签 (频率) ── */
    for (uint8_t i = 0; i < GRID_F_COUNT; i++)
    {
        float f = grid_freqs[i];
        uint16_t x = freq_to_x(f);

        /* 网格线 (decade 线用实线, 其他用暗线) */
        int is_decade = (f == 100.0f || f == 1000.0f ||
                         f == 10000.0f || f == 100000.0f);
        uint16_t grid_color = is_decade ? PLOT_GRID : COLOR_DARKGRAY;
        ST7789_DrawVLine(x, PLOT_Y1, PLOT_H, grid_color);

        /* X 轴标签: 居中, 在绘图区下方 */
        if (f >= 1000.0f)
            snprintf(buf, sizeof(buf), "%.0fk", f / 1000.0f);
        else
            snprintf(buf, sizeof(buf), "%.0f", f);

        uint8_t len = 0; while (buf[len]) len++;
        int16_t lx = (int16_t)x - (int16_t)(len * 6) / 2;
        if (lx < 0) lx = 0;
        ST7789_DrawString((uint16_t)lx, PLOT_Y2 + 4,
                          buf, PLOT_TEXT, PLOT_BG);
    }

    /* ── Y 轴标签 "dB" (左上角) ── */
    ST7789_DrawString(PLOT_X1 + 2, PLOT_Y1 + 2, "dB", PLOT_CYAN, PLOT_BG);
}

/* ================================================================
 * 曲线绘制
 * ================================================================ */

void BodePlot_DrawCurve(const float *freqs, const float *gains_db,
                        uint16_t n, uint16_t color)
{
    if (n < 2) return;

    for (uint16_t i = 0; i < n - 1; i++)
    {
        uint16_t x0 = freq_to_x(freqs[i]);
        uint16_t y0 = gain_to_y(gains_db[i]);
        uint16_t x1 = freq_to_x(freqs[i + 1]);
        uint16_t y1 = gain_to_y(gains_db[i + 1]);

        ST7789_DrawLine(x0, y0, x1, y1, color);
    }
}

/* ================================================================
 * 归一化
 * ================================================================ */

float BodePlot_Normalize(const float *gains_db, uint16_t n,
                         uint16_t skip_low)
{
    if (skip_low >= n) skip_low = 0;

    /* 在 skip_low ~ 跳过最高频几个点的范围内找最大增益 */
    uint16_t end = n;
    if (end > n - 3) end = (n > 3) ? n - 3 : n;

    /* 搜索范围: 1kHz ~ 10kHz (通带中心区, 避免找在噪声峰上) */
    uint16_t search_start = skip_low;
    uint16_t search_end   = end;
    for (uint16_t i = skip_low; i < end; i++)
    {
        if (freqs[i] >= 1000.0f) { search_start = i; break; }
    }
    for (uint16_t i = search_start; i < end; i++)
    {
        if (freqs[i] > 12000.0f) { search_end = i; break; }
    }

    float max_gain = gains_db[search_start];
    for (uint16_t i = search_start; i < search_end; i++)
    {
        if (gains_db[i] > max_gain)
            max_gain = gains_db[i];
    }

    return -max_gain;  /* 归一化偏移: 加到原始值 → 通带 = 0dB */
}

/* ================================================================
 * 完整绘制
 * ================================================================ */

void BodePlot_Draw(const float *freqs, const float *gains_db,
                   uint16_t n, float fc_hz)
{
    char buf[32];
    uint16_t y;

    /* ── ① 清屏 ── */
    ST7789_FillScreen(PLOT_BG);

    /* ── ② 标题行 ── */
    ST7789_DrawString(2, 0, "Bode Plot", PLOT_TITLE, PLOT_BG);

    /* 显示截止频率 */
    if (fc_hz > 0.0f)
    {
        if (fc_hz >= 1000.0f)
            snprintf(buf, sizeof(buf), "fc=%.1fkHz", fc_hz / 1000.0f);
        else
            snprintf(buf, sizeof(buf), "fc=%.0fHz", fc_hz);
        ST7789_DrawString(80, 0, buf, PLOT_TEXT, PLOT_BG);
    }

    /* ── ③ 轴 + 网格 ── */
    draw_axes_and_grid();

    /* ── ④ 绘制曲线 ── */
    BodePlot_DrawCurve(freqs, gains_db, n, PLOT_CURVE);

    /* ── ⑤ -3dB 标记线 ── */
    y = gain_to_y(-3.0f);
    ST7789_DrawHLine(PLOT_X1, y, PLOT_W, PLOT_MARKER_3DB);
    ST7789_DrawString(PLOT_X1 + 4, y - 7, "-3dB",
                      PLOT_MARKER_3DB, PLOT_BG);

    /* ── ⑥ 底部状态栏 ── */
    ST7789_DrawHLine(0, ST7789_HEIGHT - 12, ST7789_WIDTH, PLOT_GRID);
    snprintf(buf, sizeof(buf), "Sweep:%dpts %.0f-%.0fkHz",
             n, freqs[0] / 1000.0f, freqs[n - 1] / 1000.0f);
    ST7789_DrawString(2, ST7789_HEIGHT - 10, buf, PLOT_TEXT, PLOT_BG);
}
