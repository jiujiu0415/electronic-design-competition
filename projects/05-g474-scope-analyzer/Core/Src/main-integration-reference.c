/**
 * main.c 集成参考 — 周期信号测量分析装置 (要求1/2, 滤波器已移除)
 *
 * ============================================================
 * 电路连接:
 *   PA0 = ADC1_IN1   → 信号 (AGC+DC偏置后, 0~3.3V 单极性)
 *   PA1 = ADC3_IN1   → 检波器直流
 *   PA2 = USART2_TX   → 串口打印
 *   PA4 = GPIO Output → 模拟开关 (LOW=断u_J, HIGH=合u_J)
 *
 * ============================================================
 * 信号链 (要求1/2, 滤波器已移除):
 *   u_b(+u_J) → 加法器 → AGC(×G→3Vpp) → DC偏置 → ADC1
 *                              │
 *                              └→ 检波器 → ADC3
 *
 * ============================================================
 * 校准 (2步, 滤波器移除后):
 *   ① G_total(Vd, f)  — 检波器直流+频率 → AGC总增益 (含频率修正)
 *   ② φ_LPF(f)        — LPF 相位修正
 *
 * ============================================================
 * 输出: 全部 mV 原始域 (u_b 输入端)
 *
 * 需要的源文件:
 *   scope_adc.c,  scope_fft.c,  scope_calib.c
 *   scope_adc.h,  scope_fft.h,  scope_calib.h
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

static void uart_send(const char *s)
{
    HAL_UART_Transmit(&huart2, (uint8_t *)s, strlen(s), 1000);
}

/**
 * do_measurement — 单次完整测量
 *
 * 流程:
 *  ① 启动 ADC1 DMA (4096点 @2MSPS = 2.048ms)
 *  ② 等待 DMA 完成
 *  ③ 读 ADC3 → Vd (检波器直流)
 *  ④ FFT 分析 (内部调用 ScopeAGC_ComputeGain(Vd, f) 完成校准)
 *  ⑤ 串口输出 (mV 原始域)
 */
static void do_measurement(void)
{
    /* ── ① 启动采集 ── */
    ScopeADC_Restart();

    /* ── ② 等待 DMA 完成 (超时 ≈100ms) ── */
    uint32_t to = 100000;
    while (!ScopeADC_Ready() && --to) { __NOP(); }
    if (!to) {
        uart_send("[ERR] ADC timeout!\r\n");
        return;
    }

    /* ── ③ 读检波器 → Vd ── */
    uint16_t vd_raw = ScopeADC_ReadEnvelope();
    float vd_mV = (float)vd_raw * 3300.0f / 4096.0f;

    /* ── ④ FFT 分析 (内部完成 G_total(Vd, f) + φ_LPF 校准) ── */
    uint16_t *signal = ScopeADC_GetSignalBuffer();
    ScopeResult r = ScopeFFT_Analyze(signal, SCOPE_FFT_SIZE,
                                      ScopeADC_GetSampleRate(), vd_mV);

    /* ── ⑤ 串口输出 ── */
    snprintf(uart_buf, sizeof(uart_buf),
             "\r\n[AGC] Vd=%.1fmV  G_f1=%.2f\r\n", vd_mV, r.agc_gain);
    uart_send(uart_buf);

    ScopeFFT_Print(&r);

    /* ── 验证: 修正后基波相位应≈0 (信号源相位=0) ── */
    snprintf(uart_buf, sizeof(uart_buf),
             "  Phase check: |phi_fund|=%.3f rad (%.1f deg) [expect ~0]\r\n",
             fabsf(r.fund_phase_rad),
             fabsf(r.fund_phase_rad) * 180.0f / 3.14159265f);
    uart_send(uart_buf);
}

 * ═══════════════════════════════════════════════════════════ */


/* ═══════════════════════════════════════════════════════════
 * ④ USER CODE BEGIN 2 — 初始化
 * ═══════════════════════════════════════════════════════════

  uart_send("\r\n========================================\r\n");
  uart_send(" Scope Analyzer — STM32G474\r\n");
  uart_send(" ADC1 2MSPS + 4096 FFT\r\n");
  uart_send(" Calib: G_total(Vd,f) + phi_LPF  (filter bypassed)\r\n");
  uart_send("========================================\r\n");

  ScopeFFT_Init();
  ScopeADC_Init();

  uart_send("Ready.\r\n");

 * ═══════════════════════════════════════════════════════════ */


/* ═══════════════════════════════════════════════════════════
 * ⑤ USER CODE BEGIN WHILE — 主循环
 * ═══════════════════════════════════════════════════════════

  while (1)
  {
      /* ── 要求1/2: 无干扰, 模拟开关断开 ── */
      ScopeADC_SwitchOpen();
      HAL_Delay(10);
      do_measurement();
      HAL_Delay(2000);

      /* ── 要求3: 有干扰, 模拟开关闭合 ── */
      // ScopeADC_SwitchClose();
      // HAL_Delay(10);
      // do_measurement();
      // HAL_Delay(2000);
  }

 * ═══════════════════════════════════════════════════════════ */
