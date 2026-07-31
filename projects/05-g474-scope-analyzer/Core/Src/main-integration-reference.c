/**
 * main.c 集成参考 — 周期信号测量分析装置 v3
 * ADC1+2 Dual Interleaved 4MSPS + ADC3 检波器 + GPIO 模拟开关
 *
 * 用法: 把对应代码块复制到 CubeIDE 生成的 main.c 的对应 USER CODE 区域
 *
 * 需要的文件:
 *   Core/Inc/scope_adc.h    Core/Src/scope_adc.c
 *   Core/Inc/scope_fft.h    Core/Src/scope_fft.c
 *   Core/Inc/scope_calib.h  Core/Src/scope_calib.c
 *
 * ADC 架构 (v3):
 *   ADC1+2 (PA0): 交替采集信号, TIM2 TRGO @2MHz → 4MSPS, DMA CDR 32-bit
 *   ADC3 (PA1):   独立采集检波器直流 → 软件触发单次
 *   GPIO PA4:     模拟开关 (LOW=断开u_J, HIGH=闭合u_J)
 *
 * FFT: 8192 @4MSPS, 分辨率 488Hz
 *
 * 串口输出格式:
 *   [AGC] Vd=520.0mV G=32.08
 *   f1=10500Hz V1_pk=512.0mV  f2=31500Hz V2_pk=128.0mV
 *   Vpp=824.0mV Vrms=291.0mV
 *   Confidence: HIGH
 */

/* ============================================================
 * ① USER CODE BEGIN Includes — 文件顶部
 * ============================================================
#include "scope_adc.h"
#include "scope_fft.h"
#include "scope_calib.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
 * ============================================================ */


/* ============================================================
 * ② USER CODE BEGIN PV — 全局变量 (Private Variables)
 * ============================================================
static char uart_buf[256];
 * ============================================================ */


/* ============================================================
 * ③ USER CODE BEGIN 0 — 自定义函数
 *
 * ── unpack_interleaved ───────────────────────────────────
 * 将 DMA 32-bit CDR 解包为时间顺序的 float 数组
 *
 * CDR 格式 (MDMA mode 2):
 *   bit[15:0]  = ADC1 数据 (t=0ns, 500ns, 1000ns, ...)
 *   bit[31:16] = ADC2 数据 (t=250ns, 750ns, 1250ns, ...)
 *
 * 输出: fft_in[0]=ADC1@0, fft_in[1]=ADC2@250ns, fft_in[2]=ADC1@500ns, ...
 *
static void unpack_interleaved(const uint32_t *dmabuf, float *out,
                               uint32_t word_count)
{
    for (uint32_t i = 0; i < word_count; i++)
    {
        uint32_t word = dmabuf[i];
        out[2 * i]     = (float)(word & 0x0000FFFF);        // ADC1
        out[2 * i + 1] = (float)((word >> 16) & 0xFFFF);   // ADC2
    }
}

 *
 * ── uart_print ────────────────────────────────────────────
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
  uart_print("=== Scope Analyzer v3 — Dual Interleaved 4MSPS ===\r\n");
  uart_print("ADC1+2(PA0): Interleaved 4MSPS\r\n");
  uart_print("ADC3(PA1):   Envelope Detector\r\n");
  uart_print("FFT: 8192 pt, Res: 488 Hz, Nyquist: 2 MHz\r\n");

  // ── 初始化 FFT ──
  ScopeFFT_Init();
  uart_print("FFT initialized.\r\n");

  // ── 初始化 ADC (含校准 + DMA + 模拟开关默认断开) ──
  ScopeADC_Init();
  snprintf(uart_buf, sizeof(uart_buf),
           "ADC started @%.0f Hz (interleaved)\r\n",
           ScopeADC_GetSampleRate());
  uart_print(uart_buf);

 * ============================================================ */


/* ============================================================
 * ⑤ USER CODE BEGIN WHILE — 主循环
 * ============================================================

  while (1)
  {
      /* ═══════════════════════════════════════
       * 要求 1 或 2: 无干扰, 模拟开关断开
       * ═══════════════════════════════════════ */
      ScopeADC_SwitchOpen();                // PA4 LOW → u_J 断开

      do_measurement(1);   // 或 2
      HAL_Delay(1000);

      /* ═══════════════════════════════════════
       * 要求 3: 有干扰, 模拟开关闭合
       * ═══════════════════════════════════════
      ScopeADC_SwitchClose();               // PA4 HIGH → u_J 接入
      HAL_Delay(1);                         // 开关建立时间
      do_measurement(3);
      HAL_Delay(1000);
       */
  }

 * ============================================================ */


/* ============================================================
 * ⑥ do_measurement — 完整测量流程 (放在 USER CODE BEGIN 0)
 *   或作为一个独立函数
 * ============================================================

static void do_measurement(uint8_t requirement)
{
    (void)requirement;  // 可用于日志标记

    // ── 1. 启动采集 ──
    ScopeADC_Restart();

    // ── 2. 等待 DMA 完成 (8192 点 @4MSPS = 2.05ms) ──
    uint32_t timeout = 100000;
    while (!ScopeADC_Ready() && --timeout);
    if (!timeout)
    {
        uart_print("[ERR] ADC DMA timeout!\r\n");
        return;
    }

    // ── 3. 读检波器直流 ──
    uint16_t vd_adc = ScopeADC_ReadEnvelope();
    float vd_mV = (float)vd_adc * 3300.0f / 4096.0f;

    // ── 4. AGC 增益 ──
    float G = ScopeAGC_ComputeGain(vd_mV);
    snprintf(uart_buf, sizeof(uart_buf),
             "[AGC] Vd=%.1fmV  G=%.2f\r\n", vd_mV, G);
    uart_print(uart_buf);

    // ── 5. 解包 CDR → FFT 输入 ──
    // ⚠️ CCM SRAM (__attribute__((section(".ccmram"))))
    //    32KB for float[8192], DMA 不访问, FFT 速度最快
    static float fft_input[SCOPE_ADC_FFT_SIZE]
        __attribute__((section(".ccmram")));

    const uint32_t *dmabuf = ScopeADC_GetInterleavedBuffer();
    unpack_interleaved(dmabuf, fft_input, SCOPE_ADC_DMA_BUF_SIZE);

    // ── 6. FFT 分析 ──
    //   fft_input → Hann窗 → FFT → 峰值检测 → 500Hz吸附
    ScopeResult r = ScopeFFT_AnalyzeInterleaved(
        fft_input, SCOPE_ADC_FFT_SIZE,
        SCOPE_ADC_SAMPLE_RATE, G);

    // ── 7. H_chain 频响修正 ──
    float f1 = r.fundamental_freq;
    float H1 = ScopeCalib_GetHchain(f1);
    // Vpeak_original[k] = Vpeak_preAGC[k] / ScopeCalib_GetHchain(k * f1)
    // Vpp_original = Vpp_preAGC / H1
    // Vrms_original = Vrms_preAGC / H1

    // ── 8. 打印结果 ──
    ScopeFFT_Print(&r);
}

 * ============================================================ */
