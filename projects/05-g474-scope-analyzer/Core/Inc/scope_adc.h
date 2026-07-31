/**
 * scope_adc.h — ADC 采集驱动 (STM32G474)
 *
 * ADC1 (PA0): 信号波形, TIM2 TRGO @2.0MSPS, DMA Circular, 4096 点
 * ADC2 (PA1): 检波器直流, 软件触发单次
 * GPIO PA4:   模拟开关 (LOW=断开u_J, HIGH=闭合u_J)
 *
 * 用法:
 *   ScopeADC_Init();
 *   while (!ScopeADC_Ready());
 *   uint16_t *signal = ScopeADC_GetSignalBuffer();
 *   uint16_t vd_raw  = ScopeADC_ReadEnvelope();
 *   ... 分析 ...
 *   ScopeADC_Restart();
 */

#ifndef __SCOPE_ADC_H
#define __SCOPE_ADC_H

#include "stm32g4xx_hal.h"

/* ============================================================
 * 宏定义
 * ============================================================ */

#define SCOPE_ADC_SIGNAL_BUF_SIZE    4096
#define SCOPE_ADC_SAMPLE_RATE        2000000.0f

/* ============================================================
 * API
 * ============================================================ */

void ScopeADC_Init(void);
uint8_t ScopeADC_Ready(void);
uint16_t* ScopeADC_GetSignalBuffer(void);
uint16_t ScopeADC_ReadEnvelope(void);
void ScopeADC_Restart(void);
float ScopeADC_GetSampleRate(void);

/* ── 模拟开关 ─────────────────────────────────────── */

void ScopeADC_SwitchClose(void);   /* PA4 HIGH → 接入 u_J */
void ScopeADC_SwitchOpen(void);    /* PA4 LOW  → 断开 u_J */

#endif /* __SCOPE_ADC_H */
