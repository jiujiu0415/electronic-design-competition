/**
 * @file    ad9833.h
 * @brief   AD9833 DDS 波形发生器驱动 (STM32 HAL库)
 * @note    支持正弦波、三角波、方波输出，频率/相位可编程
 *          频率分辨率 = MCLK / 2^28 ≈ 0.1Hz (@25MHz MCLK)
 */

#ifndef __AD9833_H
#define __AD9833_H

#include "stm32f1xx_hal.h"
#include <math.h>

/* ================================================================
 * 硬件配置 (根据实际连接修改)
 * ================================================================ */

/* SPI 句柄 — 使用 SPI1 */
#define AD9833_SPI         hspi1
#define AD9833_SPI_HANDLE  (&hspi1)

/* FSYNC 引脚 — 使用 PA4 */
#define AD9833_FSYNC_PORT  GPIOA
#define AD9833_FSYNC_PIN   GPIO_PIN_4

/* AD9833 主时钟频率 (Hz) */
#define AD9833_MCLK        25000000UL   /* 25MHz 晶振 */

/* 2^28 — 用于频率字计算 */
#define AD9833_FREQ_SCALE  268435456.0f  /* 2^28 */

/* ================================================================
 * AD9833 寄存器地址 (高4位决定目标寄存器)
 * ================================================================ */

#define AD9833_REG_CTRL     0x0000  /* 控制寄存器 */
#define AD9833_REG_FREQ0    0x4000  /* 频率寄存器 0 */
#define AD9833_REG_FREQ1    0x8000  /* 频率寄存器 1 */
#define AD9833_REG_PHASE0   0xC000  /* 相位寄存器 0 */
#define AD9833_REG_PHASE1   0xE000  /* 相位寄存器 1 */

/* ================================================================
 * 控制寄存器位定义 (16-bit)
 * ================================================================ */

/* --- 写模式 --- */
#define AD9833_B28          (1 << 13)  /* 1=连续28bit写入频率 */
#define AD9833_HLB          (1 << 12)  /* 分开写时选择高/低14位 */

/* --- 频率/相位选择 --- */
#define AD9833_FSELECT      (1 << 11)  /* 1=使用FREQ1, 0=使用FREQ0 */
#define AD9833_PSELECT      (1 << 10)  /* 1=使用PHASE1, 0=使用PHASE0 */
#define AD9833_PIN_SW       (1 << 9)   /* 1=引脚控制, 0=软件控制 */

/* --- 复位与休眠 --- */
#define AD9833_RESET        (1 << 8)   /* 1=复位(输出恒定), 0=正常运行 */
#define AD9833_SLEEP1       (1 << 7)   /* 1=关闭内部MCLK */
#define AD9833_SLEEP12      (1 << 6)   /* 1=关闭DAC */

/* --- 波形选择 --- */
#define AD9833_OPBITEN      (1 << 5)   /* 1=输出DAC MSB/比较器 */
#define AD9833_SIGN_PIB     (1 << 4)   /* 比较器模式 (与OPBITEN配合) */
#define AD9833_DIV2         (1 << 3)   /* 1=方波分频 */
#define AD9833_MODE         (1 << 1)   /* 1=三角波, 0=正弦波 */

/* ================================================================
 * 波形类型枚举
 * ================================================================ */

typedef enum {
    WAVE_SINE      = 0,    /* 正弦波 */
    WAVE_TRIANGLE  = 1,    /* 三角波 */
    WAVE_SQUARE    = 2     /* 方波 */
} WaveType_t;

/* ================================================================
 * AD9833 设备结构体
 * ================================================================ */

typedef struct {
    float      frequency;      /* 当前输出频率 (Hz) */
    float      phase;          /* 当前相位偏移 (弧度) */
    WaveType_t wave_type;      /* 当前波形类型 */
    uint16_t   freq_reg_val;   /* 频率寄存器值 (28-bit) */
    uint16_t   phase_reg_val;  /* 相位寄存器值 (12-bit) */
    uint16_t   ctrl_reg;       /* 控制寄存器当前值 */
} AD9833_Dev_t;

/* ================================================================
 * API 函数声明
 * ================================================================ */

/**
 * @brief  初始化 AD9833
 * @param  dev: 设备结构体指针
 * @note   配置 SPI、FSYNC 引脚，复位设备
 */
void AD9833_Init(AD9833_Dev_t *dev);

/**
 * @brief  设置输出频率
 * @param  dev: 设备结构体指针
 * @param  freq_hz: 目标频率 (Hz)，范围 0 ~ MCLK/2
 * @note   自动计算 28-bit 频率字，使用 FREQ0 寄存器
 */
void AD9833_SetFrequency(AD9833_Dev_t *dev, float freq_hz);

/**
 * @brief  设置相位偏移
 * @param  dev: 设备结构体指针
 * @param  phase_rad: 相位偏移 (弧度)，范围 0 ~ 2π
 * @note   使用 PHASE0 寄存器
 */
void AD9833_SetPhase(AD9833_Dev_t *dev, float phase_rad);

/**
 * @brief  切换波形类型
 * @param  dev: 设备结构体指针
 * @param  wave: 波形类型 (WAVE_SINE / WAVE_TRIANGLE / WAVE_SQUARE)
 * @note   修改控制寄存器 MODE 和 OPBITEN 位
 */
void AD9833_SetWaveType(AD9833_Dev_t *dev, WaveType_t wave);

/**
 * @brief  快速配置：同时设置频率+波形
 * @param  dev: 设备结构体指针
 * @param  freq_hz: 目标频率 (Hz)
 * @param  wave: 波形类型
 * @note   一次调用完成频率和波形切换
 */
void AD9833_QuickConfig(AD9833_Dev_t *dev, float freq_hz, WaveType_t wave);

/**
 * @brief  启用/禁用输出
 * @param  dev: 设备结构体指针
 * @param  enable: 1=输出, 0=关闭
 */
void AD9833_OutputEnable(AD9833_Dev_t *dev, uint8_t enable);

/**
 * @brief  频率扫描 (用于扫频测试)
 * @param  dev: 设备结构体指针
 * @param  start_hz: 起始频率
 * @param  stop_hz: 终止频率
 * @param  step_hz: 步进频率
 * @param  delay_ms: 每步延时 (ms)
 */
void AD9833_FrequencySweep(AD9833_Dev_t *dev,
                           float start_hz,
                           float stop_hz,
                           float step_hz,
                           uint32_t delay_ms);

#endif /* __AD9833_H */
