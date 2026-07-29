/**
 * scope_fft.h — FFT 频谱分析 + 时域参数测量
 *
 * 输入: 4096 点 ADC 原始数据 (uint16_t)
 * 输出: 基频 f₁、Vpp、Vrms、谐波频率 + 幅值
 *
 * 依赖: CMSIS-DSP (arm_math.h), scope_adc.h
 *
 * FFT 参数:
 *   N = 4096, Fs = 2.0 MSPS → 频率分辨率 = 488 Hz
 *   窗函数: Hann (幅度精度优先)
 *   峰值检测: 抛物线插值修正
 */

#ifndef __SCOPE_FFT_H
#define __SCOPE_FFT_H

#include "stm32g4xx_hal.h"
#include "arm_math.h"

/* ============================================================
 * 宏定义
 * ============================================================ */

#define SCOPE_FFT_SIZE    4096
#define SCOPE_FS          2000000.0f
#define SCOPE_BIN_HZ      (SCOPE_FS / SCOPE_FFT_SIZE)  /* 488.28 Hz */
#define SCOPE_MAX_HARM     8          /* 最多检测 8 次谐波 */
#define SCOPE_PEAK_THRESH  0.05f      /* 峰值阈值: 低于基频5%的忽略 */
#define SCOPE_MIN_BIN       2          /* 跳过 bin 0/1 (DC+近DC) */

/* ============================================================
 * 频谱分量
 * ============================================================ */
typedef struct {
    float freq_hz;       /* 频率 (Hz) */
    float amplitude;     /* 幅度 (Vpeak, 正弦波峰值) */
    uint16_t bin;        /* FFT bin 索引 */
} ScopeHarmonic;

/* ============================================================
 * 完整测量结果
 * ============================================================ */
typedef struct {
    /* 时域参数 */
    float vpp;           /* 峰峰值 (V) */
    float vrms;          /* 真有效值 (V) */
    float vdc;           /* 直流偏置 (V) */

    /* 频域参数 */
    float fundamental_freq;    /* 基频 (Hz) */
    float fundamental_amp;     /* 基频幅度 (Vpeak) */

    /* 谐波 */
    ScopeHarmonic harmonics[SCOPE_MAX_HARM];
    uint8_t harmonic_count;

    /* 调试 */
    float freq_resolution;     /* 实际频率分辨率 (Hz) */
} ScopeResult;

/* ============================================================
 * API
 * ============================================================ */

/**
 * ScopeFFT_Init — 初始化 FFT 实例 (只调用一次)
 */
void ScopeFFT_Init(void);

/**
 * ScopeFFT_Analyze — 对原始 ADC 数据执行完整分析
 *
 * @param raw_buf  ADC 原始数据 (uint16_t[4096], 值范围 0~4095)
 * @param len      数据长度 (= 4096)
 * @param fs_hz    采样率 (Hz, = 2.0e6)
 * @return         完整测量结果
 */
ScopeResult ScopeFFT_Analyze(const uint16_t *raw_buf, uint16_t len, float fs_hz);

/**
 * ScopeFFT_Print — 串口打印测量结果 (方便调试)
 */
void ScopeFFT_Print(const ScopeResult *r);

#endif /* __SCOPE_FFT_H */
