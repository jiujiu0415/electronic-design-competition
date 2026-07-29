/**
 * main.c 集成参考 — 周期信号测量分析装置
 *
 * 用法: 把对应代码块复制到 CubeIDE 生成的 main.c 的对应 USER CODE 区域
 *
 * 需要的文件已放入工程:
 *   Core/Inc/scope_adc.h    Core/Src/scope_adc.c
 *   Core/Inc/scope_fft.h    Core/Src/scope_fft.c
 *
 * 串口输出格式 (每个测量周期一行):
 *   Vdc=1.650V  Vpp=1.024V  Vrms=0.362V
 *   Fundamental: 10500 Hz (0.512 Vpeak)
 *   Harmonic #3: 31500 Hz (0.128 Vpeak)
 *   Harmonic #4: 42000 Hz (0.064 Vpeak)
 */

/* ============================================================
 * ① USER CODE BEGIN Includes — 文件顶部
 * ============================================================
#include "scope_adc.h"
#include "scope_fft.h"
#include <stdio.h>
#include <string.h>
 * ============================================================ */


/* ============================================================
 * ② USER CODE BEGIN PV — 全局变量 (Private Variables)
 * ============================================================
char uart_buf[256];
 * ============================================================ */


/* ============================================================
 * ③ USER CODE BEGIN 0 — 自定义函数
 * ============================================================
 *
 * uart_print — 串口发字符串 (不依赖 printf)
 *
static void uart_print(const char *str)
{
    HAL_UART_Transmit(&huart2, (uint8_t *)str, strlen(str), 1000);
}
 * ============================================================ */


/* ============================================================
 * ④ USER CODE BEGIN 2 — main() 初始化区 (外设初始化之后)
 * ============================================================

  // ── 欢迎信息 ──
  uart_print("\r\n");
  uart_print("=== Period Signal Analyzer (G474) ===\r\n");
  uart_print("ADC: 2.0 MSPS, FFT: 4096 pt, Res: 488 Hz\r\n");

  // ── 初始化 FFT ──
  ScopeFFT_Init();
  uart_print("FFT initialized.\r\n");

  // ── 启动 ADC 采集 ──
  ScopeADC_Init();
  snprintf(uart_buf, sizeof(uart_buf),
           "ADC started @%.0f Hz, waiting for trigger...\r\n",
           ScopeADC_GetSampleRate());
  uart_print(uart_buf);

 * ============================================================ */


/* ============================================================
 * ⑤ USER CODE BEGIN WHILE — 主循环
 * ============================================================

  while (1)
  {
      // ── 等待 DMA 采满 4096 点 ──
      uart_print("\r\nWaiting...\r\n");
      while (!ScopeADC_Ready())
      {
          // 可以在这里做其他事, 或者单纯等待
      }

      // ── 取出数据 + 分析 ──
      uint16_t *buf = ScopeADC_GetBuffer();
      ScopeResult r = ScopeFFT_Analyze(buf, SCOPE_ADC_BUF_SIZE,
                                       ScopeADC_GetSampleRate());

      // ── 打印结果 ──
      ScopeFFT_Print(&r);

      // ── 重启 DMA 准备下一轮 ──
      ScopeADC_Restart();

      // ── 间隔一下 (可选) ──
      HAL_Delay(1000);
  }

 * ============================================================ */
