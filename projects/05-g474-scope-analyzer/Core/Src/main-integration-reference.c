/**
 * main.c 集成参考 — 周期信号测量分析装置 (要求1/2, 滤波器已移除)
 *
 * ============================================================
 * 电路连接:
 *   PA0 = ADC1_IN1   → 信号 (AGC+DC偏置后, 0~3.3V 单极性)
 *   PA1 = ADC3_IN1   → 检波器直流
 *   PA2 = USART2_TX  → 串口屏 RX (TJC USART HMI)
 *   PA3 = USART2_RX  → 串口屏 TX (触摸事件接收)
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
 * 显示: TJC 串口屏 (USART2, 115200 bps)
 *   编译开关: SCOPE_USE_DISPLAY (1=串口屏, 0=调试打印)
 *
 * 需要的源文件:
 *   scope_adc.c,  scope_fft.c,  scope_calib.c,  scope_display.c
 *   scope_adc.h,  scope_fft.h,  scope_calib.h,  scope_display.h
 */

/* ═══════════════════════════════════════════════════════════
 * ① USER CODE BEGIN Includes
 * ═══════════════════════════════════════════════════════════

#include "scope_adc.h"
#include "scope_fft.h"
#include "scope_calib.h"
#include "scope_display.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

 * ═══════════════════════════════════════════════════════════ */


/* ═══════════════════════════════════════════════════════════
 * ② USER CODE BEGIN PV
 * ═══════════════════════════════════════════════════════════

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
 * N次采集后, 选最后一次 HIGH 置信度的结果更新屏幕。
 */
static void do_measurement(void)
{
    float sum_fund = 0.0f, sum_vpp = 0.0f, sum_vrms = 0.0f;
    float sum_fund_sq = 0.0f;  /* 用于计算标准差 */
    float vd_sum = 0.0f;
    int   valid = 0;
    ScopeResult last_valid_r;
    uint8_t     has_valid = 0;

    /* 拷贝区 — 防止下一轮 DMA 覆盖 static buffer (问题2修复) */
    static uint16_t signal_copy[SCOPE_ADC_SIGNAL_BUF_SIZE];

    for (int navg = 0; navg < N_MEASURE_AVG; navg++)
    {
        /* ── ① 启动采集 ── */
        ScopeADC_Restart();

        /* ── ② 等待 DMA 完成 (超时 ≈100ms) ── */
        uint32_t to = 100000;
        while (!ScopeADC_Ready() && --to) { __NOP(); }
        if (!to) continue;

        /* ── ③ 读检波器 → Vd (已 16x 过采样) ── */
        uint16_t vd_raw = ScopeADC_ReadEnvelope();
        float vd_mV = (float)vd_raw * 3300.0f / 4096.0f;  /* Vref=3.30V 硬件实测 */

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
            sum_vrms    += r.vrms_mV;
            vd_sum      += vd_mV;
            valid++;

            /* 保存最后一次有效结果 + 拷贝信号缓冲 (防止下轮DMA覆盖) */
            memcpy(&last_valid_r, &r, sizeof(ScopeResult));
            memcpy(signal_copy, signal, SCOPE_ADC_SIGNAL_BUF_SIZE * sizeof(uint16_t));
            has_valid = 1;
        }
    }

    if (!has_valid) return;

    /* ── ⑤ 用平均值更新显示值 (减噪 √N) ── */
    {
        float fund_mean = sum_fund / (float)valid;
        float vpp_mean  = sum_vpp  / (float)valid;
        float vrms_mean = sum_vrms / (float)valid;
        last_valid_r.fund_vpeak_mV = fund_mean;
        last_valid_r.vpp_mV        = vpp_mean;
        last_valid_r.vrms_mV       = vrms_mean;
    }

    /* ── ⑥ 更新串口屏 ── */
#if SCOPE_USE_DISPLAY
    ScopeDisplay_Update(&last_valid_r, signal_copy);
#else
    /* 调试模式: 串口打印 */
    static char uart_buf[256];
    float fund_mean = sum_fund / (float)valid;
    float fund_std  = 0.0f;
    if (valid > 1) {
        float var = (sum_fund_sq - sum_fund*sum_fund/(float)valid) / (float)(valid-1);
        if (var > 0.0f) fund_std = sqrtf(var);
    }
    snprintf(uart_buf, sizeof(uart_buf),
             "\r\n[AGC] Vd=%.1fmV (avg %d)  G_f1=%.2f  Fund=%.1f±%.1fmV\r\n",
             vd_sum / (float)valid, valid, last_valid_r.agc_gain,
             fund_mean, fund_std);
    HAL_UART_Transmit(&huart2, (uint8_t *)uart_buf, strlen(uart_buf), 1000);
    ScopeFFT_Print(&last_valid_r);
#endif
}

 * ═══════════════════════════════════════════════════════════ */


/* ═══════════════════════════════════════════════════════════
 * ③ USER CODE BEGIN 2 — 初始化
 * ═══════════════════════════════════════════════════════════

  ScopeFFT_Init();
  ScopeADC_Init();

#if SCOPE_USE_DISPLAY
  ScopeDisplay_Init();
#else
  HAL_UART_Transmit(&huart2, (uint8_t *)"\r\n========================================\r\n", 44, 1000);
  HAL_UART_Transmit(&huart2, (uint8_t *)" Scope Analyzer — STM32G474\r\n", 31, 1000);
  HAL_UART_Transmit(&huart2, (uint8_t *)" ADC1 2MSPS + 4096 FFT\r\n", 26, 1000);
  HAL_UART_Transmit(&huart2, (uint8_t *)" Calib: G_total(Vd,f) + phi_LPF\r\n", 35, 1000);
  HAL_UART_Transmit(&huart2, (uint8_t *)"========================================\r\n", 44, 1000);
  HAL_UART_Transmit(&huart2, (uint8_t *)"Ready.\r\n", 7, 1000);
#endif

 * ═══════════════════════════════════════════════════════════ */


/* ═══════════════════════════════════════════════════════════
 * ④ USER CODE BEGIN WHILE — 主循环
 * ═══════════════════════════════════════════════════════════

  while (1)
  {
      /* ── 触摸事件处理 (屏幕按键) ── */
#if SCOPE_USE_DISPLAY
      ScopeDisplay_ProcessTouch();
#endif

      /* ── 要求1/2: 无干扰, 模拟开关断开 ── */
      ScopeADC_SwitchOpen();
      HAL_Delay(10);
      do_measurement();

      /* ── 等待 + 持续处理触摸 ── */
      for (uint16_t tick = 0; tick < 200; tick++)
      {
          HAL_Delay(10);  /* 10ms × 200 = 2s */
#if SCOPE_USE_DISPLAY
          ScopeDisplay_ProcessTouch();
#endif
      }

      /* ── 要求3: 有干扰, 模拟开关闭合 ── */
      // ScopeADC_SwitchClose();
      // HAL_Delay(10);
      // do_measurement();
      // for (uint16_t tick = 0; tick < 200; tick++) {
      //     HAL_Delay(10);
      //     ScopeDisplay_ProcessTouch();
      // }
  }

 * ═══════════════════════════════════════════════════════════ */


/* ═══════════════════════════════════════════════════════════
 * ⑤ USART2 中断回调 (TJC 触摸事件接收 + 错误恢复)
 *
 * 在 main.c 或 stm32g4xx_it.c 中添加以下两个回调函数:
 *
 * ── RX 完成回调 (正常接收) ──
 *
 *   void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
 *   {
 *       if (huart->Instance == USART2) {
 *           static uint8_t rx_byte;
 *           ScopeDisplay_IRQHandler(rx_byte);
 *           HAL_UART_Receive_IT(&huart2, &rx_byte, 1);
 *       }
 *   }
 *
 * ── 错误回调 (Overrun/Frame/Noise Error 恢复) ──
 *
 *   void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
 *   {
 *       if (huart->Instance == USART2) {
 *           /* 清错误标志 + 读 DR 清除 ORE */
 *           volatile uint32_t tmp __attribute__((unused));
 *           tmp = USART2->RDR;
 *           __HAL_UART_CLEAR_FLAGS(huart,
 *               UART_CLEAR_OREF | UART_CLEAR_FEF | UART_CLEAR_NEF);
 *           /* 重启 RX 中断 */
 *           static uint8_t rx_byte;
 *           HAL_UART_Receive_IT(&huart2, &rx_byte, 1);
 *       }
 *   }
 *
 * 未接 ErrorCallback → UART 发生 Overrun 后 RX 永久停止 → 触摸失效
 * ═══════════════════════════════════════════════════════════ */
