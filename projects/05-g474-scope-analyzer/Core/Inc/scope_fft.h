/**
 * scope_fft.h — FFT 频谱分析 + 三校准集成
 *
 * 输入: ADC1 4096点信号缓冲 + 检波器电压 Vd
 * 输出: 原始域 mV (基频/谐波/Vpp/Vrms), 频率 →500Hz吸附, 相位 →φ_LPF修正
 *
 * FFT: 4096点, Hann窗, Fs=2.0MSPS, 分辨率 488.28Hz
 * 校准: ① ÷G_total(Vd, f) ② −φ_LPF(f)  (滤波器已移除, 要求1/2)
 *
 * 依赖: CMSIS-DSP (arm_math.h), scope_calib.h, scope_adc.h
 */

#ifndef __SCOPE_FFT_H
#define __SCOPE_FFT_H

#include "stm32g4xx_hal.h"

/* ============================================================
 * 宏定义
 * ============================================================ */

#define SCOPE_FFT_SIZE        4096
#define SCOPE_FS              2000000.0f
#define SCOPE_BIN_HZ          (SCOPE_FS / SCOPE_FFT_SIZE)   /* 488.28 Hz */
#define SCOPE_MAX_HARM          8       /* 最多检测 8 次谐波 */
#define SCOPE_PEAK_THRESH       0.05f   /* 相对基频幅值阈值 */
#define SCOPE_MIN_BIN           2       /* 跳过 bin 0(DC)+1(直流泄漏区) */

/* 500Hz 吸附 (题目约束: f₁ = N×500Hz) */
#define SCOPE_FREQ_STEP         500.0f

/* 谐波搜索窗口 (在整数倍位置 ±N bin 内搜索) */
#define SCOPE_HARM_SEARCH_BINS  2

/* ADC 参考 */
#define SCOPE_ADC_VREF          3.30f
#define SCOPE_ADC_MAX           4096.0f

/* ============================================================
 * 频谱分量
 * ============================================================ */
typedef struct {
    float freq_hz;          /* 频率 (Hz) — 谐波吸附到 f₁ 的整数倍 */
    float vpeak_mV;         /* 幅度 (mVpeak) — 原始域, H_chain+AGC 已修正 */
    float phase_rad;        /* 相位 (rad) — 原始域, φ_LPF 已修正 */
    uint16_t bin;           /* FFT bin 索引 */
    uint8_t  is_interference; /* 干扰标记 */
} ScopeHarmonic;

/* ============================================================
 * 完整测量结果 — 所有电压值均为原始输入域 mV
 * ============================================================ */
typedef struct {
    /* ── 频率 ── */
    float f1_hz;            /* 基频 (Hz), 500Hz吸附后, 误差 0 */
    float f1_raw_hz;        /* 基频 (Hz), 吸附前原始值 (调试用) */
    float bin_resolution;   /* FFT bin 分辨率 (Hz) */

    /* ── 电压时域 (原始域 mV) ── */
    float vpp_mV;           /* Vpp, 时域 peak-to-peak */
    float vrms_mV;          /* Vrms, 真有效值 */
    float vdc_mV;           /* ADC 直流偏置 */

    /* ── 基波 ── */
    float fund_vpeak_mV;    /* 基波 Vpeak (mV), 已修正 */
    float fund_phase_rad;   /* 基波相位 (rad), φ_LPF 已修正 */

    /* ── 谐波 ── */
    ScopeHarmonic harmonics[SCOPE_MAX_HARM];
    uint8_t harmonic_count;

    /* ── AGC/校准 ── */
    float vd_mV;            /* 检波器直流输出 (mV), ADC3采集 */
    float agc_gain;         /* AGC 线性增益 @基频 (调试用, ScopeAGC_ComputeGain 计算) */

    /* ── 置信度 ── */
    uint8_t confidence;     /* 0=HIGH (无干扰/验证通过), 1=MEDIUM, 2=LOW */
} ScopeResult;

/* ============================================================
 * API
 * ============================================================ */

/**
 * ScopeFFT_Init — 初始化 FFT 实例 (4096点, 启动时调用一次)
 */
void ScopeFFT_Init(void);

/**
 * ScopeFFT_Analyze — 完整分析 (单ADC, 三校准集成)
 *
 * 内部执行:
 *   ① 时域 Vdc/Vpp/Vrms
 *   ② 去直流 → Hann 窗 → FFT → 幅度谱
 *   ③ 峰值检测 → 基频 → 500Hz 吸附
 *   ④ 谐波搜索 (f₁整数倍 ±2bin)
 *   ⑤ 抛物线插值修正 (频率+幅度)
 *   ⑥ 校准链: ÷G_total(Vd, f) → −φ_LPF(f)
 *   ⑦ 转换为 mV 原始域
 *
 * @param signal_buf  ADC1 原始数据 uint16_t[4096]
 * @param len         数据长度 (必须 = 4096)
 * @param fs_hz       采样率 (= 2.0e6)
 * @param vd_mV       检波器直流输出 (mV), 来自 ADC3 采集
 * @return            完整测量结果 (mV 原始域)
 */
ScopeResult ScopeFFT_Analyze(const uint16_t *signal_buf,
                              uint16_t len,
                              float fs_hz,
                              float vd_mV);

/**
 * ScopeFFT_Print — 串口打印结果 (huart2)
 */
void ScopeFFT_Print(const ScopeResult *r);

#endif /* __SCOPE_FFT_H */
