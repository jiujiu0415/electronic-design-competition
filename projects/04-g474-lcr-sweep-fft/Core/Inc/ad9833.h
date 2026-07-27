/**
 * ad9833.h — AD9833 DDS 单模块驱动 (STM32G474)
 *
 * 来源: projects/03-g474-dual-ad9833-fft (双模块版)
 * 改动:
 *   1. 去掉 module 参数 — 本工程只有一颗 AD9833
 *   2. FSYNC 固定 PA4, 宏在 main.h 里由 CubeMX 生成
 *
 * 硬件: AD9833 模块板载 25MHz 有源晶振
 *       SPI1 Transmit Only: PA5=SCLK, PA7=SDATA, PA4=FSYNC(GPIO)
 *       CubeMX GPIO Label: AD9833_FSYNC
 */

#ifndef __AD9833_H
#define __AD9833_H

#include "stm32g4xx_hal.h"

/* ============================================================
 * 硬件参数
 * ============================================================ */

/* AD9833 时钟源: 模块上板载 25MHz 有源晶振 */
#define AD9833_MCLK    25000000U

/* 2^28 = 268435456, 频率分辨率 = MCLK / 2^28 ≈ 0.093 Hz */
#define AD9833_2POW28  268435456.0f

/* ============================================================
 * 16-bit 命令的高两位 (D15-D14)
 *
 *   00 → 控制寄存器
 *   01 → FREQ0 寄存器
 *   10 → FREQ1 寄存器
 *   11 → PHASE  寄存器
 * ============================================================ */

#define CMD_CONTROL   0x0000
#define CMD_FREQ0     0x4000
#define CMD_FREQ1     0x8000
#define CMD_PHASE0    0xC000
#define CMD_PHASE1    0xE000

/* ============================================================
 * 控制寄存器各位 (D13-D0)
 * ============================================================ */

#define BIT_B28        (1 << 13)   /* 连续28bit频率写入模式 */
#define BIT_HLB        (1 << 12)   /* (B28=0时) 高/低14位选择 */
#define BIT_FSELECT    (1 << 11)   /* 0=用FREQ0, 1=用FREQ1 */
#define BIT_PSELECT    (1 << 10)   /* 0=用PHASE0, 1=用PHASE1 */
#define BIT_PIN_SW     (1 << 9)    /* 1=引脚控制, 0=软件控制 */
#define BIT_RESET      (1 << 8)    /* 1=复位(无输出), 0=正常输出 */
#define BIT_SLEEP1     (1 << 7)    /* 1=关MCLK */
#define BIT_SLEEP12    (1 << 6)    /* 1=关DAC */
#define BIT_OPBITEN    (1 << 5)    /* 1=VOUT输出DAC MSB(方波), 0=VOUT输出DAC */
#define BIT_SIGN_PIB   (1 << 4)    /* (OPBITEN=1时) 方波相关 */
#define BIT_DIV2       (1 << 3)    /* (OPBITEN=1时) 0=原频, 1=÷2 */
#define BIT_MODE       (1 << 1)    /* 0=正弦, 1=三角 (OPBITEN=0时) */

/* ============================================================
 * 预设控制字
 *
 *   所有预设都带着 RESET=1:
 *     配波形 → 配频率 → 释放复位(RESET=0) → 输出波形
 *   避免配置过程中产生毛刺。
 * ============================================================ */

/* 基础: 复位 + B28 连续写入模式 */
#define CTRL_RESET_B28     (BIT_RESET | BIT_B28)

/* 正弦波: 复位 + B28 + MODE=0 */
#define CTRL_SINE          (CTRL_RESET_B28 | 0x0000)

/* 三角波: 复位 + B28 + MODE=1 */
#define CTRL_TRIANGLE      (CTRL_RESET_B28 | BIT_MODE)

/* 方波: 复位 + B28 + OPBITEN=1 + DIV2=1 */
#define CTRL_SQUARE        (CTRL_RESET_B28 | BIT_OPBITEN | BIT_DIV2)

/* ============================================================
 * 函数声明
 * ============================================================ */

void AD9833_Init(void);
void AD9833_SetFreq(float freq_hz);
void AD9833_SetWave(uint16_t wave_ctrl);
void AD9833_WriteRegister(uint16_t data);

#endif /* __AD9833_H */
