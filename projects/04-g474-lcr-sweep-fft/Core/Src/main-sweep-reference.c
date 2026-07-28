/**
 * main.c 集成参考 — 第6步: DDS扫频 + ADC双通道采集 + FFT分析 + TFT Bode 图
 *
 * 用法: 把对应代码块复制到 CubeIDE 生成的 main.c 的对应 USER CODE 区域
 * 需要的文件已放入工程:
 *   Core/Inc/ad9833.h     Core/Src/ad9833.c
 *   Core/Inc/adc_dual.h    Core/Src/adc_dual.c
 *   Core/Inc/mcp41010.h   Core/Src/mcp41010.c
 *   Core/Inc/st7789.h      Core/Src/st7789.c
 *   Core/Inc/bode_plot.h   Core/Src/bode_plot.c
 *
 * 接线:
 *   AD9833 PGA输出 ─┬── 低通滤波器输入 (VOUTB1)
 *                    └── PA0 (ADC1_IN1, 输入参考 CH1)
 *   低通滤波器输出 (VOUTB2) ── PA1 (ADC1_IN2, 输出响应 CH2)
 *   AD9833 GND ── 滤波器 GND ── STM32 GND (共地!)
 *
 * 串口输出格式 (每个频点一行, 方便复制到 Excel 画图):
 *   Freq, Gain_dB, Phase_deg, V_in, V_out
 *
 * TFT 输出: 扫频完成后自动绘制 Bode 幅频曲线
 */

/* ============================================================
 * ① USER CODE BEGIN Includes — 文件顶部
 * ============================================================ */
#include "ad9833.h"
#include "adc_dual.h"
#include "mcp41010.h"
#include "st7789.h"
#include "bode_plot.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/* ============================================================
 * ② USER CODE BEGIN PV — 全局变量 (Private Variables)
 * ============================================================ */

/* 扫频参数 */
#define SWEEP_START_HZ   100.0f      /* 起始频率 100Hz */
#define SWEEP_STOP_HZ    100000.0f   /* 终止频率 100kHz */
#define SWEEP_POINTS     80          /* 扫描点数 (越多越精细) */
#define SWEEP_DWELL_MS   500         /* 每个频点停留 ms, 等信号+DMA稳定 */

char uart_buf[200];

/* 存储扫频结果 — 后续 TFT 画图用 */
float sweep_freq[SWEEP_POINTS];
float sweep_gain_db[SWEEP_POINTS];
float sweep_phase[SWEEP_POINTS];
int   sweep_idx = 0;

/* ============================================================
 * ③ USER CODE BEGIN 0 — 自定义函数 (放在文件尾部, main() 之前)
 * ============================================================ */

void uart_print(const char *str)
{
    HAL_UART_Transmit(&huart2, (uint8_t *)str, strlen(str), 1000);
}

/*
 * find_cutoff — 从扫频数据里找 -3dB 截止频率
 *
 * 归一化后通带 = 0dB, 找到第一个低于 -3dB 的频点
 * 扫描方向: 低→高, 找高频侧截止
 */
static float find_cutoff(float *freqs, float *gains, int n)
{
    /* 从 1kHz 往后扫描 (跳过低频交流耦合区) */
    int start = 0;
    for (int i = 0; i < n; i++) {
        if (freqs[i] >= 1000.0f) { start = i; break; }
    }

    for (int i = start; i < n; i++)
    {
        if (gains[i] <= -3.0f)
        {
            /* 线性插值: 在 i-1 和 i 之间找 -3dB 精确频率 */
            if (i > start)
            {
                float g0 = gains[i - 1];
                float g1 = gains[i];
                if (g0 > -3.0f && g1 < -3.0f)
                {
                    float t = (-3.0f - g0) / (g1 - g0);
                    return freqs[i - 1] + t * (freqs[i] - freqs[i - 1]);
                }
            }
            return freqs[i];
        }
    }
    return 0.0f;  /* 未找到 */
}

/* ============================================================
 * ④ USER CODE BEGIN 2 — main() 初始化区, 外设初始化之后
 * ============================================================ */

  uart_print("=== LCR Sweep Analyzer ===\r\n");

  /* ── 初始化 ST7789 TFT 屏幕 ── */
  ST7789_Init();
  uart_print("TFT LCD OK\r\n");

  /* 显示启动画面 */
  ST7789_DrawString(10, 100, "LCR Sweep", PLOT_TEXT, PLOT_BG);
  ST7789_DrawString(10, 116, "Analyzer", PLOT_TEXT, PLOT_BG);
  ST7789_DrawString(10, 140, "Starting...", PLOT_CYAN, PLOT_BG);
  HAL_Delay(500);

  /* ── 初始化 MCP41010 数字电位器 (增益 255=最大) ── */
  MCP41010_Init();
  uart_print("MCP41010 set to max gain\r\n");

  /* ── 初始化 AD9833 (默认 1kHz 正弦) ── */
  AD9833_Init();
  uart_print("AD9833 OK\r\n");

  /* ── 初始化 ADC 双通道采集 ── */
  ADC_Dual_Init();
  snprintf(uart_buf, sizeof(uart_buf),
           "ADC Dual started, Fs=%.0f Hz\r\n", ADC_Dual_GetSampleRate());
  uart_print(uart_buf);

  /* ── 扫频 + 测量 ── */
  uart_print("Freq, Gain_dB, Phase_deg, V_in, V_out\r\n");
  sweep_idx = 0;

  for (int i = 0; i < SWEEP_POINTS; i++)
  {
      /* 对数步进 */
      float t = (float)i / (float)(SWEEP_POINTS - 1);
      float freq = SWEEP_START_HZ * powf(SWEEP_STOP_HZ / SWEEP_START_HZ, t);

      /* ① 设 DDS 频率 */
      AD9833_SetFreq(freq);

      /* ② 等信号稳定 (DDS 切换 + 滤波器建立) */
      HAL_Delay(20);

      /* ③ 根据当前频率动态调整采样率 (保证 FFT 采到 ≥20 个周期) */
      ADC_Dual_SetSampleRate(freq);
      HAL_Delay(5);  /* 等新采样率的 DMA 启动 */

      /* ④ 等 ADC DMA 采集满 */
      uint32_t timeout = 0;
      while (!ADC_Dual_DataReady() && timeout < 1000000) timeout++;
      if (timeout >= 1000000)
      {
          snprintf(uart_buf, sizeof(uart_buf),
                   "TIMEOUT at %.0f Hz\r\n", freq);
          uart_print(uart_buf);
          ADC_Dual_Analyze(freq);  /* 重置 DMA */
          continue;
      }

      /* ⑤ FFT 分析双通道 → 计算增益+相位差 */
      ADC_DualResult r = ADC_Dual_Analyze(freq);

      /* ⑥ 输出结果 */
      snprintf(uart_buf, sizeof(uart_buf),
               "%.0f, %.2f, %.1f, %.3f, %.3f\r\n",
               r.freq_hz, r.gain_db, r.phase_diff_deg,
               r.ch1.amplitude, r.ch2.amplitude);
      uart_print(uart_buf);

      /* ⑦ 存储 (供后续 TFT 画图) */
      if (sweep_idx < SWEEP_POINTS)
      {
          sweep_freq[sweep_idx]    = r.freq_hz;
          sweep_gain_db[sweep_idx] = r.gain_db;
          sweep_phase[sweep_idx]   = r.phase_diff_deg;
          sweep_idx++;
      }

      /* ⑧ 等够剩余停留时间 (已花了 ~25ms 等稳定+DMA) */
      HAL_Delay(SWEEP_DWELL_MS - 20);
  }

  uart_print("--- Sweep Complete ---\r\n");

  /* ── 归一化增益 + 绘制 Bode 图 ── */
  {
      /* 在通带内 (1k~12kHz) 找最大增益做归一化偏移 */
      float offset = BodePlot_Normalize(sweep_freq, sweep_gain_db, sweep_idx, 0);

      /* 归一化后的增益数组 */
      float gains_norm[SWEEP_POINTS];
      for (int i = 0; i < sweep_idx; i++)
          gains_norm[i] = sweep_gain_db[i] + offset;

      /* 找 -3dB 截止频率 */
      float fc = find_cutoff(sweep_freq, gains_norm, sweep_idx);

      /* 串口输出关键参数 */
      snprintf(uart_buf, sizeof(uart_buf),
               "Passband offset: %.1f dB, fc=%.0f Hz\r\n", offset, fc);
      uart_print(uart_buf);

      /* 绘制 Bode 图到 TFT */
      BodePlot_Draw(sweep_freq, gains_norm, sweep_idx, fc);
      uart_print("Bode plot drawn on TFT\r\n");
  }

/* ============================================================
 * ⑤ USER CODE BEGIN 3 — while(1) 主循环
 * ============================================================ */

  /* 扫频完成, Bode 图已显示在屏幕上, 闲等 */
  HAL_Delay(5000);
