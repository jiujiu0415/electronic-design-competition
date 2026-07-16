/**
 * ad9833.c — AD9833 驱动实现
 * 基于 ad9833.h 中的宏定义, 对照手册第 6-9 页
 */

#include "ad9833.h"
#include <math.h>

/* SPI 和 FSYNC 由 CubeMX 自动生成在 main.c 中, 这里声明引用 */
extern SPI_HandleTypeDef hspi1;

/* ============================================================
 * 最底层的函数: 发一个 16-bit 命令给 AD9833
 *
 * 这是所有操作的基石。
 * 另外三个函数 (Init/SetFreq/SetWave) 都是通过调用它来完成的。
 *
 * 手册第 7 页 — 图 2 和 图 3 的时序:
 *   ① FSYNC 拉低     → "我要开始说话了"
 *   ② SPI 发 16 bit  → "这是命令内容"
 *   ③ FSYNC 拉高     → "我说完了, 你可以执行了"
 * ============================================================ */
void AD9833_WriteRegister(uint16_t data)
{
    /* ① 拉低 FSYNC, 通知 AD9833: "准备接收命令" */
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);

    /* ② 通过 SPI1 发送 16 位数据
     *    hspi1: CubeMX 生成的 SPI1 句柄
     *    (uint8_t *)&data: 16 位数据的地址
     *    1: 发 1 次 (但 SPI 配置为 16-bit 模式, 所以一次 = 16 位)
     *    100: 超时 100ms (正常不会超时)
     */
    HAL_SPI_Transmit(&hspi1, (uint8_t *)&data, 1, 100);

    /* ③ 拉高 FSYNC, AD9833 在这个上升沿锁存命令并执行 */
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
}

/* ============================================================
 * AD9833_Init()
 *
 * 初始化步骤 (参考手册第 9 页操作流程):
 *   1. 拉高 FSYNC 初始电平
 *   2. 写控制字 = CTRL_RESET_B28 (RESET=1, B28=1)
 *      → AD9833 进入复位, 输出为中间电平, 不会有杂波
 *   3. 写默认频率 1kHz 到 FREQ0
 *   4. 写 CTRL_SINE 并清除 RESET
 *      → 释放复位, 输出 1kHz 正弦波
 * ============================================================ */
void AD9833_Init(void)
{
    /* Step 1: FSYNC 初始为高 (空闲状态) */
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);

    /* Step 2: 复位 AD9833, 配置 B28 模式 */
    AD9833_WriteRegister(CTRL_RESET_B28);

    /* Step 3: 写默认频率 1000Hz 到 FREQ0 */
    AD9833_SetFreq(1000.0f);

    /* Step 4: 写正弦波控制字 (CTRL_SINE 里 RESET=1)
     *         然后立即写释放复位 (CTRL_SINE 去掉 RESET)
     *         → 波形开始输出 */
    AD9833_WriteRegister(CTRL_SINE);           /* 配波形, 仍在复位 */
    AD9833_WriteRegister(BIT_B28);             /* B28=1, 其余全0 → RESET=0, 开始输出 */
}

/* ============================================================
 * AD9833_SetFreq()
 *
 * 手册第 8-9 页 — 频率字计算:
 *
 *   频率分辨率 = 25MHz / 2^28 ≈ 0.093 Hz
 *   频率字 (28-bit) = f_out × 2^28 / 25MHz
 *
 * 发送方式 (B28 模式):
 *   连续发两次 16-bit 命令:
 *     第1次: FREQ0 命令 + 频率字低 14 位
 *     第2次: FREQ0 命令 + 频率字高 14 位
 *   AD9833 收到连续两次 FREQ0 写入后自动拼成 28-bit
 *
 * 为什么用 float? float 可以精确表示到 0.1Hz 级别,
 * 对于 0.1Hz~12.5MHz 的范围完全够用。
 * ============================================================ */
void AD9833_SetFreq(float freq_hz)
{
    uint32_t freq_word;   /* 28 位频率字 */
    uint16_t lsb_14;      /* 低 14 位 */
    uint16_t msb_14;      /* 高 14 位 */

    /* ------ 第 1 步: 计算 28-bit 频率字 ------ */

    /*
     * 公式: freq_word = f_out × 2^28 / MCLK
     *
     * 例: 输出 1kHz = 1000Hz
     *     freq_word = 1000 × 268435456 / 25000000
     *              = 10737 (约)
     *     AD9833 内部: 10737 × (25M / 2^28) = 999.9...Hz ≈ 1kHz ✓
     */
    freq_word = (uint32_t)(freq_hz * AD9833_2POW28 / (float)AD9833_MCLK);

    /* 安全: 28-bit 最大是 0x0FFFFFFF, 超出就截断 */
    freq_word &= 0x0FFFFFFF;

    /* ------ 第 2 步: 拆成低 14 位和高 14 位 ------ */

    /*
     * 28 位二进制: D27 .................. D14 D13..........D0
     * 拆成:
     *   lsb_14 = 低 14 位 (D13~D0)
     *   msb_14 = 高 14 位 (D27~D14)
     *
     * 分别拼接上 FREQ0 命令前缀组成两个 16-bit 命令:
     *
     *   第 1 发: CMD_FREQ0 | lsb_14  (bit15-14=01, bit13-0=频率低14位)
     *   第 2 发: CMD_FREQ0 | msb_14  (bit15-14=01, bit13-0=频率高14位)
     */
    lsb_14 = (uint16_t)(freq_word & 0x3FFF);            /* 取低 14 位 */
    msb_14 = (uint16_t)((freq_word >> 14) & 0x3FFF);    /* 取高 14 位 */

    /* ------ 第 3 步: 连续发送两帧 ------ */

    AD9833_WriteRegister(CMD_FREQ0 | lsb_14);   /* 第 1 发: 低 14 位 */
    AD9833_WriteRegister(CMD_FREQ0 | msb_14);   /* 第 2 发: 高 14 位 */

    /*
     * B28=1 模式下, AD9833 收到连续两次 FREQ0 写操作后
     * 自动把低14 + 高14 拼成完整 28-bit 频率字,
     * 无需手动通知 "我写完了"。
     */
}

/* ============================================================
 * AD9833_SetWave()
 *
 * 切换波形 = 重新写一次控制寄存器。
 *
 * 用法:
 *   AD9833_SetWave(CTRL_SINE);      → 正弦波
 *   AD9833_SetWave(CTRL_TRIANGLE);   → 三角波
 *   AD9833_SetWave(CTRL_SQUARE);     → 方波
 *
 * 注意: CTRL_SINE / CTRL_TRIANGLE / CTRL_SQUARE 这三个宏
 *       里都包含了 RESET=1 (见 ad9833.h 第四部分)。
 *
 *       所以发完它们之后必须再发一次 BIT_B28 来释放复位,
 *       不然 AD9833 一直停在复位状态, 没有输出。
 *
 *       这就是为什么 ad9833.h 里波形宏都带着 RESET:
 *       配波形 → 配频率 → 释放复位 → 输出波形
 *       避免配置过程中产生奇怪的中间波形。
 * ============================================================ */
void AD9833_SetWave(uint16_t wave_ctrl)
{
    AD9833_WriteRegister(wave_ctrl);     /* 写控制字 (含 RESET=1) */
    AD9833_WriteRegister(BIT_B28);       /* 释放复位, 波形开始输出 */
}
