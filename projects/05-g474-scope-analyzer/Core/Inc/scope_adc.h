/**
 * scope_adc.h — 双 ADC 同步采集驱动
 *
 * ADC1 (PA0): 信号输入 — 经 AGC + 1.6V 偏置, 0~3.2V 单极性
 * ADC2 (PA1): 检波器输出 — 直流电压 = 加法器总信号 Vpp
 *
 * 两个 ADC 由 TIM2 TRGO 同步触发 @2.0 MSPS
 * 各自由独立 DMA Circular 通道搬运 (ADC1→DMA1_CH1, ADC2→DMA1_CH2)
 *
 * 用法:
 *   ScopeADC_Init();
 *   while (!ScopeADC_Ready());
 *   uint16_t *signal   = ScopeADC_GetSignalBuffer();    // ADC1
 *   uint16_t *envelope = ScopeADC_GetEnvelopeBuffer();  // ADC2
 *   ... 分析 ...
 *   ScopeADC_Restart();
 *
 * 依赖: STM32G4xx HAL (hadc1, hadc2, htim2, hdma_adc1, hdma_adc2)
 */

#ifndef __SCOPE_ADC_H
#define __SCOPE_ADC_H

#include "stm32g4xx_hal.h"

/* ============================================================
 * 宏定义
 * ============================================================ */

#define SCOPE_ADC_SIGNAL_BUF_SIZE    4096     /* ADC1 信号缓冲 */
#define SCOPE_ADC_ENVELOPE_BUF_SIZE  4096     /* ADC2 检波器缓冲 */
#define SCOPE_ADC_SAMPLE_RATE        2000000.0f

/* ============================================================
 * API
 * ============================================================ */

/**
 * ScopeADC_Init — 初始化双 ADC 同步采集
 * 校准 ADC1+ADC2, 启动 TIM2 触发, 启动两个 DMA
 */
void ScopeADC_Init(void);

/**
 * ScopeADC_Ready — 双 ADC 是否均完成一轮采集
 * @return 1 = 两个 ADC 都已采满 4096 点, 0 = 等待中
 */
uint8_t ScopeADC_Ready(void);

/**
 * ScopeADC_GetSignalBuffer — 获取 ADC1 信号缓冲区
 * @return uint16_t[4096], 0~4095, 有效期为 Ready→Restart 之间
 */
uint16_t* ScopeADC_GetSignalBuffer(void);

/**
 * ScopeADC_GetEnvelopeBuffer — 获取 ADC2 检波器缓冲区
 * @return uint16_t[4096], 0~4095, 有效期为 Ready→Restart 之间
 */
uint16_t* ScopeADC_GetEnvelopeBuffer(void);

/**
 * ScopeADC_Restart — 重启双 ADC DMA, 准备下一轮采集
 */
void ScopeADC_Restart(void);

/**
 * ScopeADC_GetSampleRate — 返回采样率 (Hz)
 */
float ScopeADC_GetSampleRate(void);

#endif /* __SCOPE_ADC_H */
