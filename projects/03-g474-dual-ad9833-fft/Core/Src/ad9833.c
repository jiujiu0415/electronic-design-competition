/**
 * ad9833.c — AD9833 双模块驱动实现 (STM32G474)
 *
 * 与 F103 单模块版本的区别:
 *   ① 每个函数多了 module 参数, 选择控制哪颗 AD9833
 *   ② AD9833_WriteRegister 内部根据 module 拉对应的 FSYNC
 *   ③ 其他逻辑完全不变
 */

#include "ad9833.h"
#include "main.h"      /* FSYNC1/FSYNC2 的 Pin/Port 宏定义 */
#include <math.h>

/* SPI1 句柄 — 由 CubeMX 生成在 main.c, 这里声明引用 */
extern SPI_HandleTypeDef hspi1;

/* ============================================================
 * 最底层: 向指定模块发一个 16-bit 命令
 *
 * 时序 (AD9833 手册第 7 页 图 2 & 图 3):
 *   ① 拉低对应模块的 FSYNC     → "我要对你说话"
 *   ② SPI 发 16 bit           → "这是命令"
 *   ③ 拉高 FSYNC              → "执行!"
 *
 * 另一个模块的 FSYNC 全程保持高电平 (不选中, 不响应)。
 * ============================================================ */
void AD9833_WriteRegister(uint8_t module, uint16_t data)
{
    /* ① 拉低目标模块的 FSYNC */
    if (module == AD9833_MODULE_1) {
        HAL_GPIO_WritePin(FSYNC1_GPIO_Port, FSYNC1_Pin, GPIO_PIN_RESET);
    } else {
        HAL_GPIO_WritePin(FSYNC2_GPIO_Port, FSYNC2_Pin, GPIO_PIN_RESET);
    }

    /* ② SPI1 发送 16 位
     *    hspi1 DataSize 已配为 16-bit, 一次 Transmit = 16 位 */
    HAL_SPI_Transmit(&hspi1, (uint8_t *)&data, 1, 100);

    /* ③ 拉高 FSYNC → AD9833 在上升沿锁存并执行命令 */
    if (module == AD9833_MODULE_1) {
        HAL_GPIO_WritePin(FSYNC1_GPIO_Port, FSYNC1_Pin, GPIO_PIN_SET);
    } else {
        HAL_GPIO_WritePin(FSYNC2_GPIO_Port, FSYNC2_Pin, GPIO_PIN_SET);
    }
}

/* ============================================================
 * AD9833_Init(module)
 *
 * 初始化流程:
 *   1. 确保 FSYNC 空闲为高
 *   2. 写复位控制字 (RESET=1)
 *   3. 写默认频率 1kHz → FREQ0
 *   4. 写正弦波控制字 + 释放复位 → 输出 1kHz 正弦
 * ============================================================ */
void AD9833_Init(uint8_t module)
{
    /* Step 1: FSYNC 空闲高 (CubeMX 初始化已设, 这里再确认一次) */
    if (module == AD9833_MODULE_1) {
        HAL_GPIO_WritePin(FSYNC1_GPIO_Port, FSYNC1_Pin, GPIO_PIN_SET);
    } else {
        HAL_GPIO_WritePin(FSYNC2_GPIO_Port, FSYNC2_Pin, GPIO_PIN_SET);
    }

    /* Step 2: 复位 + B28 模式 */
    AD9833_WriteRegister(module, CTRL_RESET_B28);

    /* Step 3: 默认频率 1kHz → FREQ0 */
    AD9833_SetFreq(module, 1000.0f);

    /* Step 4: 设正弦波 + 释放复位 → 开始输出 */
    AD9833_WriteRegister(module, CTRL_SINE);
    AD9833_WriteRegister(module, CTRL_SINE & ~BIT_RESET);
}

/* ============================================================
 * AD9833_SetFreq(module, freq_hz)
 *
 * 频率计算:
 *   freq_word (28-bit) = f_out × 2^28 / MCLK
 *   例: 1kHz → 1000 × 268435456 / 25000000 = 10737
 *
 * B28 模式下发送方式:
 *   第 1 发: CMD_FREQ0 | 频率低 14 位
 *   第 2 发: CMD_FREQ0 | 频率高 14 位
 *   → AD9833 自动拼成完整 28-bit
 * ============================================================ */
void AD9833_SetFreq(uint8_t module, float freq_hz)
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
    AD9833_WriteRegister(module, CMD_FREQ0 | lsb_14);
    AD9833_WriteRegister(module, CMD_FREQ0 | msb_14);
}

/* ============================================================
 * AD9833_SetWave(module, wave_ctrl)
 *
 * 用法:
 *   AD9833_SetWave(1, CTRL_SINE);      → 模块1 输出正弦波
 *   AD9833_SetWave(2, CTRL_TRIANGLE);   → 模块2 输出三角波
 *   AD9833_SetWave(1, CTRL_SQUARE);     → 模块1 输出方波
 *
 * 注意: 波形宏里含 RESET=1。
 *       发完波形控制字后立即 `& ~BIT_RESET` 释放复位,
 *       保留波形位不变。详见 Bug #2 教训:
 *       projects/ad9833-signal-gen/经验总结-犯错误记录.md
 * ============================================================ */
void AD9833_SetWave(uint8_t module, uint16_t wave_ctrl)
{
    AD9833_WriteRegister(module, wave_ctrl);                   /* 含 RESET=1 */
    AD9833_WriteRegister(module, wave_ctrl & ~BIT_RESET);      /* 只清零 RESET */
}
