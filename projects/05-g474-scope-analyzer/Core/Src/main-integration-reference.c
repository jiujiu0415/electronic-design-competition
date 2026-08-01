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
#include <math.h>

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

/* 多采集平均 — 降低 Vd 噪声 + FFT 幅度波动, √N 改善 */
#define N_MEASURE_AVG  4  /* 设为1=单次测量, 4=4次平均 */

/**
 * do_measurement — 完整测量 (支持多次平均)
 *
 * 流程 (每次采集):
 *  ① 启动 ADC1 DMA (4096点 @2MSPS = 2.048ms)
 *  ② 等待 DMA 完成
 *  ③ 读 ADC3 → Vd (检波器直流, 16x过采样)
 *  ④ FFT 分析 (内部完成 G_total(Vd, f) + φ_LPF 校准)
 *
 * N次采集后取平均, 输出均值 + 标准差
 */
static void do_measurement(void)
{
    float sum_fund = 0.0f, sum_vpp = 0.0f;
    float sum_fund_sq = 0.0f;  /* 用于计算标准差 */
    float vd_sum = 0.0f;
    int   valid = 0;
    ScopeResult last_r;
    float last_vd_mV = 0.0f;

    for (int navg = 0; navg < N_MEASURE_AVG; navg++)
    {
        /* ── ① 启动采集 ── */
        ScopeADC_Restart();

        /* ── ② 等待 DMA 完成 (超时 ≈100ms) ── */
        uint32_t to = 100000;
        while (!ScopeADC_Ready() && --to) { __NOP(); }
        if (!to) {
            uart_send("[ERR] ADC timeout!\r\n");
            continue;
        }

        /* ── ③ 读检波器 → Vd (已 16x 过采样) ── */
        uint16_t vd_raw = ScopeADC_ReadEnvelope();
        float vd_mV = (float)vd_raw * 3300.0f / 4096.0f;

        /* ── ④ FFT 分析 ── */
        uint16_t *signal = ScopeADC_GetSignalBuffer();
        ScopeResult r = ScopeFFT_Analyze(signal, SCOPE_FFT_SIZE,
                                          ScopeADC_GetSampleRate(), vd_mV);

        /* 仅 HIGH 置信度纳入统计 */
        if (r.confidence == 0 && r.fund_vpeak_mV > 0.1f)
        {
            sum_fund    += r.fund_vpeak_mV;
            sum_fund_sq += r.fund_vpeak_mV * r.fund_vpeak_mV;
            sum_vpp     += r.vpp_mV;
            vd_sum      += vd_mV;
            valid++;
        }

        last_r    = r;
        last_vd_mV = vd_mV;
    }

    if (valid == 0) {
        /* 诊断: 打印最后一次结果帮助排查 */
        char diag[128];
        snprintf(diag, sizeof(diag),
                 "[ERR] invalid! conf=%d fund=%.1f Vd=%.1f f1=%.0f raw=%.0f\r\n",
                 last_r.confidence, last_r.fund_vpeak_mV,
                 last_vd_mV, last_r.f1_hz, last_r.f1_raw_hz);
        uart_send(diag);
        return;
    }

    /* ── ⑤ 串口输出 ── */
    float fund_mean = sum_fund / (float)valid;
    float fund_std  = 0.0f;
    if (valid > 1) {
        float var = (sum_fund_sq - sum_fund*sum_fund/(float)valid) / (float)(valid-1);
        if (var > 0.0f) fund_std = sqrtf(var);
    }
    float vd_mean   = vd_sum / (float)valid;
    float vpp_mean  = sum_vpp / (float)valid;

    snprintf(uart_buf, sizeof(uart_buf),
             "\r\n[AGC] Vd=%.1fmV (avg %d)  G_f1=%.2f\r\n",
             vd_mean, valid, last_r.agc_gain);
    uart_send(uart_buf);

    /* 打印最后一次的完整信息 (频率/谐波/相位) */
    ScopeFFT_Print(&last_r);

    /* 打印平均幅度 + 标准差 */
    snprintf(uart_buf, sizeof(uart_buf),
             "  Avg(%d): Fund=%.1f mVpk (std=%.1f)  Vpp=%.1f mV\r\n",
             valid, fund_mean, fund_std, vpp_mean);
    uart_send(uart_buf);

    /* ── 验证: 修正后基波相位应≈0 (信号源相位=0) ── */
    snprintf(uart_buf, sizeof(uart_buf),
             "  Phase check: |phi_fund|=%.3f rad (%.1f deg) [expect ~0]\r\n",
             fabsf(last_r.fund_phase_rad),
             fabsf(last_r.fund_phase_rad) * 180.0f / 3.14159265f);
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
