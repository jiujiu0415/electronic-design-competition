/**
 * scope_adc.h — ADC 高速单通道采集 (STM32G474)
 *
 * 配置: ADC1, TIM2 触发 @2.0 MSPS, DMA Circular, 4096 点
 *       12-bit, PA0=IN1 Single-ended
 *
 * 用法:
 *   ScopeADC_Init();           // 初始化
 *   while (!ScopeADC_Ready()); // 等 DMA 采集满
 *   uint16_t *buf = ScopeADC_GetBuffer();  // 拿到 4096 个点
 *   ScopeADC_Restart();        // 重启下一轮
 */

#ifndef __SCOPE_ADC_H
#define __SCOPE_ADC_H

#include "stm32g4xx_hal.h"

/* ============================================================
 * 参数
 * ============================================================ */

#define SCOPE_ADC_BUF_SIZE    4096      /* FFT 点数 */
#define SCOPE_ADC_SAMPLE_RATE 2000000.0f /* 2.0 MSPS */

/* ============================================================
 * API
 * ============================================================ */

/**
 * ScopeADC_Init — 启动 ADC + TIM2 + DMA
 * 调用后开始采集，数据持续写入内部缓冲区
 */
void ScopeADC_Init(void);

/**
 * ScopeADC_Ready — 检查 DMA 是否采满一轮
 * @return 1=缓冲区已满可读取, 0=采集未完成
 */
uint8_t ScopeADC_Ready(void);

/**
 * ScopeADC_GetBuffer — 获取原始 ADC 数据指针
 * @return 4096 个 uint16_t (0~4095)
 */
uint16_t* ScopeADC_GetBuffer(void);

/**
 * ScopeADC_Restart — 重启 DMA 准备下一轮采集
 */
void ScopeADC_Restart(void);

/**
 * ScopeADC_GetSampleRate — 返回当前采样率 (Hz)
 */
float ScopeADC_GetSampleRate(void);

#endif /* __SCOPE_ADC_H */
