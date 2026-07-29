/**
 * scope_fft.c — FFT 频谱分析 + 时域参数测量 + 干扰识别 + 交叉验证 实现
 *
 * 9 阶段流水线:
 *   ① ADC2 检波器 → Vpp_envelope, AGC 增益 G, 预期干扰幅值
 *   ② 时域: 扫 signal_buf → max/min → Vpp, 均值 → Vdc, RMS → Vrms
 *   ③ 去直流 + Hann 窗 → arm_rfft_fast_f32 → 复数频谱 → arm_cmplx_mag_f32
 *   ④ 找峰值 → 基频(最低峰) → 谐波(整数倍位置) → 提取相位
 *   ⑤ 干扰识别: 整数倍验证(层①) + 预期幅值对比(层②)
 *   ⑥ Parseval → Vrms_u_b (纯有用信号有效值)
 *   ⑦ 频谱分量 → 时域重建 → Vpp_u_b (纯有用信号峰峰值)
 *   ⑧ 交叉验证 (层③) → 置信度 (HIGH/MEDIUM/LOW)
 *   ⑨ 组装结果返回
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
static float32_t fft_in[SCOPE_FFT_SIZE];       /* 输入: 实数样本 (阶段⑦复用为重建波形) */
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
 * 工具函数 (新增): 从 FFT 复数输出提取相位
 *
 * fft_out 格式 (arm_rfft_fast_f32):
 *   fft_out[0] = re[0] (DC), fft_out[1] = re[N/2] (Nyquist)
 *   fft_out[2k] = re[k], fft_out[2k+1] = im[k]  for 1 <= k < N/2
 * ============================================================ */
static float extract_phase(uint16_t bin)
{
    if (bin == 0 || bin >= SCOPE_FFT_SIZE / 2)
        return 0.0f;

    float re = fft_out[bin * 2];
    float im = fft_out[bin * 2 + 1];
    return atan2f(im, re);
}

/* ============================================================
 * 工具函数 (新增): Parseval 定理 → Vrms
 *
 * Vrms = sqrt( Σ A_k² / 2 )
 * 只累加非干扰分量
 * ============================================================ */
static float compute_parseval_vrms(const ScopeHarmonic *components,
                                    uint8_t count)
{
    float sum_sq = 0.0f;
    for (uint8_t i = 0; i < count; i++)
    {
        if (!components[i].is_interference)
        {
            float A = components[i].amplitude;  /* Vpeak */
            sum_sq += A * A;
        }
    }
    return sqrtf(sum_sq / 2.0f);
}

/* ============================================================
 * 工具函数 (新增): 频谱分量 → 时域波形重建 → Vpp
 *
 * u_b[n] = Σ A_k * sin(2π × f_k × n/Fs + φ_k)
 *
 * 复用 fft_in[] 作为重建缓冲区 (FFT 已计算完, 原始数据不再需要)
 * 只使用非干扰分量
 * ============================================================ */
static float reconstruct_vpp(const ScopeHarmonic *components,
                              uint8_t count,
                              float fundamental_freq,
                              float fundamental_amp,
                              float fundamental_phase,
                              float fs_hz, uint16_t len)
{
    float *wave = fft_in;  /* 复用 FFT 输入缓冲 */

    /* 清零 */
    for (uint16_t n = 0; n < len; n++)
        wave[n] = 0.0f;

    /* 叠加基波 */
    float omega_fund = 2.0f * PI * fundamental_freq / fs_hz;
    for (uint16_t n = 0; n < len; n++)
    {
        wave[n] += fundamental_amp
                   * arm_sin_f32(omega_fund * (float)n + fundamental_phase);
    }

    /* 叠加各谐波 */
    for (uint8_t i = 0; i < count; i++)
    {
        if (components[i].is_interference) continue;

        float A   = components[i].amplitude;
        float f   = components[i].freq_hz;
        float phi = components[i].phase_rad;
        float omega = 2.0f * PI * f / fs_hz;

        for (uint16_t n = 0; n < len; n++)
        {
            wave[n] += A * arm_sin_f32(omega * (float)n + phi);
        }
    }

    /* 扫描 max/min */
    float max_val = wave[0], min_val = wave[0];
    for (uint16_t i = 1; i < len; i++)
    {
        if (wave[i] > max_val) max_val = wave[i];
        if (wave[i] < min_val) min_val = wave[i];
    }
    return max_val - min_val;  /* Vpp */
}

/* ============================================================
 * 工具函数 (新增): 相位反推
 *
 * 基频的相位没有直接存在 harmonics[] 里，需单独获取。
 * 这里通过峰值 bin 提取。
 * ============================================================ */
static float get_fundamental_phase(uint16_t fund_bin)
{
    return extract_phase(fund_bin);
}

/* ============================================================
 * ScopeFFT_Init — 初始化 FFT
 * ============================================================ */
void ScopeFFT_Init(void)
{
    arm_rfft_fast_init_f32(&fft_inst, SCOPE_FFT_SIZE);
}

/* ============================================================
 * ScopeFFT_Analyze — 完整 9 阶段分析
 * ============================================================ */
ScopeResult ScopeFFT_Analyze(const uint16_t *signal_buf,
                              const uint16_t *envelope_buf,
                              uint16_t len, float fs_hz)
{
    ScopeResult result;
    memset(&result, 0, sizeof(result));

    if (len != SCOPE_FFT_SIZE) return result;

    result.freq_resolution = fs_hz / (float)SCOPE_FFT_SIZE;

    /* ═══════════════════════════════════════════
     * ① ADC2 检波器 → Vpp_envelope → AGC 增益 → 预期干扰幅值
     * ═══════════════════════════════════════════ */

    double env_sum = 0.0;
    for (uint16_t i = 0; i < len; i++)
        env_sum += (double)envelope_buf[i];
    float env_adc_avg = (float)(env_sum / (double)len);
    result.vpp_envelope = env_adc_avg * ADC_VREF / ADC_MAXVAL;

    /* AGC 增益 G = V_out_target / V_in */
    float G = SCOPE_AGC_TARGET_VPP / result.vpp_envelope;
    if (G < SCOPE_AGC_GAIN_MIN) G = SCOPE_AGC_GAIN_MIN;
    if (G > SCOPE_AGC_GAIN_MAX) G = SCOPE_AGC_GAIN_MAX;
    result.agc_gain = G;

    /* 干扰 200mVpp → 100mVpeak, 经过 AGC 后: 0.1V × G */
    result.interference_expected_amp = SCOPE_INTERFERENCE_VPEAK * G;

    /* ═══════════════════════════════════════════
     * ② 时域参数: Vdc, Vpp, Vrms (总信号, 含干扰+偏置)
     * ═══════════════════════════════════════════ */

    float sum = 0.0f;
    uint16_t min_val = 4095, max_val = 0;

    for (uint16_t i = 0; i < len; i++)
    {
        uint16_t v = signal_buf[i];
        sum += (float)v;
        if (v < min_val) min_val = v;
        if (v > max_val) max_val = v;
    }

    /* 直流偏置 */
    float dc_adc = sum / (float)len;
    result.vdc = dc_adc * ADC_VREF / ADC_MAXVAL;

    /* 总峰峰值 (含干扰) */
    float vpp_adc = (float)(max_val - min_val);
    result.vpp = vpp_adc * ADC_VREF / ADC_MAXVAL;

    /* 总真有效值 (AC耦合) */
    float sum_sq = 0.0f;
    for (uint16_t i = 0; i < len; i++)
    {
        float diff = (float)signal_buf[i] - dc_adc;
        sum_sq += diff * diff;
    }
    float rms_adc = sqrtf(sum_sq / (float)len);
    result.vrms = rms_adc * ADC_VREF / ADC_MAXVAL;

    /* ═══════════════════════════════════════════
     * ③ 去直流 + Hann 窗 + FFT + 幅度谱 + 归一化
     * ═══════════════════════════════════════════ */

    for (uint16_t i = 0; i < len; i++)
    {
        float val = (float)signal_buf[i] - dc_adc;
        /* Hann 窗: w[n] = 0.5 * (1 - cos(2π·n/(N-1))) */
        float w = 0.5f * (1.0f - arm_cos_f32(2.0f * PI * i / (float)(len - 1)));
        fft_in[i] = val * w;
    }

    arm_rfft_fast_f32(&fft_inst, fft_in, fft_out, 0);
    arm_cmplx_mag_f32(fft_out, fft_mag, SCOPE_FFT_SIZE / 2 + 1);

    /* 归一化 + Hann 窗增益补偿 */
    for (uint16_t i = 0; i <= SCOPE_FFT_SIZE / 2; i++)
    {
        if (i == 0)
        {
            fft_mag[i] = fft_mag[i] / (float)SCOPE_FFT_SIZE;
        }
        else
        {
            fft_mag[i] = fft_mag[i] * 4.0f / (float)SCOPE_FFT_SIZE;
        }
    }

    /* ═══════════════════════════════════════════
     * ④ 峰值检测 + 基频识别 + 谐波搜索 + 相位提取
     * ═══════════════════════════════════════════ */

    float noise_floor = 1e-9f;
    uint16_t peak_bins[32];
    float peak_mags[32];
    uint8_t peak_count = 0;

    uint16_t nyquist_bin = SCOPE_FFT_SIZE / 2;

    for (uint16_t k = SCOPE_MIN_BIN; k < nyquist_bin; k++)
    {
        float m = fft_mag[k];
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
        result.confidence = SCOPE_CONFIDENCE_LOW;
        return result;
    }

    /* 找基频: 最低频率的有效峰 */
    float max_mag = find_max(peak_mags, peak_count);
    float threshold = max_mag * SCOPE_PEAK_THRESH;

    int fund_idx = -1;
    float fund_freq = 1000000.0f;
    for (uint8_t i = 0; i < peak_count; i++)
    {
        float freq_i = peak_bins[i] * fs_hz / SCOPE_FFT_SIZE;
        if (peak_mags[i] >= threshold
            && freq_i < fund_freq
            && freq_i >= 5000.0f)
        {
            fund_freq = freq_i;
            fund_idx = i;
        }
    }

    if (fund_idx < 0)
    {
        result.confidence = SCOPE_CONFIDENCE_LOW;
        return result;
    }

    /* 抛物线插值修正基频 */
    float fund_freq_corrected, fund_amp_corrected;
    parabola_interp(fft_mag, peak_bins[fund_idx],
                     &fund_freq_corrected, &fund_amp_corrected, fs_hz);

    result.fundamental_freq = fund_freq_corrected;
    result.fundamental_amp  = fund_amp_corrected * ADC_VREF / ADC_MAXVAL;

    /* 记录已占用的峰值索引 (用于干扰识别) */
    uint8_t peak_claimed[32] = {0};
    peak_claimed[fund_idx] = 1;

    /* 谐波搜索 + 相位提取 */
    result.harmonic_count = 0;
    for (uint8_t order = 2; order <= (SCOPE_MAX_HARM + 1); order++)
    {
        float target_freq = (float)order * result.fundamental_freq;

        if (target_freq > fs_hz * 0.45f) break;

        float target_bin_float = target_freq / fs_hz * SCOPE_FFT_SIZE;
        int target_bin = (int)(target_bin_float + 0.5f);

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
            result.harmonics[idx].phase_rad = extract_phase((uint16_t)best_bin);
            result.harmonics[idx].is_interference = 0;
            result.harmonic_count++;

            /* 标记该峰值已被谐波认领 */
            for (uint8_t p = 0; p < peak_count; p++)
            {
                if (peak_bins[p] == (uint16_t)best_bin)
                    peak_claimed[p] = 1;
            }

            if (result.harmonic_count >= SCOPE_MAX_HARM) break;
        }
    }

    /* ═══════════════════════════════════════════
     * ⑤ 干扰识别 (层① 整数倍验证 + 层② 预期幅值对比)
     * ═══════════════════════════════════════════ */

    result.interference_peaks = 0;
    uint8_t any_suspicious_harmonic = 0;

    /* 层①: 未被认领的峰值 → 检查是否满足某个整数倍 */
    for (uint8_t i = 0; i < peak_count; i++)
    {
        if (peak_claimed[i]) continue;

        float peak_freq = peak_bins[i] * fs_hz / SCOPE_FFT_SIZE;

        /* 跳过基频自身 (已被认领) */
        uint8_t matches_harmonic = 0;
        for (uint8_t order = 2; order <= (SCOPE_MAX_HARM + 1); order++)
        {
            float expected = (float)order * result.fundamental_freq;
            float error = ABS_F(peak_freq - expected) / expected;
            /* 更宽松的容忍度 (3 bin = ~1.5kHz) 以防噪声导致谐波漏检 */
            if (error < 0.05f)  /* 5% 容忍 */
            {
                matches_harmonic = 1;
                break;
            }
        }

        if (!matches_harmonic)
        {
            /* 这个峰是无法匹配的孤立峰 → 层②检查 */
            result.interference_peaks++;

            float peak_amp = peak_mags[i] * ADC_VREF / ADC_MAXVAL;
            /* 如果幅值接近预期干扰幅值 → 确认为干扰 */
            if (ABS_F(peak_amp - result.interference_expected_amp)
                < SCOPE_VERIFY_AMP_MATCH_TOL * result.interference_expected_amp)
            {
                /* 层②确认: 幅值匹配 */
            }
            /* 否则也只是未识别的谱线，不影响谐波列表 */
        }
    }

    /* 层②延伸: 已识别的谐波中，有没有幅值接近预期干扰幅值的？ */
    for (uint8_t i = 0; i < result.harmonic_count; i++)
    {
        float A_k = result.harmonics[i].amplitude;
        if (result.interference_expected_amp > 0.001f)
        {
            float rel_diff = ABS_F(A_k - result.interference_expected_amp)
                             / result.interference_expected_amp;
            if (rel_diff < SCOPE_VERIFY_AMP_SUS_THRESH)
            {
                /* 该谐波幅值与预期干扰幅值接近 → 可疑 */
                any_suspicious_harmonic = 1;
            }
        }
    }

    /* ═══════════════════════════════════════════
     * ⑥ Parseval → Vrms_u_b
     * ═══════════════════════════════════════════ */

    /* 构建包含基频在内的完整分量列表 */
    ScopeHarmonic all_components[SCOPE_MAX_HARM + 1];
    uint8_t comp_count = 0;

    /* 基频 */
    all_components[comp_count].freq_hz   = result.fundamental_freq;
    all_components[comp_count].amplitude = result.fundamental_amp;
    all_components[comp_count].phase_rad = get_fundamental_phase(peak_bins[fund_idx]);
    all_components[comp_count].is_interference = 0;
    comp_count++;

    /* 谐波 */
    for (uint8_t i = 0; i < result.harmonic_count && comp_count < (SCOPE_MAX_HARM + 1); i++)
    {
        all_components[comp_count] = result.harmonics[i];
        comp_count++;
    }

    result.vrms_u_b = compute_parseval_vrms(all_components, comp_count);

    /* ═══════════════════════════════════════════
     * ⑦ 时域波形重建 → Vpp_u_b
     * ═══════════════════════════════════════════ */

    float fund_phase = get_fundamental_phase(peak_bins[fund_idx]);
    result.vpp_u_b = reconstruct_vpp(result.harmonics, result.harmonic_count,
                                      result.fundamental_freq,
                                      result.fundamental_amp,
                                      fund_phase,
                                      fs_hz, len);

    /* ═══════════════════════════════════════════
     * ⑧ 交叉验证 (层③) → 置信度
     * ═══════════════════════════════════════════ */

    uint8_t pass1 = 0, pass2 = 0, pass3 = 0;

    /* 校验 1: Parseval 一致性
       Vrms_total² ≈ Vrms_u_b² + (A_J_expected)²/2 */
    {
        float expected_power = result.vrms_u_b * result.vrms_u_b
                             + (result.interference_expected_amp
                                * result.interference_expected_amp) / 2.0f;
        float actual_power = result.vrms * result.vrms;
        if (actual_power > 1e-9f)
        {
            float error = ABS_F(expected_power - actual_power) / actual_power;
            pass1 = (error < SCOPE_VERIFY_PARSEVAL_TOL) ? 1 : 0;
        }
        else
        {
            pass1 = 1;  /* 信号为零, 视为一致 */
        }
    }

    /* 校验 2: Vpp 交叉验证 (AGC 前域)
       Vpp_u_b/G + 0.2V ≈ Vpp_envelope */
    {
        float vpp_u_b_pre_agc = result.vpp_u_b / result.agc_gain;
        float expected_total_pre = vpp_u_b_pre_agc + SCOPE_INTERFERENCE_VPP;
        if (result.vpp_envelope > 0.001f)
        {
            float error = ABS_F(expected_total_pre - result.vpp_envelope)
                          / result.vpp_envelope;
            pass2 = (error < SCOPE_VERIFY_VPP_TOL) ? 1 : 0;
        }
        else
        {
            pass2 = 1;
        }
    }

    /* 校验 3: 谐波幅值无异常 */
    pass3 = any_suspicious_harmonic ? 0 : 1;

    /* 综合置信度 */
    if (result.interference_peaks == 0)
    {
        /* 没有检测到任何干扰峰 → 可能是要求1/2场景, 高置信度 */
        result.confidence = SCOPE_CONFIDENCE_HIGH;
    }
    else if (pass1 && pass2 && pass3)
    {
        result.confidence = SCOPE_CONFIDENCE_HIGH;
    }
    else if (pass1 || pass2)
    {
        result.confidence = SCOPE_CONFIDENCE_MEDIUM;
    }
    else
    {
        result.confidence = SCOPE_CONFIDENCE_LOW;
    }

    /* ═══════════════════════════════════════════
     * ⑨ 返回结果
     * ═══════════════════════════════════════════ */

    return result;
}

/* ============================================================
 * ScopeFFT_Print — 串口打印结果
 * ============================================================ */
void ScopeFFT_Print(const ScopeResult *r)
{
    char buf[256];

    HAL_UART_Transmit(&huart2, (uint8_t *)"\r\n=== Measurement ===\r\n", 22, 1000);

    /* 时域原始 */
    snprintf(buf, sizeof(buf),
             "Vdc=%.3fV  Vpp_total=%.3fV  Vrms_total=%.3fV\r\n",
             r->vdc, r->vpp, r->vrms);
    HAL_UART_Transmit(&huart2, (uint8_t *)buf, strlen(buf), 1000);

    /* 频谱反推 */
    snprintf(buf, sizeof(buf),
             "Vpp_u_b(recon)=%.3fV  Vrms_u_b(Parseval)=%.3fV\r\n",
             r->vpp_u_b, r->vrms_u_b);
    HAL_UART_Transmit(&huart2, (uint8_t *)buf, strlen(buf), 1000);

    /* ADC2 检波器 + AGC */
    snprintf(buf, sizeof(buf),
             "Vpp_envelope(ADC2)=%.3fV  AGC_Gain=%.2f\r\n",
             r->vpp_envelope, r->agc_gain);
    HAL_UART_Transmit(&huart2, (uint8_t *)buf, strlen(buf), 1000);

    /* 基频 */
    snprintf(buf, sizeof(buf),
             "Fundamental: %.0f Hz (%.3f Vpeak)  Res=%.1f Hz\r\n",
             r->fundamental_freq, r->fundamental_amp,
             r->freq_resolution);
    HAL_UART_Transmit(&huart2, (uint8_t *)buf, strlen(buf), 1000);

    /* 谐波 */
    for (uint8_t i = 0; i < r->harmonic_count && i < SCOPE_MAX_HARM; i++)
    {
        snprintf(buf, sizeof(buf),
                 "  Harmonic #%d: %.0f Hz (%.3f Vpeak, phi=%.2f rad)%s\r\n",
                 i + 2,
                 r->harmonics[i].freq_hz,
                 r->harmonics[i].amplitude,
                 r->harmonics[i].phase_rad,
                 r->harmonics[i].is_interference ? " [INTERFERENCE]" : "");
        HAL_UART_Transmit(&huart2, (uint8_t *)buf, strlen(buf), 1000);
    }

    /* 干扰信息 */
    snprintf(buf, sizeof(buf),
             "Interference: %.3f Vpeak expected, %d peaks detected\r\n",
             r->interference_expected_amp, r->interference_peaks);
    HAL_UART_Transmit(&huart2, (uint8_t *)buf, strlen(buf), 1000);

    /* 置信度 */
    const char *conf_str;
    switch (r->confidence)
    {
        case SCOPE_CONFIDENCE_HIGH:   conf_str = "HIGH";   break;
        case SCOPE_CONFIDENCE_MEDIUM: conf_str = "MEDIUM"; break;
        default:                      conf_str = "LOW";    break;
    }
    snprintf(buf, sizeof(buf), "Confidence: %s\r\n", conf_str);
    HAL_UART_Transmit(&huart2, (uint8_t *)buf, strlen(buf), 1000);

    HAL_UART_Transmit(&huart2, (uint8_t *)"=== End ===\r\n", 13, 1000);
}
