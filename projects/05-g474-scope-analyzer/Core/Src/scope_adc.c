/**
 * scope_adc.c — ADC 高速单通道采集实现 (STM32G474)
 *
 * 数据流:
 *   TIM2 TRGO @2MHz → ADC1 CH1(PA0) → DMA Circular → adc_buf[4096]
 *   DMA 采满一轮 → 硬件中断 → 置 ready 标志 → 主循环取出分析
 *
 * 依赖: CubeMX 已完成 ADC1+TIM2+DMA 配置
 */

#include "scope_adc.h"

/* ============================================================
 * 外部引用 — CubeMX 生成
 * ============================================================ */
extern ADC_HandleTypeDef hadc1;
extern TIM_HandleTypeDef htim2;
extern DMA_HandleTypeDef hdma_adc1;

/* ============================================================
 * 内部变量
 * ============================================================ */
static uint16_t adc_buf[SCOPE_ADC_BUF_SIZE];
static volatile uint8_t ready_flag = 0;

/* ============================================================
 * HAL 回调 — DMA 传输完成
 * ============================================================ */

/**
 * HAL_ADC_ConvCpltCallback
 *
 * DMA Circular 模式下，每采满一轮（4096 点）触发一次。
 * 收到后暂停 DMA（保护数据不被覆盖），置 ready 标志。
 */
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == ADC1)
    {
        HAL_ADC_Stop_DMA(&hadc1);
        ready_flag = 1;
    }
}

/**
 * HAL_ADC_ConvHalfCpltCallback
 *
 * 半满中断暂时不用，留空占位。
 * 后续如需双缓冲连续采集，在这里处理前半段。
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
    /* ── 校准 ADC ── */
    HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED);

    /* ── 启动 TIM2 (触发源) ── */
    HAL_TIM_Base_Start(&htim2);

    /* ── 启动 ADC DMA (Circular) ── */
    HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_buf, SCOPE_ADC_BUF_SIZE);
}

uint8_t ScopeADC_Ready(void)
{
    return ready_flag;
}

uint16_t* ScopeADC_GetBuffer(void)
{
    return adc_buf;
}

void ScopeADC_Restart(void)
{
    ready_flag = 0;
    HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_buf, SCOPE_ADC_BUF_SIZE);
}

float ScopeADC_GetSampleRate(void)
{
    return SCOPE_ADC_SAMPLE_RATE;
}
