/**
 * main.c 集成参考 — 周期信号测量分析装置
 *
 * ADC1 (PA0): 信号 2MSPS, DMA 4096点
 * ADC2 (PA1): 检波器直流, 软件触发
 * GPIO PA4:   模拟开关
 *
 * 用法: 复制对应代码块到 CubeIDE 生成的 main.c 的 USER CODE 区域
 *
 * 需要的文件:
 *   scope_adc.h/c, scope_fft.h/c, scope_calib.h/c
 */

/* ═══════════════════════════════════════════════════════════
 * ① USER CODE BEGIN Includes
 * ═══════════════════════════════════════════════════════════

#include "scope_adc.h"
#include "scope_fft.h"
#include "scope_calib.h"
#include <stdio.h>
#include <string.h>

 * ═══════════════════════════════════════════════════════════ */


/* ═══════════════════════════════════════════════════════════
 * ② USER CODE BEGIN PV
 * ═══════════════════════════════════════════════════════════

static char uart_buf[256];

 * ═══════════════════════════════════════════════════════════ */


/* ═══════════════════════════════════════════════════════════
 * ③ USER CODE BEGIN 0 — 辅助函数
 * ═══════════════════════════════════════════════════════════

static void uart_print(const char *str)
{
    HAL_UART_Transmit(&huart2, (uint8_t *)str, strlen(str), 1000);
}

static void do_measurement(void)
{
    // ── 1. 启动采集 ──
    ScopeADC_Restart();

    // ── 2. 等待 DMA (4096点 @2MSPS = 2.05ms) ──
    uint32_t timeout = 100000;
    while (!ScopeADC_Ready() && --timeout);
    if (!timeout) {
        uart_print("[ERR] ADC timeout!\r\n");
        return;
    }

    // ── 3. 读检波器 ──
    uint16_t vd_raw = ScopeADC_ReadEnvelope();
    float vd_mV = (float)vd_raw * 3300.0f / 4096.0f;

    // ── 4. AGC 增益 ──
    float G = ScopeAGC_ComputeGain(vd_mV);
    snprintf(uart_buf, sizeof(uart_buf),
             "[AGC] Vd=%.1fmV  G=%.2f\r\n", vd_mV, G);
    uart_print(uart_buf);

    // ── 5. FFT 分析 ──
    uint16_t *signal = ScopeADC_GetSignalBuffer();
    ScopeResult r = ScopeFFT_AnalyzeSimple(signal, 4096,
                                            ScopeADC_GetSampleRate(), G);

    // ── 6. H_chain 频响修正 ──
    float H1 = ScopeCalib_GetHchain(r.fundamental_freq);
    // Vpp_original = Vpp_preAGC / H1
    // Vpeak_original[k] = Vpeak_preAGC[k] / ScopeCalib_GetHchain(k * f1)

    // ── 7. 输出 ──
    ScopeFFT_Print(&r);
}

 * ═══════════════════════════════════════════════════════════ */


/* ═══════════════════════════════════════════════════════════
 * ④ USER CODE BEGIN 2 — 初始化
 * ═══════════════════════════════════════════════════════════

  uart_print("\r\n=== Scope Analyzer — ADC1 2MSPS + ADC2 Envelope ===\r\n");
  ScopeFFT_Init();
  ScopeADC_Init();
  uart_print("Ready.\r\n");

 * ═══════════════════════════════════════════════════════════ */


/* ═══════════════════════════════════════════════════════════
 * ⑤ USER CODE BEGIN WHILE — 主循环
 * ═══════════════════════════════════════════════════════════

  while (1)
  {
      // 要求1/2 (无干扰): 开关断开
      ScopeADC_SwitchOpen();
      do_measurement();
      HAL_Delay(1000);

      // 要求3 (有干扰): 开关闭合
      // ScopeADC_SwitchClose();
      // HAL_Delay(1);
      // do_measurement();
      // HAL_Delay(1000);
  }

 * ═══════════════════════════════════════════════════════════ */
