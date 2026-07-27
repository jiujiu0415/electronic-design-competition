/**
 * adc_dual.h — ADC1 双通道同步采集 + FFT 分析 (STM32G474)
 *
 * 功能:
 *   ADC1 CH1(PA0) + CH2(PA1) 同步采集 → 分离 → FFT → 幅相比
 *   用于 LCR 网络扫频: CH1=输入参考, CH2=输出响应
 *
 * 依赖:
 *   CubeMX 配置完成: ADC1 双通道 Scan, TIM2 触发, DMA Circular
 *   CMSIS-DSP 库 (arm_math.h)
 */

#ifndef __ADC_DUAL_H
#define __ADC_DUAL_H

#include "stm32g4xx_hal.h"
#include "arm_math.h"

/* ============================================================
 * 宏定义
 * ============================================================ */

#define FFT_SIZE        2048                     /* FFT 点数 */
#define ADC_BUF_SIZE    (FFT_SIZE * 2)           /* DMA 缓冲区: 两通道交错 = 4096 */

/* ============================================================
 * 单通道测量结果
 * ============================================================ */
typedef struct {
    float freq_hz;       /* 检测到的频率 (Hz) */
    float amplitude;     /* 归一化幅度 (0~1, 对应 0~3.3V) */
    float phase_deg;     /* 相位 (度, -180~+180) */
    float dc_offset_v;   /* 直流偏置 (V) */
} ADC_ChannelResult;

/* ============================================================
 * 双通道对比结果 — LCR 传输函数 H(jω) = Vout/Vin
 * ============================================================ */
typedef struct {
    float freq_hz;          /* 当前频率 (Hz) */
    float gain;             /* 幅度比 = |Vout| / |Vin| */
    float gain_db;          /* 增益 = 20*log10(gain) */
    float phase_diff_deg;   /* 相位差 = ∠Vout - ∠Vin (度) */
    ADC_ChannelResult ch1;  /* 输入通道原始结果 */
    ADC_ChannelResult ch2;  /* 输出通道原始结果 */
} ADC_DualResult;

/* ============================================================
 * API 函数
 * ============================================================ */

/**
 * ADC_Dual_Init — 初始化 ADC + DMA + FFT
 * 调用后 ADC 开始采集 (DMA Circular), 数据持续流入 adc_buf
 */
void ADC_Dual_Init(void);

/**
 * ADC_Dual_DataReady — 检查 DMA 是否完成一轮采集
 * @return 1 = 缓冲区已满, 可以取数据; 0 = 还在采集中
 */
uint8_t ADC_Dual_DataReady(void);

/**
 * ADC_Dual_Analyze — 对已采集的数据执行双通道 FFT 分析
 * @param target_freq_hz  目标频率 (扫频当前频率), 用于精确提取该频点的幅相
 * @return 双通道对比结果 (增益 + 相位差)
 *
 * 调用后自动重启 DMA, 准备下一轮采集。
 */
ADC_DualResult ADC_Dual_Analyze(float target_freq_hz);

/**
 * ADC_Dual_SetSampleRate — 动态调整采样率 (Hz)
 *
 * 根据扫频频率自适应:
 *   目标: 采集 ~20 个信号周期, 保证 FFT 有足够的频率分辨率
 *   范围: 10kHz ~ 500kHz
 *
 * 内部会停止并重启 DMA, 调用前需确保上一轮采集已完成。
 */
void ADC_Dual_SetSampleRate(float target_freq_hz);

/**
 * ADC_Dual_GetSampleRate — 返回当前采样率 (Hz)
 */
float ADC_Dual_GetSampleRate(void);

#endif /* __ADC_DUAL_H */
