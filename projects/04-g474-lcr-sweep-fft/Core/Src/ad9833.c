/**
 * ad9833.c — AD9833 DDS 单模块驱动实现 (STM32G474)
 *
 * 来源: projects/03-g474-dual-ad9833-fft (双模块版)
 * 改动:
 *   1. 去掉 module 参数, 只控制一颗 AD9833
 *   2. FSYNC 固定 PA4, 直接引用 CubeMX 生成的标签 AD9833_FSYNC
 *   3. 其他逻辑 (寄存器操作、频率计算、B28发送) 完全不变
 */

#include "ad9833.h"
#include "main.h"      /* CubeMX 生成: AD9833_FSYNC_Pin / AD9833_FSYNC_GPIO_Port */

/* SPI1 句柄 — 由 CubeMX 生成在 main.c */
extern SPI_HandleTypeDef hspi1;

/* ============================================================
 * 最底层: 向 AD9833 发一个 16-bit 命令
 *
 * 时序 (AD9833 手册 图 2 & 图 3):
 *   ① 拉低 FSYNC           → "我要对你说话"
 *   ② SPI 发 16 bit        → "这是命令"
 *   ③ 拉高 FSYNC           → "执行!"
 *
 * SPI1 已配置为 16-bit Transmit Only Master:
 *   HAL_SPI_Transmit(&hspi1, &data, 1, timeout)
 *   一次发送 1 个 16-bit 数据帧, MSB first。
 * ============================================================ */
void AD9833_WriteRegister(uint16_t data)
{
    /* ① 拉低 FSYNC — 开始一次传输 */
    HAL_GPIO_WritePin(AD9833_FSYNC_GPIO_Port, AD9833_FSYNC_Pin, GPIO_PIN_RESET);

    /* ② SPI1 发送 16 位 */
    HAL_SPI_Transmit(&hspi1, (uint8_t *)&data, 1, 100);

    /* ③ 拉高 FSYNC — AD9833 在上升沿锁存并执行命令 */
    HAL_GPIO_WritePin(AD9833_FSYNC_GPIO_Port, AD9833_FSYNC_Pin, GPIO_PIN_SET);
}

/* ============================================================
 * AD9833_Init()
 *
 * 初始化流程:
 *   1. 确保 FSYNC 空闲为高
 *   2. 写复位控制字 (RESET=1, 无输出)
 *   3. 写默认频率 1kHz → FREQ0
 *   4. 写正弦波控制字 + 释放复位 → 开始输出 1kHz 正弦
 * ============================================================ */
void AD9833_Init(void)
{
    /* Step 1: FSYNC 空闲高 (CubeMX 初始化已设, 这里再确认) */
    HAL_GPIO_WritePin(AD9833_FSYNC_GPIO_Port, AD9833_FSYNC_Pin, GPIO_PIN_SET);

    /* Step 2: 复位 + B28 模式 */
    AD9833_WriteRegister(CTRL_RESET_B28);

    /* Step 3: 默认频率 1kHz → FREQ0 寄存器 */
    AD9833_SetFreq(1000.0f);

    /* Step 4: 设正弦波 + 释放复位 → 开始输出 */
    AD9833_WriteRegister(CTRL_SINE);
    AD9833_WriteRegister(CTRL_SINE & ~BIT_RESET);
}

/* ============================================================
 * AD9833_SetFreq(freq_hz)
 *
 * 频率计算:
 *   freq_word (28-bit) = f_out × 2^28 / MCLK
 *   例: 1kHz → 1000 × 268435456 / 25000000 = 10737
 *
 * B28 模式下发两帧:
 *   第 1 发: CMD_FREQ0 | 频率低 14 位
 *   第 2 发: CMD_FREQ0 | 频率高 14 位
 *   → AD9833 内部自动拼成完整 28-bit 频率字
 * ============================================================ */
void AD9833_SetFreq(float freq_hz)
{
    uint32_t freq_word;
    uint16_t lsb_14;
    uint16_t msb_14;

    /* 计算 28-bit 频率字 */
    freq_word = (uint32_t)(freq_hz * AD9833_2POW28 / (float)AD9833_MCLK);

    /* 安全截断: 不超过 28-bit (0x0FFFFFFF) */
    freq_word &= 0x0FFFFFFF;

    /* 拆成低 14 位和高 14 位 */
    lsb_14 = (uint16_t)(freq_word & 0x3FFF);
    msb_14 = (uint16_t)((freq_word >> 14) & 0x3FFF);

    /* 连续发两帧 (B28 模式自动拼接) */
    AD9833_WriteRegister(CMD_FREQ0 | lsb_14);
    AD9833_WriteRegister(CMD_FREQ0 | msb_14);
}

/* ============================================================
 * AD9833_SetWave(wave_ctrl)
 *
 * 用法:
 *   AD9833_SetWave(CTRL_SINE);      → 输出正弦波
 *   AD9833_SetWave(CTRL_TRIANGLE);   → 输出三角波
 *   AD9833_SetWave(CTRL_SQUARE);     → 输出方波
 *
 * 注意: 波形宏里含 RESET=1。
 *       发完波形控制字后立即 `& ~BIT_RESET` 释放复位,
 *       保留波形位不变。详见项目经验总结 Bug #2:
 *       寄存器: 改一位用掩码, 不写全新的值
 * ============================================================ */
void AD9833_SetWave(uint16_t wave_ctrl)
{
    AD9833_WriteRegister(wave_ctrl);                   /* 含 RESET=1 */
    AD9833_WriteRegister(wave_ctrl & ~BIT_RESET);      /* 只清零 RESET 位 */
}
