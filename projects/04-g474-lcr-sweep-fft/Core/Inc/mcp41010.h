/**
 * mcp41010.h — MCP41010 数字电位器驱动 (ST32G474)
 *
 * 用途: 调节 AD9833 模块的 PGA 输出幅度
 *       增益 = 0~255, 255 = 最大 (约 4.8Vpp)
 *
 * 硬件: MCP41010 与 AD9833 共用 SPI1, 独立 CS 片选
 *       SPI1: PA5=SCK, PA7=SDATA (16-bit, MSB first)
 *       MCP41010 CS: 需要在 CubeMX 里新增一个 GPIO_Output
 *
 * 注意: SPI1 配置为 16-bit Data Size, MCP41010 只认 8-bit,
 *       所以把增益值放在高 8 位 (D15-D8), MSB first 传输。
 */

#ifndef __MCP41010_H
#define __MCP41010_H

#include "stm32g4xx_hal.h"

/* 增益范围 */
#define MCP41010_GAIN_MIN   0
#define MCP41010_GAIN_MAX   255

/* 上电默认增益 (设为最大, 输出最强信号) */
#define MCP41010_DEFAULT    MCP41010_GAIN_MAX

void MCP41010_Init(void);
void MCP41010_SetGain(uint8_t gain);

#endif /* __MCP41010_H */
