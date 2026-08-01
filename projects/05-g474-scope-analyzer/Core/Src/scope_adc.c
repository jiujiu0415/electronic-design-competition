/**
 * scope_adc.c — ADC 采集驱动实现 (STM32G474)
 *
 * ADC1 (PA0): 信号波形, TIM2 TRGO @2.0MSPS, DMA Circular
 * ADC2 (PA1): 检波器直流, 软件触发单次
 *
 * 依赖: CubeMX 生成的 hadc1, hadc2, htim2, hdma_adc1
 */

#include "scope_adc.h"

extern ADC_HandleTypeDef  hadc1;
extern ADC_HandleTypeDef  hadc2;
extern TIM_HandleTypeDef  htim2;
extern DMA_HandleTypeDef  hdma_adc1;

static uint16_t adc1_signal_buf[SCOPE_ADC_SIGNAL_BUF_SIZE];
static volatile uint8_t adc1_ready = 0;

/* ── DMA 完成回调 ──────────────────────────────────── */

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == ADC1)
    {
        HAL_ADC_Stop_DMA(&hadc1);
        adc1_ready = 1;
    }
}

void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc)
{
    (void)hadc;
}

/* ── API 实现 ──────────────────────────────────────── */

void ScopeADC_Init(void)
{
    HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED);
    HAL_ADCEx_Calibration_Start(&hadc2, ADC_SINGLE_ENDED);

    HAL_TIM_Base_Start(&htim2);

    HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc1_signal_buf,
                      SCOPE_ADC_SIGNAL_BUF_SIZE);

    ScopeADC_SwitchOpen();
}

uint8_t ScopeADC_Ready(void)
{
    return adc1_ready;
}

uint16_t* ScopeADC_GetSignalBuffer(void)
{
    return adc1_signal_buf;
}

#define SCOPE_VD_OVERSAMPLE  16  /* Vd 过采样次数 — 降噪 √16=4x */

uint16_t ScopeADC_ReadEnvelope(void)
{
    uint32_t sum = 0;
    for (int i = 0; i < SCOPE_VD_OVERSAMPLE; i++)
    {
        HAL_ADC_Start(&hadc2);
        HAL_ADC_PollForConversion(&hadc2, 10);
        sum += HAL_ADC_GetValue(&hadc2);
        HAL_ADC_Stop(&hadc2);
    }
    return (uint16_t)(sum / SCOPE_VD_OVERSAMPLE);
}

void ScopeADC_Restart(void)
{
    adc1_ready = 0;
    HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc1_signal_buf,
                      SCOPE_ADC_SIGNAL_BUF_SIZE);
}

float ScopeADC_GetSampleRate(void)
{
    return SCOPE_ADC_SAMPLE_RATE;
}

/* ── 模拟开关 ──────────────────────────────────────── */

void ScopeADC_SwitchClose(void)
{
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
}

void ScopeADC_SwitchOpen(void)
{
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);
}
