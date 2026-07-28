/**
 * bode_plot.h — Bode 幅频曲线绘制 (ST7789, 320×240 横屏)
 *
 * 依赖: st7789.h
 * 功能: 在 TFT 屏幕上绘制扫频结果的幅频曲线 (对数 X 轴 + dB Y 轴)
 */

#ifndef __BODE_PLOT_H
#define __BODE_PLOT_H

#include "st7789.h"
#include <stdint.h>

/*
 * Bode 绘制配置
 *
 * 坐标系 (横屏 320×240):
 *   ┌──────────────────────────────┐ y=0
 *   │  title                       │
 *   │  ┌──────────────────────┐    │
 *   │  │                      │    │
 *   │  │    绘图区域           │    │
 *   │  │    270×210 px        │    │
 *   │  │                      │    │
 *   │  └──────────────────────┘    │
 *   │  100Hz    1kHz   10kHz 100kHz│ y=239
 *   └──────────────────────────────┘
 *    x=0                         x=319
 *
 * X 轴: 对数频率 (PLOT_X1 ~ PLOT_X2 像素)
 * Y 轴: 增益 dB   (PLOT_Y1 ~ PLOT_Y2 像素)
 */

/* 绘图区域边界 (像素坐标) */
#define PLOT_X1    42
#define PLOT_X2    310
#define PLOT_Y1    8
#define PLOT_Y2    212

/* Y 轴 dB 范围 */
#define PLOT_DB_MIN   -32.0f
#define PLOT_DB_MAX    6.0f

/* 颜色方案 */
#define PLOT_BG         COLOR_BLACK
#define PLOT_GRID       COLOR_DARKGRAY
#define PLOT_AXIS       COLOR_LIGHTGRAY
#define PLOT_CURVE      COLOR_YELLOW
#define PLOT_TEXT       COLOR_WHITE
#define PLOT_TITLE      COLOR_CYAN
#define PLOT_MARKER_3DB COLOR_RED

/*
 * BodePlot_Draw — 一次性绘制完整 Bode 图
 *
 * @param freqs     频率数组 (Hz), 长度 n
 * @param gains_db  增益数组 (dB), 长度 n (passband 应归一化到 0dB)
 * @param n         数据点数
 * @param fc_hz     截止频率 (Hz), 0 表示不标注
 */
void BodePlot_Draw(const float *freqs, const float *gains_db,
                   uint16_t n, float fc_hz);

/*
 * BodePlot_DrawCurve — 只绘制曲线 (不清屏, 用于叠加多条曲线)
 */
void BodePlot_DrawCurve(const float *freqs, const float *gains_db,
                        uint16_t n, uint16_t color);

/*
 * BodePlot_Normalize — 归一化增益: 自动检测通带最大增益并平移
 *
 * @param freqs     频率数组 (Hz), 用于定位通带范围
 * @param gains_db  原始增益数组
 * @param n         数据点数
 * @param skip_low  跳过前 skip_low 个点 (低频交流耦合区)
 * @return          归一化偏移量 (加到原始值 = 归一化值)
 */
float BodePlot_Normalize(const float *freqs, const float *gains_db,
                         uint16_t n, uint16_t skip_low);

#endif /* __BODE_PLOT_H */
