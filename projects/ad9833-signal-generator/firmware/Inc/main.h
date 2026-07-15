#ifndef __MAIN_H
#define __MAIN_H

#include "stm32f1xx_hal.h"

/* ================================================================
 * 系统配置宏
 * ================================================================ */

/* 系统时钟: HSE 8MHz -> PLL x9 -> 72MHz */
#define SYSTEM_CLOCK_HZ     72000000U

/* 系统嘀嗒定时器周期 (1ms) */
#define TICK_PERIOD_MS      1

/* LED 引脚 (PC13, 板载LED) */
#define LED_PORT            GPIOC
#define LED_PIN             GPIO_PIN_13

/* ================================================================
 * 全局外设句柄 (在 main.c 中定义)
 * ================================================================ */

extern SPI_HandleTypeDef   hspi1;
extern UART_HandleTypeDef  huart1;

/* ================================================================
 * 函数声明
 * ================================================================ */

void SystemClock_Config(void);
void MX_GPIO_Init(void);
void MX_SPI1_Init(void);
void MX_USART1_UART_Init(void);
void Error_Handler(void);

#endif /* __MAIN_H */
