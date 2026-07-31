/**
 * scope_fft.h — FFT 频谱分析 + 时域参数测量 + 干扰识别 + 交叉验证
 *
 * 输入: ADC1 信号缓冲 (uint16_t[4096]) + AGC 增益
 * 输出: 基频 f₁、Vpp、Vrms、谐波频率+幅值+相位
 *
 * 依赖: CMSIS-DSP (arm_math.h), scope_adc.h
 *
 * FFT 参数:
 *   N = 4096, Fs = 2.0 MSPS → 频率分辨率 = 488 Hz
 *   窗函数: Hann (幅度精度优先)
 *   峰值检测: 抛物线插值修正
 *   基频吸附: round(f₁_raw / 500) × 500
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

/* 交叉验证阈值 */
#define SCOPE_VERIFY_PARSEVAL_TOL  0.15f  /* Parseval 一致性: 15% */
#define SCOPE_VERIFY_VPP_TOL       0.20f  /* Vpp 交叉校验: 20% */
#define SCOPE_VERIFY_AMP_SUS_THRESH 0.15f /* 谐波幅值可疑阈值: 15% */
#define SCOPE_VERIFY_AMP_MATCH_TOL  0.20f /* 干扰幅值匹配容差: 20% */

/* AGC 参数 */
#define SCOPE_AGC_TARGET_VPP  3.00f    /* AD603 AGC 目标输出 Vpp (v4架构) */
#define SCOPE_AGC_GAIN_MIN    1.0f     /* AGC 增益下限 */
#define SCOPE_AGC_GAIN_MAX    100.0f   /* AGC 增益上限 (~40dB) */
#define SCOPE_INTERFERENCE_VPP 0.20f   /* 干扰 200mVpp (已知固定值) */
#define SCOPE_INTERFERENCE_VPEAK 0.10f /* 干扰 100mV peak */

/* ============================================================
 * 置信度
 * ============================================================ */
typedef enum {
    SCOPE_CONFIDENCE_HIGH   = 0,
    SCOPE_CONFIDENCE_MEDIUM = 1,
    SCOPE_CONFIDENCE_LOW    = 2
} ScopeConfidence;

/* ============================================================
 * 频谱分量 (新增相位 + 干扰标记)
 * ============================================================ */
typedef struct {
    float freq_hz;            /* 频率 (Hz) */
    float amplitude;          /* 幅度 (Vpeak, 正弦波峰值) */
    float phase_rad;          /* 相位 (radians, 相对分析窗起点) */
    uint16_t bin;             /* FFT bin 索引 */
    uint8_t is_interference;  /* 1 = 识别为干扰分量 */
} ScopeHarmonic;

/* ============================================================
 * 完整测量结果
 * ============================================================ */
typedef struct {
    /* ── 时域原始参数 (ADC1, 含干扰+偏置) ── */
    float vpp;                /* 峰峰值 (V), 总信号含干扰 */
    float vrms;               /* 真有效值 (V), 总信号含干扰 */
    float vdc;                /* 直流偏置 (V) */

    /* ── 频谱反推 (u_b 纯有用信号, 不含干扰) ── */
    float vpp_u_b;            /* u_b Vpp, 从频谱分量重建 */
    float vrms_u_b;           /* u_b Vrms, Parseval 定理 */

    /* ── 检波器 (ADC2) ── */
    float vpp_envelope;       /* ADC2 检波器总 Vpp (AGC 前) */

    /* ── AGC + 干扰估计 ── */
    float agc_gain;                  /* 估计的 AGC 线性增益 */
    float interference_expected_amp; /* 干扰预期 Vpeak (ADC域, AGC后) */
    uint8_t interference_peaks;      /* 检测到的干扰谱线数量 */

    /* ── 基频 ── */
    float fundamental_freq;   /* 基频 (Hz) */
    float fundamental_amp;    /* 基频幅度 (Vpeak) */

    /* ── 谐波 ── */
    ScopeHarmonic harmonics[SCOPE_MAX_HARM];
    uint8_t harmonic_count;

    /* ── 置信度 ── */
    ScopeConfidence confidence;

    /* ── 调试 ── */
    float freq_resolution;    /* 实际频率分辨率 (Hz) */
} ScopeResult;

/* ============================================================
 * API
 * ============================================================ */

/**
 * ScopeFFT_Init — 初始化 FFT 实例 (4096 点, 只调用一次)
 */
void ScopeFFT_Init(void);

/**
 * ScopeFFT_AnalyzeSimple — 单 ADC 完整分析
 *
 * @param signal_buf  ADC1 信号原始数据 (uint16_t[4096])
 * @param len         数据长度 (= 4096)
 * @param fs_hz       采样率 (= 2.0e6)
 * @param agc_gain    AGC 线性增益 (来自 ScopeAGC_ComputeGain)
 * @return            测量结果 (含基频/谐波/Vpp/Vrms)
 */
ScopeResult ScopeFFT_AnalyzeSimple(const uint16_t *signal_buf,
                                    uint16_t len, float fs_hz,
                                    float agc_gain);

/**
 * ScopeFFT_Print — 串口打印测量结果
 */
void ScopeFFT_Print(const ScopeResult *r);

#endif /* __SCOPE_FFT_H */
