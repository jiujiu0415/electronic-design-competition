/**
 * scope_fft.c — FFT 频谱分析 + 时域参数测量实现
 *
 * 流程:
 *   ① 时域: 扫 raw_buf → 找 max/min → Vpp, 求均值 → Vdc, 算 RMS → Vrms
 *   ② 去直流 + Hann 窗
 *   ③ arm_rfft_fast_f32 → 复数频谱
 *   ④ arm_cmplx_mag_f32 → 幅度谱
 *   ⑤ 归一化 (FFT size + 窗增益 + ADC 量化)
 *   ⑥ 找峰值 → 识别基频(最低峰) → 找谐波(整数倍位置)
 */

#include "scope_fft.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

/* ============================================================
 * 外部引用 — 串口打印
 * ============================================================ */
extern UART_HandleTypeDef huart2;

/* ============================================================
 * 静态变量 — FFT 工作区
 * ============================================================ */
static arm_rfft_fast_instance_f32 fft_inst;
static float32_t fft_in[SCOPE_FFT_SIZE];       /* 输入: 实数样本 */
static float32_t fft_out[SCOPE_FFT_SIZE * 2];  /* 输出: 复数对 (re,im) */
static float32_t fft_mag[SCOPE_FFT_SIZE];      /* 幅度谱 */

/* ADC 参考电压 */
#define ADC_VREF    3.30f
#define ADC_MAXVAL  4096.0f

/* 工具: 绝对值 */
#define ABS_F(x) ((x) < 0.0f ? -(x) : (x))

/* ============================================================
 * 工具函数: 找数组中的最大值
 * ============================================================ */
static float find_max(const float *buf, uint16_t len)
{
    float max_val = buf[0];
    for (uint16_t i = 1; i < len; i++)
        if (buf[i] > max_val) max_val = buf[i];
    return max_val;
}

/* ============================================================
 * 工具函数: 抛物线插值修正峰值
 *
 * 用 k-1, k, k+1 三点的幅度拟合抛物线，找到真正的峰位置。
 * 这样可以突破 bin 间距 (488Hz) 的限制。
 *
 * @param mag      幅度谱
 * @param k        峰值 bin
 * @param freq_out 输出: 修正后频率 (Hz)
 * @param amp_out  输出: 修正后幅度
 * @param fs_hz    采样率
 * ============================================================ */
static void parabola_interp(const float *mag, uint16_t k,
                             float *freq_out, float *amp_out, float fs_hz)
{
    if (k < 1 || k >= SCOPE_FFT_SIZE / 2 - 1)
    {
        *freq_out = k * fs_hz / SCOPE_FFT_SIZE;
        *amp_out  = mag[k];
        return;
    }

    float a = mag[k - 1];
    float b = mag[k];
    float c = mag[k + 1];

    /* 抛物线顶点偏移 d (以 bin 为单位) */
    float denom = 2.0f * b - a - c;
    float d = 0.0f;
    if (ABS_F(denom) > 1e-9f)
        d = 0.5f * (a - c) / denom;

    /* 限制偏移在 ±0.5 bin 以内 */
    if (d >  0.5f) d =  0.5f;
    if (d < -0.5f) d = -0.5f;

    *freq_out = (k + d) * fs_hz / SCOPE_FFT_SIZE;
    *amp_out  = b - 0.25f * (a - c) * d;
}

/* ============================================================
 * ScopeFFT_Init — 初始化 FFT
 * ============================================================ */
void ScopeFFT_Init(void)
{
    arm_rfft_fast_init_f32(&fft_inst, SCOPE_FFT_SIZE);
}

/* ============================================================
 * ScopeFFT_Analyze — 完整分析
 * ============================================================ */
ScopeResult ScopeFFT_Analyze(const uint16_t *raw_buf, uint16_t len, float fs_hz)
{
    ScopeResult result;
    memset(&result, 0, sizeof(result));

    if (len != SCOPE_FFT_SIZE) return result;

    result.freq_resolution = fs_hz / (float)SCOPE_FFT_SIZE;

    /* ────────────────────────────────────────
     * ① 时域参数: Vdc, Vpp, Vrms
     * ──────────────────────────────────────── */

    float sum = 0.0f;
    uint16_t min_val = 4095, max_val = 0;

    for (uint16_t i = 0; i < len; i++)
    {
        uint16_t v = raw_buf[i];
        sum += (float)v;
        if (v < min_val) min_val = v;
        if (v > max_val) max_val = v;
    }

    /* 直流偏置 */
    float dc_adc = sum / (float)len;
    result.vdc = dc_adc * ADC_VREF / ADC_MAXVAL;

    /* 峰峰值 */
    float vpp_adc = (float)(max_val - min_val);
    result.vpp = vpp_adc * ADC_VREF / ADC_MAXVAL;

    /* 真有效值: √(Σ(v[n] - Vdc)² / N) */
    float sum_sq = 0.0f;
    for (uint16_t i = 0; i < len; i++)
    {
        float diff = (float)raw_buf[i] - dc_adc;
        sum_sq += diff * diff;
    }
    float rms_adc = sqrtf(sum_sq / (float)len);
    result.vrms = rms_adc * ADC_VREF / ADC_MAXVAL;

    /* ────────────────────────────────────────
     * ② 去直流 + Hann 窗
     * ──────────────────────────────────────── */

    for (uint16_t i = 0; i < len; i++)
    {
        float val = (float)raw_buf[i] - dc_adc;
        /* Hann 窗: w[n] = 0.5 * (1 - cos(2π·n/(N-1))) */
        float w = 0.5f * (1.0f - arm_cos_f32(2.0f * PI * i / (float)(len - 1)));
        fft_in[i] = val * w;
    }

    /* ────────────────────────────────────────
     * ③ FFT → 复数频谱
     *
     * fft_out 格式 (arm_rfft_fast_f32 输出):
     *   {re[0], im[0], re[1], im[1], ..., re[N/2], im[N/2]}
     *   其中 im[0] = im[N/2] = 0 (实数 FFT)
     *   有效 bin 数 = N/2 + 1 = 2049
     * ──────────────────────────────────────── */

    arm_rfft_fast_f32(&fft_inst, fft_in, fft_out, 0);

    /* ────────────────────────────────────────
     * ④ 幅度谱
     * ──────────────────────────────────────── */

    arm_cmplx_mag_f32(fft_out, fft_mag, SCOPE_FFT_SIZE / 2 + 1);

    /* ────────────────────────────────────────
     * ⑤ 归一化 + 窗增益补偿
     *
     * arm_rfft_fast_f32 输出的 DC bin (k=0) 和 AC bin (k>0) 缩放不同:
     *   DC:  sum(信号) / 1 (无缩放)
     *   AC:  幅度 × N/2 × window_gain
     *
     * 归一化: AC bin 除以 N/2 → 恢复峰值幅度 (ADC 单位)
     * Hann 窗补偿: 除以 0.5 (Hann coherent gain)
     *
     * 所以对于 AC bin: mag /= (N/2) → mag /= 0.5 → mag *= 2/N, mag *= 2
     *               = mag * 4 / N  → 除以 N 再乘 4
     * ──────────────────────────────────────── */

    for (uint16_t i = 0; i <= SCOPE_FFT_SIZE / 2; i++)
    {
        if (i == 0)
        {
            /* DC: 除以 N */
            fft_mag[i] = fft_mag[i] / (float)SCOPE_FFT_SIZE;
        }
        else
        {
            /* AC: 除以 N/2，再补偿 Hann 窗 (×2) → 总 ×4/N */
            fft_mag[i] = fft_mag[i] * 4.0f / (float)SCOPE_FFT_SIZE;
        }
    }

    /* ────────────────────────────────────────
     * ⑥ 找峰值
     *
     * 策略: 扫描 bins，标记所有局部最大值（比左右邻居都高）
     * ──────────────────────────────────────── */

    float noise_floor = 1e-9f;   /* 噪声底 */
    uint16_t peak_bins[32];       /* 最多记录 32 个峰 */
    float peak_mags[32];
    uint8_t peak_count = 0;

    uint16_t nyquist_bin = SCOPE_FFT_SIZE / 2;

    for (uint16_t k = SCOPE_MIN_BIN; k < nyquist_bin; k++)
    {
        float m = fft_mag[k];
        /* 局部最大值: 比左邻和右邻都高 */
        if (m > fft_mag[k - 1] && m > fft_mag[k + 1] && m > noise_floor)
        {
            if (peak_count < 32)
            {
                peak_bins[peak_count] = k;
                peak_mags[peak_count] = m;
                peak_count++;
            }
        }
    }

    if (peak_count == 0)
    {
        /* 没找到峰 → 返回纯时域结果 */
        return result;
    }

    /* ────────────────────────────────────────
     * ⑦ 找基频: 有效峰值中最低频率的那个
     *
     * 基频是信号的最低频率分量，且在有效测量范围内。
     * 跳过 bin 0,1 (DC 附近)，从 bin 2 开始。
     * ──────────────────────────────────────── */

    /* 首先找最大谱线幅值作为参考，用于设定阈值 */
    float max_mag = find_max(peak_mags, peak_count);
    float threshold = max_mag * SCOPE_PEAK_THRESH;

    /* 最低频率的有效峰 = 基频 */
    int fund_idx = -1;
    float fund_freq = 1000000.0f;  /* 初始化为极大值 */
    for (uint8_t i = 0; i < peak_count; i++)
    {
        float freq_i = peak_bins[i] * fs_hz / SCOPE_FFT_SIZE;
        if (peak_mags[i] >= threshold
            && freq_i < fund_freq
            && freq_i >= 5000.0f)   /* 排除 5kHz 以下的噪声/DC 残余 */
        {
            fund_freq = freq_i;
            fund_idx = i;
        }
    }

    if (fund_idx < 0)
    {
        return result;
    }

    /* 抛物线插值修正基频 */
    float fund_freq_corrected, fund_amp_corrected;
    parabola_interp(fft_mag, peak_bins[fund_idx],
                     &fund_freq_corrected, &fund_amp_corrected, fs_hz);

    result.fundamental_freq = fund_freq_corrected;
    result.fundamental_amp  = fund_amp_corrected * ADC_VREF / ADC_MAXVAL;

    /* ────────────────────────────────────────
     * ⑧ 找谐波: 在基频整数倍附近找峰
     *
     * 对于每个谐波次数 k=2,3,4,..., 在 k*f₁ 附近 ±2 bin 范围内找最大峰
     * ──────────────────────────────────────── */

    result.harmonic_count = 0;
    for (uint8_t order = 2; order <= (SCOPE_MAX_HARM + 1); order++)
    {
        float target_freq = (float)order * result.fundamental_freq;

        /* 超出 Nyquist 或测量上限 → 停止 */
        if (target_freq > fs_hz * 0.45f) break;

        float target_bin_float = target_freq / fs_hz * SCOPE_FFT_SIZE;
        int target_bin = (int)(target_bin_float + 0.5f);

        /* 在 target_bin ±2 范围内搜索最大峰 */
        int search_start = target_bin - 2;
        int search_end   = target_bin + 2;
        if (search_start < (int)SCOPE_MIN_BIN) search_start = SCOPE_MIN_BIN;
        if (search_end >= (int)nyquist_bin) search_end = nyquist_bin - 1;

        int best_bin = -1;
        float best_mag = 0.0f;
        for (int k = search_start; k <= search_end; k++)
        {
            if (fft_mag[k] > best_mag)
            {
                best_mag = fft_mag[k];
                best_bin = k;
            }
        }

        if (best_bin >= 0 && best_mag >= threshold)
        {
            float h_freq, h_amp;
            parabola_interp(fft_mag, (uint16_t)best_bin,
                             &h_freq, &h_amp, fs_hz);

            uint8_t idx = result.harmonic_count;
            result.harmonics[idx].freq_hz   = h_freq;
            result.harmonics[idx].amplitude = h_amp * ADC_VREF / ADC_MAXVAL;
            result.harmonics[idx].bin       = (uint16_t)best_bin;
            result.harmonic_count++;

            if (result.harmonic_count >= SCOPE_MAX_HARM) break;
        }
    }

    return result;
}

/* ============================================================
 * ScopeFFT_Print — 串口打印结果
 * ============================================================ */
void ScopeFFT_Print(const ScopeResult *r)
{
    char buf[128];

    snprintf(buf, sizeof(buf),
             "\r\n=== Measurement ===\r\n");
    HAL_UART_Transmit(&huart2, (uint8_t *)buf, strlen(buf), 1000);

    snprintf(buf, sizeof(buf),
             "Vdc=%.3fV  Vpp=%.3fV  Vrms=%.3fV\r\n",
             r->vdc, r->vpp, r->vrms);
    HAL_UART_Transmit(&huart2, (uint8_t *)buf, strlen(buf), 1000);

    snprintf(buf, sizeof(buf),
             "Fundamental: %.0f Hz (%.3f Vpeak)\r\n",
             r->fundamental_freq, r->fundamental_amp);
    HAL_UART_Transmit(&huart2, (uint8_t *)buf, strlen(buf), 1000);

    snprintf(buf, sizeof(buf),
             "Freq resolution: %.1f Hz\r\n",
             r->freq_resolution);
    HAL_UART_Transmit(&huart2, (uint8_t *)buf, strlen(buf), 1000);

    for (uint8_t i = 0; i < r->harmonic_count && i < SCOPE_MAX_HARM; i++)
    {
        snprintf(buf, sizeof(buf),
                 "  Harmonic #%d: %.0f Hz (%.3f Vpeak)\r\n",
                 i + 2, /* 从 2 次谐波开始 */
                 r->harmonics[i].freq_hz,
                 r->harmonics[i].amplitude);
        HAL_UART_Transmit(&huart2, (uint8_t *)buf, strlen(buf), 1000);
    }

    HAL_UART_Transmit(&huart2, (uint8_t *)"=== End ===\r\n", 13, 1000);
}
