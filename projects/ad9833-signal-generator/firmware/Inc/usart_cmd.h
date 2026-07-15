/**
 * @file    usart_cmd.h
 * @brief   串口命令行接口 — 通过 UART 控制 AD9833 信号源
 * @note    命令格式: "命令 参数1 参数2 ..."
 *          波特率 115200, 8N1
 */

#ifndef __USART_CMD_H
#define __USART_CMD_H

#include "stm32f1xx_hal.h"
#include "ad9833.h"

/* 命令缓冲区大小 */
#define CMD_BUFFER_SIZE     64
#define CMD_MAX_ARGS        4

/* ================================================================
 * API 函数声明
 * ================================================================ */

/**
 * @brief  初始化串口命令行
 * @note   使能 UART1 中断接收
 */
void USART_CMD_Init(void);

/**
 * @brief  处理接收到的命令字符串
 * @param  cmd_line: 命令字符串
 * @param  dev: AD9833 设备指针
 * @note   在主循环中调用，或通过 UART 接收中断触发
 */
void USART_CMD_Process(char *cmd_line, AD9833_Dev_t *dev);

/**
 * @brief  打印设备当前状态
 * @param  dev: AD9833 设备指针
 */
void USART_CMD_PrintStatus(AD9833_Dev_t *dev);

/**
 * @brief  打印帮助信息
 */
void USART_CMD_PrintHelp(void);

#endif /* __USART_CMD_H */
