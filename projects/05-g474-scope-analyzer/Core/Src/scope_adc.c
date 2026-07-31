/**
 * scope_adc.c — ADC1+2 交替采集 + ADC3 检波器 实现 (STM32G474)
 *
 * v3: Dual Interleaved 4MSPS (2026-07-31)
 *
 * 数据流:
 *   TIM2 TRGO @2MHz ─→ ADC1 CH1(PA0) ──┐ Dual Interleaved
 *                     └→ ADC2 CH1(PA0) ──┘
 *                           ↓
 *                     ADC_CDR (32-bit)
 *                           ↓ DMA1_CH1 Circular, Word
 *                     uint32_t dmabuf[4096]
 *                           ↓ 解包
 *                     8192 个 uint16_t 采样点 → FFT
 *
 * ADC3: PB1, 软件触发单次 → 检波器直流 Vd(raw)
 *
 * 模拟开关: PA4 GPIO Output
 *
 * 依赖: CubeMX 生成的 hadc1, hadc2, hadc3, htim2, hdma_adc1
 */

#include "scope_adc.h"

/* ============================================================
 * 外部引用 — CubeMX 生成
 * ============================================================ */
extern ADC_HandleTypeDef  hadc1;
extern ADC_HandleTypeDef  hadc2;
extern ADC_HandleTypeDef  hadc3;
extern TIM_HandleTypeDef  htim2;
extern DMA_HandleTypeDef  hdma_adc1;

/* ============================================================
 * 内部变量
 * ============================================================ */

/** DMA 缓冲: 4096 个 32-bit word @SRAM1 (DMA 可访问) */
static uint32_t dmabuf[SCOPE_ADC_DMA_BUF_SIZE];

/** DMA 完成标志 */
static volatile uint8_t dma_ready = 0;

/* ============================================================
 * HAL 回调 — DMA 传输完成
 * ============================================================ */

/**
 * HAL_ADC_ConvCpltCallback
 *
 * 在 Dual Interleaved + MDMA mode 2 下:
 *   每个 DMA Word 搬运完成后不触发中断 (HalfCplt/ConvCplt 指 DMA 流)。
 *   当 Circular DMA 完成一轮 (4096 Word) 后触发此回调。
 *
 * 此时立即停止 DMA，防止覆盖数据。
 */
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == ADC1)
    {
        /* 停止 Master ADC DMA (也会停 Slave) */
        HAL_ADCEx_MultiModeStop_DMA(&hadc1);
        dma_ready = 1;
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
    /* ── 1. 校准三个 ADC ──
     * 数据手册建议每次上电后校准一次。
     * ADC1/2 用于高速交替, ADC3 用于直流读取。
     */
    HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED);
    HAL_ADCEx_Calibration_Start(&hadc2, ADC_SINGLE_ENDED);
    HAL_ADCEx_Calibration_Start(&hadc3, ADC_SINGLE_ENDED);

    /* ── 2. 启动 TIM2 (TRGO @2MHz, 两个 ADC 共享) ── */
    HAL_TIM_Base_Start(&htim2);

    /* ── 3. 启动 ADC2 (Slave, 无 DMA) ──
     * ⚠️ Slave 必须先于 Master 启动 (HAL 要求)
     */
    HAL_ADC_Start(&hadc2);

    /* ── 4. 启动 ADC1 (Master, MultiMode DMA) ──
     * HAL_ADCEx_MultiModeStart_DMA() 是 Dual Mode 专用 API。
     * 替代 HAL_ADC_Start_DMA()。
     *
     * DMA 参数 (由 CubeMX 配置):
     *   - 数据宽度: Word (32-bit)
     *   - 模式: Circular
     *   - 目标: ADC_CDR (固定地址)
     */
    HAL_ADCEx_MultiModeStart_DMA(&hadc1, (uint32_t *)dmabuf, SCOPE_ADC_DMA_BUF_SIZE);

    /* ── 5. 模拟开关默认断开 (无干扰) ── */
    ScopeADC_SwitchOpen();
}

uint8_t ScopeADC_Ready(void)
{
    return dma_ready;
}

const uint32_t* ScopeADC_GetInterleavedBuffer(void)
{
    return dmabuf;
}

uint16_t ScopeADC_ReadEnvelope(void)
{
    /* ADC3 可能正在忙 (虽然配置了 Discontinuous=Disable) */
    if (HAL_ADC_Start(&hadc3) != HAL_OK)
        return 0;

    if (HAL_ADC_PollForConversion(&hadc3, 10) != HAL_OK)
        return 0;

    return (uint16_t)HAL_ADC_GetValue(&hadc3);
}

void ScopeADC_Restart(void)
{
    dma_ready = 0;

    /* ADC2 (Slave) 先启动 */
    HAL_ADC_Start(&hadc2);

    /* ADC1 (Master) 后启动 */
    HAL_ADCEx_MultiModeStart_DMA(&hadc1, (uint32_t *)dmabuf, SCOPE_ADC_DMA_BUF_SIZE);
}

float ScopeADC_GetSampleRate(void)
{
    return SCOPE_ADC_SAMPLE_RATE;
}

/* ── 模拟开关 ─────────────────────────────────────────── */

void ScopeADC_SwitchClose(void)
{
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
}

void ScopeADC_SwitchOpen(void)
{
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);
}
