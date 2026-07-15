/**
 * @file    usart_cmd.c
 * @brief   串口命令行实现 — 通过串口助手发送命令控制信号源
 */

#include "usart_cmd.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* UART 句柄 (在 main.c 中定义) */
extern UART_HandleTypeDef huart1;

/* 接收缓冲区 */
static char rx_buffer[CMD_BUFFER_SIZE];
static uint8_t rx_index = 0;
static uint8_t rx_complete = 0;

/* ================================================================
 * 辅助宏
 * ================================================================ */

/* 字符串比较 */
#define STREQ(a, b)  (strcmp((a), (b)) == 0)
/* 字符串忽略大小写比较 */
#define STRCEQ(a, b) (strcasecmp((a), (b)) == 0)

/* ================================================================
 * 串口收发函数
 * ================================================================ */

static void UART_Print(const char *str)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)str, strlen(str), 100);
}

static void UART_Println(const char *str)
{
    UART_Print(str);
    UART_Print("\r\n");
}

/**
 * @brief  UART 接收完成回调 (每个字符)
 * @note   在 HAL_UART_RxCpltCallback 中调用
 */
void USART_CMD_RxCallback(uint8_t byte)
{
    if (byte == '\r' || byte == '\n')
    {
        if (rx_index > 0)
        {
            rx_buffer[rx_index] = '\0';
            rx_complete = 1;
            rx_index = 0;
        }
    }
    else if (rx_index < CMD_BUFFER_SIZE - 1)
    {
        /* 回显 */
        HAL_UART_Transmit(&huart1, &byte, 1, 10);
        rx_buffer[rx_index++] = byte;
    }
}

/**
 * @brief  检查是否有完整命令到达
 * @return 1=有命令, 0=无命令
 */
uint8_t USART_CMD_Available(void)
{
    return rx_complete;
}

/**
 * @brief  获取命令字符串并清除标志
 * @param  buf: 输出缓冲区
 */
void USART_CMD_GetCommand(char *buf)
{
    strcpy(buf, rx_buffer);
    rx_complete = 0;
}

/* ================================================================
 * 串口初始化
 * ================================================================ */

void USART_CMD_Init(void)
{
    /* 启动首个字节的中断接收 */
    HAL_UART_Receive_IT(&huart1, (uint8_t *)rx_buffer, 1);

    /* 打印欢迎信息 */
    UART_Println("\r\n====================================");
    UART_Println("  AD9833 Signal Generator v1.0");
    UART_Println("  STM32F103C8T6 + AD9833 DDS");
    UART_Println("====================================");
    UART_Println("Type 'help' for command list\r\n");
}

/* ================================================================
 * 命令解析
 * ================================================================ */

/**
 * @brief  分割命令字符串为 argc/argv
 */
static int parse_args(char *line, char *argv[], int max_args)
{
    int argc = 0;
    char *token = strtok(line, " \t");
    while (token != NULL && argc < max_args)
    {
        argv[argc++] = token;
        token = strtok(NULL, " \t");
    }
    return argc;
}

void USART_CMD_Process(char *cmd_line, AD9833_Dev_t *dev)
{
    char *argv[CMD_MAX_ARGS + 1];
    char cmd_copy[CMD_BUFFER_SIZE];
    int  argc;

    /* 跳过空命令 */
    if (cmd_line[0] == '\0') return;

    /* 复制命令字符串 (strtok会修改原字符串) */
    strcpy(cmd_copy, cmd_line);
    argc = parse_args(cmd_copy, argv, CMD_MAX_ARGS);

    if (argc == 0) return;

    /* ---- help — 帮助 ---- */
    if (STRCEQ(argv[0], "help") || STRCEQ(argv[0], "?"))
    {
        USART_CMD_PrintHelp();
    }
    /* ---- freq <频率Hz> — 设置频率 ---- */
    else if (STRCEQ(argv[0], "freq") && argc >= 2)
    {
        float f = atof(argv[1]);
        AD9833_SetFrequency(dev, f);
        UART_Print("OK: Frequency = ");
        char buf[32];
        sprintf(buf, "%.2f Hz\r\n", dev->frequency);
        UART_Print(buf);
    }
    /* ---- wave <sin|tri|sqr> — 切换波形 ---- */
    else if (STRCEQ(argv[0], "wave") && argc >= 2)
    {
        WaveType_t w;
        const char *name;

        if (STRCEQ(argv[1], "sin") || STRCEQ(argv[1], "sine"))
        {
            w = WAVE_SINE;
            name = "Sine";
        }
        else if (STRCEQ(argv[1], "tri") || STRCEQ(argv[1], "triangle"))
        {
            w = WAVE_TRIANGLE;
            name = "Triangle";
        }
        else if (STRCEQ(argv[1], "sqr") || STRCEQ(argv[1], "square"))
        {
            w = WAVE_SQUARE;
            name = "Square";
        }
        else
        {
            UART_Println("ERR: Unknown wave type. Use: sin|tri|sqr");
            return;
        }

        AD9833_SetWaveType(dev, w);
        UART_Print("OK: Wave = ");
        UART_Println(name);
    }
    /* ---- phase <角度> — 设置相位 ---- */
    else if (STRCEQ(argv[0], "phase") && argc >= 2)
    {
        float deg = atof(argv[1]);
        float rad = deg * (float)M_PI / 180.0f;
        AD9833_SetPhase(dev, rad);
        char buf[32];
        sprintf(buf, "OK: Phase = %.1f deg\r\n", deg);
        UART_Print(buf);
    }
    /* ---- sweep <start> <stop> <step> <delay_ms> — 扫频 ---- */
    else if (STRCEQ(argv[0], "sweep") && argc >= 3)
    {
        float start  = atof(argv[1]);
        float stop   = atof(argv[2]);
        float step   = (argc >= 4) ? atof(argv[3]) : 100.0f;
        uint32_t del = (argc >= 5) ? (uint32_t)atoi(argv[4]) : 50;

        char buf[64];
        sprintf(buf, "Sweeping %.0f -> %.0f Hz, step=%.0f Hz, delay=%lu ms\r\n",
                start, stop, step, del);
        UART_Print(buf);

        AD9833_FrequencySweep(dev, start, stop, step, del);

        UART_Println("Sweep done.");
    }
    /* ---- on / off — 开关输出 ---- */
    else if (STRCEQ(argv[0], "on"))
    {
        AD9833_OutputEnable(dev, 1);
        UART_Println("Output: ON");
    }
    else if (STRCEQ(argv[0], "off"))
    {
        AD9833_OutputEnable(dev, 0);
        UART_Println("Output: OFF");
    }
    /* ---- status — 打印状态 ---- */
    else if (STRCEQ(argv[0], "status"))
    {
        USART_CMD_PrintStatus(dev);
    }
    /* ---- 未知命令 ---- */
    else
    {
        UART_Print("ERR: Unknown command '");
        UART_Print(argv[0]);
        UART_Println("'. Type 'help'.");
    }
}

/* ================================================================
 * 状态打印与帮助
 * ================================================================ */

void USART_CMD_PrintStatus(AD9833_Dev_t *dev)
{
    const char *wave_name;
    char buf[64];

    switch (dev->wave_type)
    {
        case WAVE_SINE:     wave_name = "Sine";     break;
        case WAVE_TRIANGLE: wave_name = "Triangle";  break;
        case WAVE_SQUARE:   wave_name = "Square";    break;
        default:            wave_name = "Unknown";   break;
    }

    UART_Println("--- AD9833 Status ---");

    sprintf(buf, "  Frequency: %.2f Hz\r\n", dev->frequency);
    UART_Print(buf);

    sprintf(buf, "  Waveform:  %s\r\n", wave_name);
    UART_Print(buf);

    sprintf(buf, "  Phase:     %.1f deg\r\n",
            dev->phase * 180.0f / (float)M_PI);
    UART_Print(buf);

    sprintf(buf, "  MCLK:      %lu Hz\r\n", (unsigned long)AD9833_MCLK);
    UART_Print(buf);

    UART_Println("----------------------");
}

void USART_CMD_PrintHelp(void)
{
    UART_Println("=== Commands ===");
    UART_Println("  freq <Hz>               Set frequency");
    UART_Println("  wave <sin|tri|sqr>      Set waveform");
    UART_Println("  phase <deg>             Set phase offset (degrees)");
    UART_Println("  sweep <start> <stop> [step] [delay_ms]");
    UART_Println("                          Frequency sweep");
    UART_Println("  on / off                Enable / disable output");
    UART_Println("  status                  Print current settings");
    UART_Println("  help                    Show this help");
    UART_Println("");
    UART_Println("  Examples:");
    UART_Println("    freq 1000             -> 1 kHz");
    UART_Println("    freq 50k              -> 50 kHz");
    UART_Println("    wave sin              -> Sine wave");
    UART_Println("    wave sqr              -> Square wave");
    UART_Println("    phase 90              -> 90 degree shift");
    UART_Println("    sweep 100 10000 100 50 -> 100Hz-10kHz sweep");
}
