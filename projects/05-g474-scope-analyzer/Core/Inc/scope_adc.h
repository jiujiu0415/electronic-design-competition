/**
 * scope_adc.h — ADC1+2 交替采集 + ADC3 检波器驱动 (STM32G474)
 *
 * ── v3: Dual Interleaved 4MSPS (2026-07-31) ──
 *
 * ADC1+2 (PA0): 交替采集信号波形, TIM2 TRGO @2MHz 触发 → 等效 4MSPS
 *   DMA1_CH1 Circular, Word (32-bit), 从 ADC_CDR 搬运 4096 个 32-bit 字
 *   CDR[15:0]=ADC1, CDR[31:16]=ADC2, 解包后共 8192 个采样点
 *
 * ADC3 (PB1): 独立采集检波器直流电压
 *   软件触发单次转换, 无需 DMA
 *
 * GPIO PA4: 模拟开关控制 (LOW=断开u_J, HIGH=闭合u_J)
 *
 * FFT: 8192 @4MSPS → 频率分辨率 488 Hz
 * 时间窗: 2.048ms → 满足 ≤2s
 *
 * 依赖: CubeMX 生成的 hadc1, hadc2, hadc3, htim2, hdma_adc1
 */

#ifndef __SCOPE_ADC_H
#define __SCOPE_ADC_H

#include "stm32g4xx_hal.h"

/* ============================================================
 * 宏定义
 * ============================================================ */

/** DMA 缓冲: 4096 个 32-bit Word, 每个含 ADC1+ADC2 各一值 */
#define SCOPE_ADC_DMA_BUF_SIZE      4096
/** 解包后总采样点数: 4096 × 2 = 8192 */
#define SCOPE_ADC_SAMPLE_COUNT      8192
/** 交替等效采样率: 2MHz 触发 × 2 = 4.0 MSPS */
#define SCOPE_ADC_SAMPLE_RATE       4000000.0f
/** FFT 大小: 与采样点数一致 */
#define SCOPE_ADC_FFT_SIZE          8192

/* ============================================================
 * API
 * ============================================================ */

/**
 * ScopeADC_Init — 初始化交替 ADC + ADC3 + 模拟开关 GPIO
 *
 * 内部流程:
 *   1. 校准 ADC1, ADC2, ADC3
 *   2. 启动 TIM2
 *   3. 启动 ADC2 (Slave, 无 DMA)
 *   4. 启动 ADC1 (Master, MultiMode DMA)
 */
void ScopeADC_Init(void);

/**
 * ScopeADC_Ready — DMA 是否完成一轮采集
 * @return 1 = 已采满 4096 Word (8192 采样点), 0 = 等待中
 */
uint8_t ScopeADC_Ready(void);

/**
 * ScopeADC_GetInterleavedBuffer — 获取 DMA 原始缓冲
 * @return uint32_t[4096], CDR 格式:
 *         每个 word: [ADC2_data << 16 | ADC1_data]
 *         有效期为 Ready→Restart 之间
 */
const uint32_t* ScopeADC_GetInterleavedBuffer(void);

/**
 * ScopeADC_ReadEnvelope — 读检波器直流电压 (ADC3 单次转换)
 * @return 12-bit ADC 原始值 (0~4095)
 *
 * 调用时机: DMA 完成后调用即可, 不要求实时。
 * 如果 ADC3 正在忙, 返回 0（极少发生, 因为 ADC3 不连续转换）。
 */
uint16_t ScopeADC_ReadEnvelope(void);

/**
 * ScopeADC_Restart — 重启交替 DMA, 准备下一轮采集
 */
void ScopeADC_Restart(void);

/**
 * ScopeADC_GetSampleRate — 返回采样率 (Hz)
 */
float ScopeADC_GetSampleRate(void);

/* ── 模拟开关 (PA4) ─────────────────────────────────────── */

/**
 * ScopeADC_SwitchClose — 闭合模拟开关, 接入 u_J 干扰
 */
void ScopeADC_SwitchClose(void);

/**
 * ScopeADC_SwitchOpen — 断开模拟开关, 屏蔽 u_J 干扰
 */
void ScopeADC_SwitchOpen(void);

#endif /* __SCOPE_ADC_H */
