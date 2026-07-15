/**
 * @file    main.c
 * @brief   AD9833 信号发生器 — 主程序
 * @note    STM32F103C8T6 + AD9833 DDS
 *          SPI1 (PA5/PA7) + PA4(FSYNC) 控制 AD9833
 *          USART1 (PA9/PA10) 115200 8N1 串口命令控制
 *
 * 使用方法:
 *   1. 烧录程序，连接硬件
 *   2. 打开串口助手 (115200, 8N1)
 *   3. 输入命令，例如:
 *        freq 1000    -> 输出 1kHz 正弦波
 *        wave sqr     -> 切换为方波
 *        freq 50k     -> 50kHz
 *        sweep 100 10000 100 50 -> 100Hz~10kHz 扫频
 *        status       -> 查看当前状态
 */

#include "main.h"
#include "ad9833.h"
#include "usart_cmd.h"
#include <string.h>

/* ================================================================
 * 全局变量
 * ================================================================ */

SPI_HandleTypeDef   hspi1;
UART_HandleTypeDef  huart1;
AD9833_Dev_t        ad9833_dev;        /* AD9833 设备实例 */
char                cmd_line[CMD_BUFFER_SIZE]; /* 命令缓冲 */

/* ================================================================
 * 主函数
 * ================================================================ */

int main(void)
{
    /* HAL 库初始化 */
    HAL_Init();

    /* 系统时钟配置: 72MHz */
    SystemClock_Config();

    /* 外设初始化 */
    MX_GPIO_Init();
    MX_SPI1_Init();
    MX_USART1_UART_Init();

    /* 串口命令行初始化 (启动中断接收) */
    USART_CMD_Init();

    /* AD9833 初始化: 默认 1kHz 正弦波 */
    AD9833_Init(&ad9833_dev);

    /* 打印初始状态 */
    USART_CMD_PrintStatus(&ad9833_dev);

    /* ============================================================
     * 主循环
     *   - 轮询检查串口命令
     *   - 有命令到达时解析执行
     *   - LED 心跳闪烁
     * ============================================================ */
    uint32_t last_led_tick = 0;

    while (1)
    {
        /* 轮询检查是否有完整命令 */
        if (USART_CMD_Available())
        {
            USART_CMD_GetCommand(cmd_line);
            USART_CMD_Process(cmd_line, &ad9833_dev);
            UART_Print("\r\n> ");  /* 命令提示符 */
        }

        /* LED 心跳: 500ms 翻转一次 (表示系统正常运行) */
        uint32_t now = HAL_GetTick();
        if (now - last_led_tick >= 500)
        {
            HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
            last_led_tick = now;
        }
    }
}

/* ================================================================
 * 系统时钟配置
 * ================================================================ */

/**
 * @brief  配置系统时钟
 *         HSE (8MHz 外部晶振) -> PLL x9 -> SYSCLK 72MHz
 *         APB1 = 36MHz, APB2 = 72MHz
 */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef       osc_init = {0};
    RCC_ClkInitTypeDef       clk_init = {0};
    RCC_PeriphCLKInitTypeDef periph_clk = {0};

    /* HSE 振荡器: 8MHz */
    osc_init.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc_init.HSEState       = RCC_HSE_ON;
    osc_init.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    osc_init.PLL.PLLState   = RCC_PLL_ON;
    osc_init.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    osc_init.PLL.PLLMUL     = RCC_PLL_MUL9;     /* 8MHz × 9 = 72MHz */
    HAL_RCC_OscConfig(&osc_init);

    /* 系统时钟: HCLK=72M, PCLK1=36M, PCLK2=72M */
    clk_init.ClockType      = RCC_CLOCKTYPE_SYSCLK |
                              RCC_CLOCKTYPE_HCLK |
                              RCC_CLOCKTYPE_PCLK1 |
                              RCC_CLOCKTYPE_PCLK2;
    clk_init.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk_init.AHBCLKDivider  = RCC_SYSCLK_DIV1;   /* HCLK = 72MHz */
    clk_init.APB1CLKDivider = RCC_HCLK_DIV2;     /* PCLK1 = 36MHz */
    clk_init.APB2CLKDivider = RCC_HCLK_DIV1;     /* PCLK2 = 72MHz */
    HAL_RCC_ClockConfig(&clk_init, FLASH_LATENCY_2);
}

/* ================================================================
 * SPI1 初始化
 * ================================================================ */

/**
 * @brief  配置 SPI1 — 控制 AD9833
 *         PA5 = SCK, PA7 = MOSI
 *         Mode 2: CPOL=1, CPHA=0 (AD9833 要求在下降沿采样)
 *         8-bit 数据宽度, MSB First
 *         预分频: SPI_CLK = 72MHz / 256 ≈ 281kHz (AD9833 最高 40MHz)
 */
void MX_SPI1_Init(void)
{
    hspi1.Instance               = SPI1;
    hspi1.Init.Mode              = SPI_MODE_MASTER;
    hspi1.Init.Direction         = SPI_DIRECTION_2LINES;
    hspi1.Init.DataSize          = SPI_DATASIZE_16BIT;  /* AD9833 16-bit 传输 */
    hspi1.Init.CLKPolarity       = SPI_POLARITY_HIGH;    /* CPOL=1 */
    hspi1.Init.CLKPhase          = SPI_PHASE_2EDGE;      /* CPHA=0 (下降沿捕获) */
    hspi1.Init.NSS               = SPI_NSS_SOFT;         /* 软件控制 FSYNC */
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_256;
    hspi1.Init.FirstBit          = SPI_FIRSTBIT_MSB;
    hspi1.Init.TIMode            = SPI_TIMODE_DISABLE;
    hspi1.Init.CRCCalculation    = SPI_CRCCALCULATION_DISABLE;
    hspi1.Init.CRCPolynomial     = 10;

    HAL_SPI_Init(&hspi1);
}

/* ================================================================
 * USART1 初始化
 * ================================================================ */

/**
 * @brief  配置 USART1 — 串口命令行
 *         PA9 = TX, PA10 = RX
 *         115200 bps, 8N1
 */
void MX_USART1_UART_Init(void)
{
    huart1.Instance          = USART1;
    huart1.Init.BaudRate     = 115200;
    huart1.Init.WordLength   = UART_WORDLENGTH_8B;
    huart1.Init.StopBits     = UART_STOPBITS_1;
    huart1.Init.Parity       = UART_PARITY_NONE;
    huart1.Init.Mode         = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;

    HAL_UART_Init(&huart1);
}

/* ================================================================
 * GPIO 初始化
 * ================================================================ */

void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef gpio_init = {0};

    /* 使能 GPIO 时钟 */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /* PA4: FSYNC (推挽输出, 初始高电平) */
    gpio_init.Pin   = AD9833_FSYNC_PIN;
    gpio_init.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pull  = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(AD9833_FSYNC_PORT, &gpio_init);
    HAL_GPIO_WritePin(AD9833_FSYNC_PORT, AD9833_FSYNC_PIN, GPIO_PIN_SET);

    /* PC13: 板载 LED (推挽输出, 初始高电平 = 灭) */
    gpio_init.Pin   = LED_PIN;
    gpio_init.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pull  = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_PORT, &gpio_init);
    HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET);
}

/* ================================================================
 * UART 接收中断回调
 * ================================================================ */

/**
 * @brief  HAL UART 接收完成回调
 * @note   每收到一个字符调用 USART_CMD_RxCallback
 *         然后重新启动下一个字节的接收
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        static uint8_t rx_byte;
        USART_CMD_RxCallback(rx_byte);
        HAL_UART_Receive_IT(&huart1, &rx_byte, 1);  /* 继续接收下一个字节 */
    }
}

/* ================================================================
 * SPI MSP 初始化和反初始化回调 (HAL自动调用)
 * ================================================================ */

void HAL_SPI_MspInit(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance == SPI1)
    {
        __HAL_RCC_GPIOA_CLK_ENABLE();
        __HAL_RCC_SPI1_CLK_ENABLE();

        GPIO_InitTypeDef gpio = {0};

        /* PA5 = SCK */
        gpio.Pin       = GPIO_PIN_5;
        gpio.Mode      = GPIO_MODE_AF_PP;
        gpio.Speed     = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(GPIOA, &gpio);

        /* PA7 = MOSI */
        gpio.Pin       = GPIO_PIN_7;
        gpio.Mode      = GPIO_MODE_AF_PP;
        HAL_GPIO_Init(GPIOA, &gpio);
    }
}

void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        __HAL_RCC_GPIOA_CLK_ENABLE();
        __HAL_RCC_USART1_CLK_ENABLE();

        GPIO_InitTypeDef gpio = {0};

        /* PA9 = TX */
        gpio.Pin       = GPIO_PIN_9;
        gpio.Mode      = GPIO_MODE_AF_PP;
        gpio.Speed     = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(GPIOA, &gpio);

        /* PA10 = RX */
        gpio.Pin       = GPIO_PIN_10;
        gpio.Mode      = GPIO_MODE_INPUT;
        gpio.Pull      = GPIO_PULLUP;
        HAL_GPIO_Init(GPIOA, &gpio);
    }
}

/* ================================================================
 * 错误处理
 * ================================================================ */

void Error_Handler(void)
{
    /* 错误状态: LED 快速闪烁 */
    while (1)
    {
        HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
        HAL_Delay(100);
    }
}

/* ================================================================
 * SysTick 中断处理
 * ================================================================ */

void SysTick_Handler(void)
{
    HAL_IncTick();
}
