/**
 * adc_fft.h — ADC采集 + FFT频谱分析 v2 (STM32G474)
 *
 * 新增:
 *   - 多峰值检测 (叠加信号分离)
 *   - 过零检测辅助测频 (精度远高于 FFT 分辨率)
 *   - 谐波分析, 自动识别波形类型
 *   - 相位警告 (单通道相位会漂, 需双通道测相位差)
 *
 * 依赖:
 *   - CubeMX: ADC1_IN1(PA0), TIM2 TRGO 触发, DMA Circular
 *   - CMSIS-DSP 库
 */

#ifndef __ADC_FFT_H
#define __ADC_FFT_H

#include "stm32g4xx_hal.h"
#include "arm_math.h"

/* ============================================================
 * 宏定义
 * ============================================================ */

#define ADC_BUF_SIZE    2048
#define FFT_SIZE        2048
#define MAX_PEAKS       8       /* 最多检测的频谱峰个数 */
#define PEAK_THRESHOLD  0.02f   /* 高度低于基频峰值 2% 的峰忽略 */

/* ============================================================
 * 数据结构
 * ============================================================ */

/**
 * PeakInfo — 一个频谱峰的信息
 */
typedef struct {
    float freq_hz;      /* 频率 (Hz) */
    float amplitude;    /* 归一化幅度 (0~1) */
    float phase_deg;    /* 相位 (度), ⚠️ 单通道会漂移! */
} PeakInfo;

/**
 * FFT_Result — 一次 FFT 分析的完整结果
 */
typedef struct {
    PeakInfo peaks[MAX_PEAKS];  /* 检测到的频谱峰 (按幅度降序) */
    uint8_t  peak_count;        /* 实际峰数 */
    float    dc_offset_v;       /* 直流偏置 (V) */
    float    freq_zc;           /* 过零检测频率 (Hz), 比 FFT 更准 */
    char     waveform[16];      /* 波形类型: SINE / TRIANGLE / SQUARE / SAWTOOTH / COMPOSITE */
} FFT_Result;

/* ============================================================
 * API 函数
 * ============================================================ */

void       ADC_FFT_Init(void);          /* 初始化, 启动采集 */
uint8_t    ADC_FFT_DataReady(void);     /* 数据就绪? */
FFT_Result ADC_FFT_Analyze(void);       /* 分析, 返回完整结果 */
float      ADC_FFT_GetSampleRate(void); /* 采样率 (Hz) */

#endif
