/**
 * adc_fft.h — ADC采集 + FFT频谱分析 (STM32G474)
 *
 * 功能:
 *   - ADC1 DMA 采集 (TIM2 触发, 250kHz)
 *   - CMSIS-DSP 实数 FFT 分析
 *   - 提取基频信号的 频率/幅度/相位
 *
 * 依赖:
 *   - CubeMX: ADC1_IN1(PA0), TIM2 TRGO 触发, DMA Circular
 *   - CMSIS-DSP 库: arm_math.h
 *
 * FFT 参数:
 *   - FFT_SIZE = 2048 点
 *   - 采样率 = 250kHz (TIM2)
 *   - 频率分辨率 ≈ 122Hz
 */

#ifndef __ADC_FFT_H
#define __ADC_FFT_H

#include "stm32g4xx_hal.h"
#include "arm_math.h"

/* ============================================================
 * 宏定义
 * ============================================================ */

#define ADC_BUF_SIZE    2048       /* ADC DMA 缓冲区大小 */
#define FFT_SIZE        2048       /* FFT 点数 (必须为 2 的幂) */

/* ============================================================
 * 数据结构
 * ============================================================ */

/**
 * FFT_Result — 一次 FFT 分析的完整结果
 */
typedef struct {
    float freq_hz;      /* 基频 (Hz) */
    float amplitude;    /* 归一化幅度 (0.0 ~ 1.0, 相对 ADC 满量程) */
    float phase_deg;    /* 相位 (度, -180 ~ +180) */
    float dc_offset;    /* 直流偏置 */
    uint16_t peak_idx;  /* 峰值所在 FFT bin 索引 */
} FFT_Result;

/* ============================================================
 * API 函数
 * ============================================================ */

/**
 * ADC_FFT_Init — 初始化 ADC 采集和 FFT
 *
 * 执行: ADC 校准 → 启动 DMA → 启动 TIM2
 * 之后 DMA 在后台循环采集，不占用 CPU。
 */
void ADC_FFT_Init(void);

/**
 * ADC_FFT_DataReady — 检查是否有新数据
 *
 * @return 0=还在采集中  1=数据就绪可分析
 *
 * 使用方式: 在主循环中轮询，返回 1 时调用 ADC_FFT_Analyze()
 */
uint8_t ADC_FFT_DataReady(void);

/**
 * ADC_FFT_Analyze — 对最新数据进行 FFT 分析
 *
 * @return FFT_Result 包含频率/幅度/相位
 *
 * 调用后自动重启 ADC 采集。
 */
FFT_Result ADC_FFT_Analyze(void);

/**
 * ADC_FFT_GetSampleRate — 返回当前 ADC 采样率 (Hz)
 */
float ADC_FFT_GetSampleRate(void);

#endif /* __ADC_FFT_H */
