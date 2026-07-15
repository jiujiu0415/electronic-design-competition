/**
 * @file    ad9833.c
 * @brief   AD9833 DDS 驱动实现
 * @note    使用 SPI1 + PA4(FSYNC)，SPI Mode 2 (CPOL=1, CPHA=0)
 *          每次传输 16 bits，FSYNC 在传输完成后自动拉高
 */

#include "ad9833.h"
#include <stdio.h>

/* ================================================================
 * 低层 SPI 操作
 * ================================================================ */

/**
 * @brief  向 AD9833 写入 16-bit 数据
 * @param  data: 16-bit 数据
 * @note   FSYNC 先拉低，传输完成后再拉高（FSYNC 上升沿锁存）
 *         这是标准 3 线 SPI 时序
 */
static void AD9833_WriteRegister(uint16_t data)
{
    /* 拉低 FSYNC，开始传输 */
    HAL_GPIO_WritePin(AD9833_FSYNC_PORT, AD9833_FSYNC_PIN, GPIO_PIN_RESET);

    /* SPI 发送 16-bit (MSB first) */
    HAL_SPI_Transmit(AD9833_SPI_HANDLE, (uint8_t *)&data, 1, HAL_MAX_DELAY);

    /* 拉高 FSYNC，数据锁存 */
    HAL_GPIO_WritePin(AD9833_FSYNC_PORT, AD9833_FSYNC_PIN, GPIO_PIN_SET);
}

/**
 * @brief  写控制寄存器 (D15-D14 = 00)
 * @param  ctrl_word: 16-bit 控制字
 */
static void AD9833_WriteControl(uint16_t ctrl_word)
{
    AD9833_WriteRegister(AD9833_REG_CTRL | (ctrl_word & 0x3FFF));
}

/**
 * @brief  写频率寄存器 (28-bit，分两次 14-bit 写入)
 * @param  reg_addr: 频率寄存器地址 (AD9833_REG_FREQ0 或 AD9833_REG_FREQ1)
 * @param  freq_word: 28-bit 频率字
 * @note   B28=1 模式下，连续两次 16-bit 写入完成 28-bit 频率字配置
 *         第1次: 低14位 (D13=0, D12=1)
 *         第2次: 高14位 (D13=0, D12=0)
 *         实际写入时在寄存器地址基础上拼接数据位
 */
static void AD9833_WriteFreqRegister(uint16_t reg_addr, uint32_t freq_word)
{
    uint16_t lsb, msb;

    /* 频率字限制在 28-bit */
    freq_word &= 0x0FFFFFFF;

    /* 低14位: D15-D14 = 01(寄存器), D13=0(freq), D12=0(LSB), D11-D0 = freq[13:0] */
    lsb = reg_addr | (uint16_t)(freq_word & 0x3FFF);
    /* 高14位: D15-D14 = 01(寄存器), D13=0(freq), D12=1(MSB), D11-D0 = freq[27:14] */
    msb = reg_addr | (uint16_t)((freq_word >> 14) & 0x3FFF);

    AD9833_WriteRegister(lsb);
    AD9833_WriteRegister(msb);
}

/**
 * @brief  写相位寄存器 (12-bit)
 * @param  reg_addr: 相位寄存器地址 (AD9833_REG_PHASE0 或 AD9833_REG_PHASE1)
 * @param  phase_word: 12-bit 相位字
 * @note   相位偏移 = 2π × (PHASE / 4096)
 */
static void AD9833_WritePhaseRegister(uint16_t reg_addr, uint16_t phase_word)
{
    /* D15-D14 = 11(相位reg), D13=0, D12=0, D11-D0 = 相位值 */
    uint16_t data = reg_addr | (phase_word & 0x0FFF);
    AD9833_WriteRegister(data);
}

/* ================================================================
 * 高层 API 实现
 * ================================================================ */

void AD9833_Init(AD9833_Dev_t *dev)
{
    /* 确保 FSYNC 初始为高 */
    HAL_GPIO_WritePin(AD9833_FSYNC_PORT, AD9833_FSYNC_PIN, GPIO_PIN_SET);

    /* 初始控制字: B28=1 (连续写频率), 软件控制, RESET=1 */
    dev->ctrl_reg = AD9833_B28 | AD9833_RESET;
    AD9833_WriteControl(dev->ctrl_reg);

    /* 默认参数 */
    dev->frequency    = 1000.0f;     /* 1kHz */
    dev->phase        = 0.0f;
    dev->wave_type    = WAVE_SINE;
    dev->freq_reg_val = 0;
    dev->phase_reg_val = 0;

    /* 写入默认频率 */
    AD9833_SetFrequency(dev, dev->frequency);

    /* 释放复位，开始输出 */
    dev->ctrl_reg &= ~AD9833_RESET;
    AD9833_WriteControl(dev->ctrl_reg);

    HAL_Delay(1);  /* 等待稳定 */
}

void AD9833_SetFrequency(AD9833_Dev_t *dev, float freq_hz)
{
    uint32_t freq_word;

    /* 频率范围检查 */
    if (freq_hz < 0.0f)  freq_hz = 0.0f;
    if (freq_hz > (float)(AD9833_MCLK / 2))
        freq_hz = (float)(AD9833_MCLK / 2);

    /* 计算 28-bit 频率字: FREQ = f_out × 2^28 / MCLK */
    freq_word = (uint32_t)(freq_hz * AD9833_FREQ_SCALE / (float)AD9833_MCLK);

    /* 写入 FREQ0 寄存器 */
    AD9833_WriteFreqRegister(AD9833_REG_FREQ0, freq_word);

    /* 更新设备状态 */
    dev->frequency    = freq_hz;
    dev->freq_reg_val = (uint16_t)(freq_word & 0xFFFF);
}

void AD9833_SetPhase(AD9833_Dev_t *dev, float phase_rad)
{
    uint16_t phase_word;
    float    phase_normalized;

    /* 归一化到 [0, 2π) */
    while (phase_rad < 0.0f)         phase_rad += (float)(2.0 * M_PI);
    while (phase_rad >= (float)(2.0 * M_PI)) phase_rad -= (float)(2.0 * M_PI);

    /* 转换为 12-bit 相位字: PHASE = phase × 4096 / 2π */
    phase_normalized = phase_rad / (float)(2.0 * M_PI);
    phase_word = (uint16_t)(phase_normalized * 4096.0f);

    /* 写入 PHASE0 寄存器 */
    AD9833_WritePhaseRegister(AD9833_REG_PHASE0, phase_word);

    dev->phase         = phase_rad;
    dev->phase_reg_val = phase_word;
}

void AD9833_SetWaveType(AD9833_Dev_t *dev, WaveType_t wave)
{
    /* 清除波形相关位: MODE, OPBITEN, DIV2, SIGN_PIB */
    dev->ctrl_reg &= ~(AD9833_MODE | AD9833_OPBITEN | AD9833_DIV2 | AD9833_SIGN_PIB);

    switch (wave)
    {
        case WAVE_SINE:
            /* 正弦波: MODE=0, OPBITEN=0 (DAC输出正弦) */
            break;

        case WAVE_TRIANGLE:
            /* 三角波: MODE=1 (DAC输出三角波) */
            dev->ctrl_reg |= AD9833_MODE;
            break;

        case WAVE_SQUARE:
            /* 方波: OPBITEN=1, DIV2=1 (MSB输出, 分频后得到占空比50%方波) */
            dev->ctrl_reg |= AD9833_OPBITEN | AD9833_DIV2;
            break;
    }

    AD9833_WriteControl(dev->ctrl_reg);
    dev->wave_type = wave;
}

void AD9833_QuickConfig(AD9833_Dev_t *dev, float freq_hz, WaveType_t wave)
{
    /* 先复位，配置期间不输出不稳定的波形 */
    dev->ctrl_reg |= AD9833_RESET;
    AD9833_WriteControl(dev->ctrl_reg);

    /* 设置频率 */
    AD9833_SetFrequency(dev, freq_hz);

    /* 设置波形 */
    AD9833_SetWaveType(dev, wave);

    /* 释放复位 */
    dev->ctrl_reg &= ~AD9833_RESET;
    AD9833_WriteControl(dev->ctrl_reg);
}

void AD9833_OutputEnable(AD9833_Dev_t *dev, uint8_t enable)
{
    if (enable)
    {
        /* 唤醒: 清除 SLEEP1 和 SLEEP12，释放 RESET */
        dev->ctrl_reg &= ~(AD9833_SLEEP1 | AD9833_SLEEP12 | AD9833_RESET);
    }
    else
    {
        /* 休眠: 关闭内部时钟和DAC */
        dev->ctrl_reg |= (AD9833_SLEEP1 | AD9833_SLEEP12);
    }

    AD9833_WriteControl(dev->ctrl_reg);
}

void AD9833_FrequencySweep(AD9833_Dev_t *dev,
                           float start_hz,
                           float stop_hz,
                           float step_hz,
                           uint32_t delay_ms)
{
    float freq;
    int32_t direction = (stop_hz > start_hz) ? 1 : -1;
    float step = (float)direction * step_hz;

    for (freq = start_hz;
         direction > 0 ? (freq <= stop_hz) : (freq >= stop_hz);
         freq += step)
    {
        AD9833_SetFrequency(dev, freq);
        HAL_Delay(delay_ms);
    }
}
