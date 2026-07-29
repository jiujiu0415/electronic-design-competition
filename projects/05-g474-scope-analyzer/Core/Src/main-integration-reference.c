/**
 * main.c 集成参考 — 周期信号测量分析装置 (双 ADC 版本)
 *
 * 用法: 把对应代码块复制到 CubeIDE 生成的 main.c 的对应 USER CODE 区域
 *
 * 需要的文件:
 *   Core/Inc/scope_adc.h    Core/Src/scope_adc.c
 *   Core/Inc/scope_fft.h    Core/Src/scope_fft.c
 *
 * ADC 架构:
 *   ADC1 (PA0): 信号输入 (经 AGC+偏置, 0~3.2V)
 *   ADC2 (PA1): 检波器输出 (直流 = 总信号 Vpp)
 *   两个 ADC 由 TIM2 TRGO 同步触发 @2.0 MSPS
 *
 * 串口输出格式 (每个测量周期一行):
 *   Vdc=1.650V  Vpp_total=1024.0mV  Vrms_total=362.0mV
 *   Vpp_u_b(original)=824.0mV  Vrms_u_b(original)=291.0mV
 *   Vpp_envelope(ADC2)=310mV  AGC_Gain=10.32
 *   Fundamental: 10500 Hz (512.0 mVpeak)
 *   Harmonic #3: 31500 Hz (128.0 mVpeak, phi=0.52 rad)
 *   Harmonic #4: 42000 Hz (64.0 mVpeak, phi=-1.23 rad)
 *   Interference: 1032 mVpeak expected, 1 peaks detected
 *   Confidence: HIGH
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
  uart_print("=== Period Signal Analyzer (G474, Dual-ADC) ===\r\n");
  uart_print("ADC: 2.0 MSPS, FFT: 4096 pt, Res: 488 Hz\r\n");
  uart_print("ADC1(PA0)=Signal, ADC2(PA1)=Envelope Detector\r\n");

  // ── 初始化 FFT ──
  ScopeFFT_Init();
  uart_print("FFT initialized.\r\n");

  // ── 启动双 ADC 采集 ──
  ScopeADC_Init();
  snprintf(uart_buf, sizeof(uart_buf),
           "Dual ADC started @%.0f Hz\r\n",
           ScopeADC_GetSampleRate());
  uart_print(uart_buf);

 * ============================================================ */


/* ============================================================
 * ⑤ USER CODE BEGIN WHILE — 主循环
 * ============================================================

  while (1)
  {
      /* ═══════════════════════════════════════
       * 要求1/2 (无干扰): 用 ScopeFFT_AnalyzeSimple
       * ═══════════════════════════════════════ */
      uart_print("\r\nWaiting...\r\n");
      uint32_t timeout = HAL_GetTick();
      while (!ScopeADC_Ready())
      {
          if (HAL_GetTick() - timeout > 100)
          {
              uart_print("ERROR: ADC timeout!\r\n");
              break;
          }
      }
      if (!ScopeADC_Ready()) { ScopeADC_Restart(); HAL_Delay(500); continue; }

      uint16_t *sig = ScopeADC_GetSignalBuffer();
      ScopeResult r = ScopeFFT_AnalyzeSimple(sig, 4096, ScopeADC_GetSampleRate(), 1.0f);
      ScopeFFT_Print(&r);
      ScopeADC_Restart();
      HAL_Delay(1000);

      /* ═══════════════════════════════════════
       * 要求3 (有干扰): 改用 ScopeFFT_Analyze
       *
       * uint16_t *env = ScopeADC_GetEnvelopeBuffer();
       * ScopeResult r = ScopeFFT_Analyze(sig, env, 4096, ScopeADC_GetSampleRate());
       * ═══════════════════════════════════════ */
  }

 * ============================================================ */
