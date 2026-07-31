/**
 * scope_fft.h — FFT 频谱分析 + 时域参数测量 + 干扰识别 + 交叉验证
 *
 * v3: Dual Interleaved 4MSPS (2026-07-31)
 *
 * 输入: float[8192] (已解包+去直流的时间顺序采样值)
 *   — 由 scope_adc.c 的 CDR 32-bit DMA 缓冲解包得到
 *   — 采样率 4.0 MSPS, 交替: ADC1@t0, ADC2@t250ns, ADC1@t500ns, ...
 *
 * 输出: 基频 f₁、Vpp、Vrms、谐波频率+幅值+相位、干扰识别、置信度
 *
 * 依赖: CMSIS-DSP (arm_math.h), scope_adc.h, scope_calib.h
 *
 * FFT 参数:
 *   N = 8192, Fs = 4.0 MSPS → 频率分辨率 = 488 Hz
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

#define SCOPE_FFT_SIZE    8192
#define SCOPE_FS          4000000.0f
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
 * ScopeFFT_Init — 初始化 FFT 实例 (8192 点, 只调用一次)
 *
 * 分配 arm_rfft_fast_instance_f32, 可在 CCM SRAM 分配 FFT 缓冲。
 */
void ScopeFFT_Init(void);

/**
 * ScopeFFT_AnalyzeInterleaved — v3 交替数据完整分析
 *
 * 输入是已从 CDR 解包的 float 数组 (8192 样本, 4MSPS)。
 * AGC 增益由 scope_calib 提供, 用于反算原始信号幅值。
 *
 * 内部流程:
 *   1. 去直流 (均值)
 *   2. Hann 窗
 *   3. arm_rfft_fast_f32 → 8192 实数 FFT
 *   4. 幅度谱 + 抛物线插值峰值检测
 *   5. 500Hz 基频吸附
 *   6. 谐波搜索 (整数倍验证)
 *   7. 干扰识别 (非整数倍孤立峰)
 *   8. 时域重建 Vpp
 *   9. Parseval Vrms
 *
 * @param fft_in     FFT 输入缓冲 float[8192] (可放 CCM)
 * @param len        数据长度 (= 8192)
 * @param fs_hz      采样率 (= 4.0e6)
 * @param agc_gain   AGC 线性增益 (来自 ScopeAGC_ComputeGain)
 * @return           测量结果 (含置信度)
 */
ScopeResult ScopeFFT_AnalyzeInterleaved(float *fft_in,
                                         uint16_t len, float fs_hz,
                                         float agc_gain);

/**
 * ScopeFFT_Print — 串口打印测量结果
 */
void ScopeFFT_Print(const ScopeResult *r);

#endif /* __SCOPE_FFT_H */
