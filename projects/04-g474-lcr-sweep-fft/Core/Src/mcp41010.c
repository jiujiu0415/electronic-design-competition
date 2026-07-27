/**
 * mcp41010.c — MCP41010 数字电位器驱动实现
 */

#include "mcp41010.h"
#include "main.h"   /* CubeMX 生成: MCP41010_CS_Pin / MCP41010_CS_GPIO_Port */

extern SPI_HandleTypeDef hspi1;

/* ============================================================
 * MCP41010_Init — 上电初始化
 *
 * 时序: 确保 CS=高 (不选中), 等待上电稳定
 * ============================================================ */
void MCP41010_Init(void)
{
    /* CS 拉高 — 不选中 */
    HAL_GPIO_WritePin(MCP41010_CS_GPIO_Port, MCP41010_CS_Pin, GPIO_PIN_SET);
    HAL_Delay(1);

    /* 设默认增益 */
    MCP41010_SetGain(MCP41010_DEFAULT);
}

/* ============================================================
 * MCP41010_SetGain — 设置增益 (0~255, 255=最大)
 *
 * 协议:
 *   ① 拉低 CS 选中 MCP41010
 *   ② SPI 发 8-bit 命令 (放在 16-bit 帧的高字节, MSB first)
 *   ③ 拉高 CS → MCP41010 在上升沿锁存
 *
 * 注意: AD9833 FSYNC 必须保持高, 否则它也会响应 SPI 总线上的数据
 * ============================================================ */
void MCP41010_SetGain(uint8_t gain)
{
    /* 把 8-bit 增益值放在 16-bit 数据的高字节
     * SPI1 MSB first → D15 先发, 所以 D15-D8 = gain */
    uint16_t cmd = ((uint16_t)gain) << 8;

    /* ① CS 拉低 → 选中 MCP41010 */
    HAL_GPIO_WritePin(MCP41010_CS_GPIO_Port, MCP41010_CS_Pin, GPIO_PIN_RESET);

    /* ② 发送 (一个 16-bit 帧, 高 8 位 = gain, 低 8 位 = don't care) */
    HAL_SPI_Transmit(&hspi1, (uint8_t *)&cmd, 1, 100);

    /* ③ CS 拉高 → 锁存 */
    HAL_GPIO_WritePin(MCP41010_CS_GPIO_Port, MCP41010_CS_Pin, GPIO_PIN_SET);
}
