/**
 * scope_adc.c — 双 ADC 同步采集实现 (STM32G474)
 *
 * 数据流:
 *   TIM2 TRGO @2MHz ─┬→ ADC1 CH1(PA0) → DMA1_CH1 → adc1_signal_buf[4096]
 *                     └→ ADC2 CH2(PA1) → DMA1_CH2 → adc2_envelope_buf[4096]
 *
 *   DMA 双通道采满一轮 → 硬件中断 → 置 ready 标志 → 主循环取出分析
 *
 * 依赖: CubeMX 已完成 ADC1+ADC2+TIM2+DMA 配置
 */

#include "scope_adc.h"

/* ============================================================
 * 外部引用 — CubeMX 生成
 * ============================================================ */
extern ADC_HandleTypeDef hadc1;
extern ADC_HandleTypeDef hadc2;
extern TIM_HandleTypeDef htim2;
extern DMA_HandleTypeDef hdma_adc1;
extern DMA_HandleTypeDef hdma_adc2;

/* ============================================================
 * 内部变量
 * ============================================================ */
static uint16_t adc1_signal_buf[SCOPE_ADC_SIGNAL_BUF_SIZE];
static uint16_t adc2_envelope_buf[SCOPE_ADC_ENVELOPE_BUF_SIZE];
static volatile uint8_t adc1_ready = 0;
static volatile uint8_t adc2_ready = 0;

/* ============================================================
 * HAL 回调 — DMA 传输完成
 * ============================================================ */

/**
 * HAL_ADC_ConvCpltCallback
 *
 * 每个 ADC 采满 4096 点各触发一次。立即停止该 ADC 的 DMA 防止覆盖。
 * 两个 ADC 都可能先完成（差几十 ns），各自的 ISR 会被尾链处理。
 */
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == ADC1)
    {
        HAL_ADC_Stop_DMA(&hadc1);
        adc1_ready = 1;
    }
    else if (hadc->Instance == ADC2)
    {
        HAL_ADC_Stop_DMA(&hadc2);
        adc2_ready = 1;
    }
}

/**
 * HAL_ADC_ConvHalfCpltCallback — 暂不使用
 */
void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc)
{
    (void)hadc;
}

/* ============================================================
 * API 实现
 * ============================================================ */

void ScopeADC_Init(void)
{
    /* ── 校准两个 ADC ── */
    HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED);
    HAL_ADCEx_Calibration_Start(&hadc2, ADC_SINGLE_ENDED);

    /* ── 启动 TIM2 (触发源, 两个 ADC 共享) ── */
    HAL_TIM_Base_Start(&htim2);

    /* ── 启动两个 ADC DMA (Circular) ── */
    /* TIM2 已经跑起来了, 两个 ADC 同时收到下一个 TRGO 沿 */
    HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc1_signal_buf,
                      SCOPE_ADC_SIGNAL_BUF_SIZE);
    HAL_ADC_Start_DMA(&hadc2, (uint32_t *)adc2_envelope_buf,
                      SCOPE_ADC_ENVELOPE_BUF_SIZE);
}

uint8_t ScopeADC_Ready(void)
{
    return (adc1_ready && adc2_ready) ? 1 : 0;
}

uint16_t* ScopeADC_GetSignalBuffer(void)
{
    return adc1_signal_buf;
}

uint16_t* ScopeADC_GetEnvelopeBuffer(void)
{
    return adc2_envelope_buf;
}

void ScopeADC_Restart(void)
{
    adc1_ready = 0;
    adc2_ready = 0;
    HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc1_signal_buf,
                      SCOPE_ADC_SIGNAL_BUF_SIZE);
    HAL_ADC_Start_DMA(&hadc2, (uint32_t *)adc2_envelope_buf,
                      SCOPE_ADC_ENVELOPE_BUF_SIZE);
}

float ScopeADC_GetSampleRate(void)
{
    return SCOPE_ADC_SAMPLE_RATE;
}
