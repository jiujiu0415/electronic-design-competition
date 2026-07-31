/**
 * scope_fft.c — FFT 频谱分析 + 三校准集成 实现
 *
 * 完整计算链:
 *   ADC raw → 时域 Vdc/Vpp/Vrms
 *          → 去直流+Hann窗 → FFT → 幅度谱 → 归一化
 *          → 峰值检测 → 基频 → 500Hz吸附
 *          → 谐波搜索(f₁整数倍±2bin)
 *          → 抛物线插值(freq+amplitude)
 *          → 相位提取(atan2)
 *          → 校准链: ÷G → ÷H_chain(f) → −φ_LPF(f)
 *          → mV输出
 */

#include "scope_fft.h"
#include "scope_calib.h"
#include "arm_math.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

/* ============================================================
 * 外部引用
 * ============================================================ */
extern UART_HandleTypeDef huart2;

/* ============================================================
 * 静态工作区
 * ============================================================ */
static arm_rfft_fast_instance_f32 fft_inst;

/* FFT 缓冲 — 确保有足够空间 */
static float32_t fft_in  [SCOPE_FFT_SIZE];         /* 输入: 实数样本  */
static float32_t fft_out [SCOPE_FFT_SIZE];         /* 输出: 复数 (紧凑格式) */
static float32_t fft_mag [SCOPE_FFT_SIZE / 2 + 1]; /* 幅度谱 */

/* 工具宏 */
#define ABS_F(x)  ((x) < 0.0f ? -(x) : (x))

/* ============================================================
 * 工具函数: 抛物线插值 — 修正频率+幅度
 *
 * 用 k−1, k, k+1 三点拟合抛物线顶点。
 * 突破 488Hz bin 间距限制。
 * ============================================================ */
static void parabola_interp(const float *mag, uint16_t k,
                             float *freq_hz, float *amp,
                             float fs_hz, uint16_t fft_n)
{
    if (k < 1 || k >= fft_n / 2)
    {
        *freq_hz = (float)k * fs_hz / (float)fft_n;
        *amp     = mag[k];
        return;
    }

    float a = mag[k - 1];
    float b = mag[k];
    float c = mag[k + 1];

    /* 偏移量 d (bin 为单位) */
    float denom = 2.0f * b - a - c;
    float d = 0.0f;
    if (ABS_F(denom) > 1e-9f)
        d = 0.5f * (a - c) / denom;

    /* 钳位 */
    if (d >  0.5f) d =  0.5f;
    if (d < -0.5f) d = -0.5f;

    *freq_hz = ((float)k + d) * fs_hz / (float)fft_n;
    *amp     = b - 0.25f * (a - c) * d;
}

/* ============================================================
 * 工具函数: 提取 FFT 相位
 *
 * arm_rfft_fast_f32 输出格式 (紧凑):
 *   fft_out[0]    = Re[DC]
 *   fft_out[1]    = Re[Nyquist]
 *   fft_out[2k]   = Re[k], fft_out[2k+1] = Im[k]  (1≤k<N/2)
 * ============================================================ */
static float extract_phase(uint16_t bin, uint16_t fft_n)
{
    if (bin == 0 || bin >= fft_n / 2)
        return 0.0f;

    float re = fft_out[bin * 2];
    float im = fft_out[bin * 2 + 1];
    return atan2f(im, re);
}

/* ============================================================
 * 工具函数: Hann 窗
 * w[n] = 0.5 * (1 − cos(2π·n/(N−1)))
 * ============================================================ */
static inline float hann_window(uint16_t n, uint16_t N)
{
    return 0.5f * (1.0f - arm_cos_f32(2.0f * PI * (float)n / (float)(N - 1)));
}

/* ============================================================
 * 工具函数: ADC code → 电压 (V)
 * ============================================================ */
static inline float adc_code_to_V(float code)
{
    return code * SCOPE_ADC_VREF / SCOPE_ADC_MAX;
}

/* ============================================================
 * 三校准: 幅度 — ADC域 Vpeak → 原始输入域 mVpeak
 *
 *   V_adc(V)    = ADC code × 3.3/4096
 *   V_preAGC(V) = V_adc / G            (AGC 增益补偿)
 *   V_orig(V)   = V_preAGC / H_chain(f) (加法器+LPF幅频补偿)
 *   → ×1000 → mV
 * ============================================================ */
static float calibrate_vpeak_mV(float vpeak_adc_code, float freq_hz, float agc_gain)
{
    float H = ScopeCalib_GetHchain(freq_hz);
    if (H < 0.5f) H = 0.5f;

    float V_adc = adc_code_to_V(vpeak_adc_code);
    float V_preAGC = V_adc / agc_gain;
    float V_orig   = V_preAGC / H;
    return V_orig * 1000.0f;
}

/* ============================================================
 * 三校准: 相位 — FFT实测相位 → 原始输入域相位
 *
 *   φ_orig = φ_measured − φ_LPF(f)
 *
 * 信号源约束: 基波+谐波相位恒为0
 *   → 修正后 φ_orig 应为 0 (可验证电路模型精度)
 * ============================================================ */
static float calibrate_phase_rad(float phase_measured_rad, float freq_hz)
{
    float phi_lpf  = ScopeCalib_GetLPFPhase(freq_hz);
    float phi_orig = phase_measured_rad - phi_lpf;

    /* 规范化到 [−π, +π] (while: 极端情况下差值可能 >2π) */
    while (phi_orig >  PI) phi_orig -= 2.0f * PI;
    while (phi_orig < -PI) phi_orig += 2.0f * PI;

    return phi_orig;
}

/* ============================================================
 * ScopeFFT_Init — 初始化 FFT 实例
 * ============================================================ */
void ScopeFFT_Init(void)
{
    arm_rfft_fast_init_f32(&fft_inst, SCOPE_FFT_SIZE);
    memset(fft_in,  0, sizeof(fft_in));
    memset(fft_out, 0, sizeof(fft_out));
    memset(fft_mag, 0, sizeof(fft_mag));
}

/* ============================================================
 * ScopeFFT_Analyze — 完整分析 (单ADC, 三校准集成)
 * ============================================================ */
ScopeResult ScopeFFT_Analyze(const uint16_t *signal_buf,
                              uint16_t len,
                              float fs_hz,
                              float agc_gain)
{
    ScopeResult r;
    memset(&r, 0, sizeof(r));

    /* 参数校验 */
    if (len != SCOPE_FFT_SIZE || signal_buf == NULL)
        return r;

    r.bin_resolution = fs_hz / (float)SCOPE_FFT_SIZE;
    r.agc_gain       = (agc_gain > 1.0f) ? agc_gain : 1.0f;

    /* ═══════════════════════════════════════════════
     * 阶段 1: 时域参数 — Vdc, Vpp, Vrms (ADC域)
     * ═══════════════════════════════════════════════ */

    float    sum     = 0.0f;
    uint16_t min_raw = 4095;
    uint16_t max_raw = 0;

    for (uint16_t i = 0; i < len; i++)
    {
        uint16_t v = signal_buf[i];
        sum += (float)v;
        if (v < min_raw) min_raw = v;
        if (v > max_raw) max_raw = v;
    }

    float dc_adc_raw = sum / (float)len;
    r.vdc_mV = dc_adc_raw * (SCOPE_ADC_VREF / SCOPE_ADC_MAX) * 1000.0f;

    /* ── 去直流 + Hann 窗 (fft_in) ── */
    for (uint16_t i = 0; i < len; i++)
    {
        float ac = (float)signal_buf[i] - dc_adc_raw;
        fft_in[i] = ac * hann_window(i, len);
    }

    /* ── 时域 Vpp/Vrms (去直流后, ADC域) ── */
    float sum_sq = 0.0f;
    float min_ac = 1e9f, max_ac = -1e9f;
    for (uint16_t i = 0; i < len; i++)
    {
        float ac = (float)signal_buf[i] - dc_adc_raw;
        sum_sq += ac * ac;
        if (ac < min_ac) min_ac = ac;
        if (ac > max_ac) max_ac = ac;
    }
    float vpp_adc = max_ac - min_ac;
    float vrms_adc = sqrtf(sum_sq / (float)len);

    /* ═══════════════════════════════════════════════
     * 阶段 2: FFT → 幅度谱 → 归一化
     * ═══════════════════════════════════════════════ */

    arm_rfft_fast_f32(&fft_inst, fft_in, fft_out, 0);

    /*
     * arm_rfft_fast_f32 紧凑输出格式 (N 点 FFT → N 元素输出):
     *   fft_out[0]    = Re[DC]
     *   fft_out[1]    = Re[Nyquist]
     *   fft_out[2k]   = Re[k], fft_out[2k+1] = Im[k]  (1 ≤ k < N/2)
     *
     * 注意: arm_cmplx_mag_f32 默认从 offset 0 开始配对,
     *       会把 (DC, Nyquist) 看作第一对 — 这是错的。
     *       修复: 只对 bin 1..N/2-1 用 arm_cmplx_mag_f32,
     *             bin 0 和 N/2 手动赋值。
     */

    /* bin 1..N/2−1: 从 fft_out[2] 开始配对 */
    arm_cmplx_mag_f32(&fft_out[2], &fft_mag[1], SCOPE_FFT_SIZE / 2 - 1);

    /* bin 0 (DC) 和 bin N/2 (Nyquist) — 都是纯实数 */
    fft_mag[0]                  = fabsf(fft_out[0]);
    fft_mag[SCOPE_FFT_SIZE / 2] = fabsf(fft_out[1]);

    /* 归一化 + Hann 窗增益补偿:
     *   DC (k=0):  ÷N (平均)
     *   其他 (k>0): ×4/N (Hann 窗相干增益 = 0.5) */
    for (uint16_t i = 0; i <= SCOPE_FFT_SIZE / 2; i++)
    {
        if (i == 0)
            fft_mag[i] /= (float)SCOPE_FFT_SIZE;     /* mean */
        else
            fft_mag[i] *= 4.0f / (float)SCOPE_FFT_SIZE; /* A_peak */
    }

    /* ═══════════════════════════════════════════════
     * 阶段 3: 峰值检测
     *
     * 收集所有局部极大值 (最多256个), 然后按幅值降序排列,
     * 取前32个最强的。确保真实信号峰 (幅值数百) 不会被
     * 大量低频噪声峰 (幅值0.1~0.7) 挤出列表。
     * ═══════════════════════════════════════════════ */

    uint16_t peak_bins[256];
    float    peak_mags[256];
    uint16_t peak_count = 0;
    uint16_t nyquist    = SCOPE_FFT_SIZE / 2;

    for (uint16_t k = SCOPE_MIN_BIN; k < nyquist; k++)
    {
        float m = fft_mag[k];
        if (m > fft_mag[k - 1] && m > fft_mag[k + 1] && m > 1e-9f)
        {
            if (peak_count < 256)
            {
                peak_bins[peak_count] = k;
                peak_mags[peak_count] = m;
                peak_count++;
            }
        }
    }

    /* 按幅值降序排列 (简单冒泡, 256个最多~32K次比较) */
    for (uint16_t i = 0; i < peak_count; i++)
    {
        for (uint16_t j = i + 1; j < peak_count; j++)
        {
            if (peak_mags[j] > peak_mags[i])
            {
                uint16_t tmp_b = peak_bins[i];
                float    tmp_m = peak_mags[i];
                peak_bins[i] = peak_bins[j];
                peak_mags[i] = peak_mags[j];
                peak_bins[j] = tmp_b;
                peak_mags[j] = tmp_m;
            }
        }
    }

    /* 只保留前32个最强的峰 */
    if (peak_count > 32) peak_count = 32;

    if (peak_count == 0)
    {
        r.confidence = 2;  /* LOW */
        return r;
    }

    /* ═══════════════════════════════════════════════
     * 阶段 4: 基频检测 → 500Hz 吸附
     *
     * 策略: 谐波序列匹配
     *   对每个候选峰, 统计其整数倍处是否有其他峰对齐(±2bin)。
     *   匹配数最多的峰为基频 — 因为真正基波总有最多整数倍谐波。
     *
     *   即使某次谐波(H2/H3/H4)幅值超过基波, 基波仍有最多匹配:
     *     f₁ 有 H2+H3+H4+H5... 多个匹配
     *     H3 只有 H6+H9 少数匹配
     *
     *   低频杂散峰: 0匹配 → 自动排除
     *   仅基波无谐波: 所有峰0匹配 → 退化为最强峰
     * ═══════════════════════════════════════════════ */

    /* Step 1: 谐波序列匹配 — 每个峰作为候选基频, 统计其谐波匹配数 */
    int   best_fund_idx = -1;
    int   best_match    = -1;  /* -1 = 尚未找到有效候选 */
    float best_mag      = 0.0f;

    for (uint16_t i = 0; i < peak_count; i++)
    {
        float fc = (float)peak_bins[i] * fs_hz / (float)SCOPE_FFT_SIZE;

        if (fc < 5000.0f || fc > 500000.0f) continue;

        /* 统计 order 2~8 有多少个整数倍位置有其他峰对齐 */
        int matches = 0;
        for (uint8_t k = 2; k <= 8; k++)
        {
            float expected = fc * (float)k;
            if (expected > fs_hz * 0.45f) break;  /* 超过90% Nyquist → 不搜 */

            int expected_bin = (int)(expected / fs_hz
                                     * (float)SCOPE_FFT_SIZE + 0.5f);
            for (uint16_t j = 0; j < peak_count; j++)
            {
                if (j == i) continue;
                int db = (int)peak_bins[j] - expected_bin;
                if (db >= -2 && db <= 2) { matches++; break; }
            }
        }

        /* 匹配数多者胜; 平局取幅值大者 */
        if (matches > best_match ||
            (matches == best_match && peak_mags[i] > best_mag))
        {
            best_match    = matches;
            best_fund_idx = (int)i;
            best_mag      = peak_mags[i];
        }
    }

    /* Step 2: 未找到有效候选 → 退化为最强峰 (仅基波/噪声情形) */
    if (best_fund_idx < 0)
    {
        best_mag = 0.0f;
        for (uint16_t i = 0; i < peak_count; i++)
        {
            float f = (float)peak_bins[i] * fs_hz / (float)SCOPE_FFT_SIZE;
            if (f >= 5000.0f && f <= 500000.0f && peak_mags[i] > best_mag)
            {
                best_mag      = peak_mags[i];
                best_fund_idx = (int)i;
            }
        }
    }

    if (best_fund_idx < 0)
    {
        r.confidence = 2;
        return r;
    }

    /* 抛物线插值修正基频 (吸附前) */
    float fund_freq_precise, fund_amp_raw;
    parabola_interp(fft_mag, peak_bins[(uint16_t)best_fund_idx],
                     &fund_freq_precise, &fund_amp_raw,
                     fs_hz, SCOPE_FFT_SIZE);

    r.f1_raw_hz = fund_freq_precise;

    /* 500Hz 吸附:
     *   f₁ = round(f₁_raw / 500) × 500
     *   约束: N≥20 (10kHz), N≤1000 (500kHz) */
    float N_float = fund_freq_precise / SCOPE_FREQ_STEP;
    uint16_t N = (uint16_t)(N_float + 0.5f);
    if (N < 20)  N = 20;
    if (N > 1000) N = 1000;
    r.f1_hz = (float)N * SCOPE_FREQ_STEP;

    /* ═══════════════════════════════════════════════
     * 阶段 5: 基波相位提取 (在原始 bin 处)
     * ═══════════════════════════════════════════════ */
    float fund_phase_raw = extract_phase(peak_bins[(uint16_t)best_fund_idx], SCOPE_FFT_SIZE);

    /* ═══════════════════════════════════════════════
     * 阶段 6: 谐波搜索 (f_k = k × f₁ 附近)
     * ═══════════════════════════════════════════════ */

    r.harmonic_count = 0;
    uint8_t peak_claimed[32] = {0};
    peak_claimed[(uint16_t)best_fund_idx] = 1;

    /* 谐波搜索阈值: 全局最强峰的 5% */
    {
        float global_max = 0.0f;
        for (uint16_t i = 0; i < peak_count; i++)
            if (peak_mags[i] > global_max) global_max = peak_mags[i];
        float threshold = global_max * SCOPE_PEAK_THRESH;

        for (uint8_t order = 2; order <= (SCOPE_MAX_HARM + 1); order++)
        {
        float target_freq = (float)order * r.f1_hz;

        /* 超过 Nyquist 的 90% 停止搜索 */
        if (target_freq > fs_hz * 0.45f) break;

        int target_bin = (int)(target_freq / fs_hz * (float)SCOPE_FFT_SIZE + 0.5f);

        int search_start = target_bin - SCOPE_HARM_SEARCH_BINS;
        int search_end   = target_bin + SCOPE_HARM_SEARCH_BINS;
        if (search_start < (int)SCOPE_MIN_BIN) search_start = SCOPE_MIN_BIN;
        if (search_end   >= (int)nyquist)     search_end   = nyquist - 1;

        int   best_bin = -1;
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
                             &h_freq, &h_amp, fs_hz, SCOPE_FFT_SIZE);

            float h_phase_raw = extract_phase((uint16_t)best_bin, SCOPE_FFT_SIZE);

            uint8_t idx = r.harmonic_count;
            r.harmonics[idx].freq_hz   = h_freq;
            r.harmonics[idx].vpeak_mV  = 0.0f; /* 待校准阶段填充 */
            r.harmonics[idx].phase_rad = 0.0f; /* 待校准阶段填充 */
            r.harmonics[idx].bin       = (uint16_t)best_bin;
            r.harmonics[idx].is_interference = 0;

            /* 存储中间值 (ADC域, 校准前) — 用 phase_rad 暂存 */
            r.harmonics[idx].phase_rad = h_phase_raw;    /* 暂存原始相位 */
            /* vpeak_mV 暂存 ADC域 amplitude */
            r.harmonics[idx].vpeak_mV  = h_amp;           /* 暂存 ADC域幅度 */

            r.harmonic_count++;

            /* 标记该峰值已被认领 */
            for (uint16_t p = 0; p < peak_count; p++)
                if (peak_bins[p] == (uint16_t)best_bin)
                    peak_claimed[p] = 1;

            if (r.harmonic_count >= SCOPE_MAX_HARM) break;
        }
    }
    }

    /* ═══════════════════════════════════════════════
     * 阶段 7: 三校准链 — 从 ADC 域 → 原始输入域 mV
     * ================================================================ */

    /* ── 应用到基波 ── */
    r.fund_vpeak_mV  = calibrate_vpeak_mV(fund_amp_raw, r.f1_hz, r.agc_gain);
    r.fund_phase_rad = calibrate_phase_rad(fund_phase_raw, r.f1_hz);

    /* ── 应用到各谐波 ── */
    for (uint8_t i = 0; i < r.harmonic_count; i++)
    {
        float adc_amp   = r.harmonics[i].vpeak_mV;   /* 暂存的 ADC 幅度 */
        float raw_phase = r.harmonics[i].phase_rad;  /* 暂存的原始相位 */
        float freq      = (float)(i + 2) * r.f1_hz; /* 精确谐波频率 = order × f₁ */

        r.harmonics[i].vpeak_mV  = calibrate_vpeak_mV(adc_amp, freq, r.agc_gain);
        r.harmonics[i].phase_rad = calibrate_phase_rad(raw_phase, freq);
        r.harmonics[i].freq_hz   = freq;  /* 谐波频率吸附到精确整数倍 */
    }

    /* ── 时域 Vpp/Vrms 也做 AGC + H_chain 修正 ── */
    {
        float H1 = ScopeCalib_GetHchain(r.f1_hz);
        if (H1 < 0.5f) H1 = 0.5f;

        float vpp_V   = adc_code_to_V(vpp_adc) / r.agc_gain / H1;
        float vrms_V  = adc_code_to_V(vrms_adc) / r.agc_gain / H1;
        r.vpp_mV  = vpp_V * 1000.0f;
        r.vrms_mV = vrms_V * 1000.0f;
    }

    /* ═══════════════════════════════════════════════
     * 阶段 8: 干扰识别 (要求3用)
     *
     * 未被认领的峰值 = 潜在的干扰分量
     * 只标记，不混入谐波列表
     * ═══════════════════════════════════════════════ */

    /* 干扰阈值: 全局最强峰的 5% (与谐波搜索阈值一致) */
    float interference_threshold = peak_mags[0] * SCOPE_PEAK_THRESH;

    uint8_t interference_count = 0;
    for (uint16_t i = 0; i < peak_count; i++)
    {
        if (peak_claimed[i]) continue;

        /* 幅值低于阈值 → 忽略 (噪声/杂散, 非干扰) */
        if (peak_mags[i] < interference_threshold) continue;

        float pf = (float)peak_bins[i] * fs_hz / (float)SCOPE_FFT_SIZE;

        /* 检查是否落在某个谐波整数倍的 ±5% 以内
           (可能是 FFT 泄漏导致谐波没被搜到) */
        uint8_t near_harmonic = 0;
        for (uint8_t o = 1; o <= (SCOPE_MAX_HARM + 1); o++)
        {
            float expected = (float)o * r.f1_hz;
            if (ABS_F(pf - expected) / expected < 0.05f)
            {
                near_harmonic = 1;
                break;
            }
        }

        if (!near_harmonic)
        {
            /* 确认为干扰/杂散谱线 */
            interference_count++;
        }
    }

    /* ═══════════════════════════════════════════════
     * 阶段 9: 置信度判定
     * ═══════════════════════════════════════════════ */

    if (interference_count == 0)
        r.confidence = 0;  /* HIGH — 无干扰杂散 */
    else if (interference_count <= 3)
        r.confidence = 1;  /* MEDIUM — 可能有干扰 */
    else
        r.confidence = 2;  /* LOW — 噪声/干扰严重 */

    return r;
}

/* ============================================================
 * ScopeFFT_Print — 串口打印 (mV 单位)
 * ============================================================ */
void ScopeFFT_Print(const ScopeResult *r)
{
    char buf[256];

    HAL_UART_Transmit(&huart2, (uint8_t *)"\r\n", 2, 1000);

    /* ── 频率 ── */
    snprintf(buf, sizeof(buf),
             "f1 = %.0f Hz  (raw=%.1f, N=%d, res=%.1f Hz)\r\n",
             r->f1_hz, r->f1_raw_hz,
             (int)(r->f1_hz / SCOPE_FREQ_STEP + 0.5f),
             r->bin_resolution);
    HAL_UART_Transmit(&huart2, (uint8_t *)buf, strlen(buf), 1000);

    /* ── 时域 ── */
    snprintf(buf, sizeof(buf),
             "Vdc=%.0fmV  Vpp=%.1fmV  Vrms=%.1fmV  AGC_G=%.2f\r\n",
             r->vdc_mV, r->vpp_mV, r->vrms_mV, r->agc_gain);
    HAL_UART_Transmit(&huart2, (uint8_t *)buf, strlen(buf), 1000);

    /* ── 基波 ── */
    snprintf(buf, sizeof(buf),
             "Fund: %.1f mVpeak,  phi=%.3f rad (%.1f deg)\r\n",
             r->fund_vpeak_mV, r->fund_phase_rad,
             r->fund_phase_rad * 180.0f / PI);
    HAL_UART_Transmit(&huart2, (uint8_t *)buf, strlen(buf), 1000);

    /* ── 谐波 ── */
    for (uint8_t i = 0; i < r->harmonic_count; i++)
    {
        snprintf(buf, sizeof(buf),
                 "  H%-2d: %7.0f Hz  %7.1f mVpeak  phi=%+.3f rad (%+.1f deg)\r\n",
                 i + 2,
                 r->harmonics[i].freq_hz,
                 r->harmonics[i].vpeak_mV,
                 r->harmonics[i].phase_rad,
                 r->harmonics[i].phase_rad * 180.0f / PI);
        HAL_UART_Transmit(&huart2, (uint8_t *)buf, strlen(buf), 1000);
    }

    /* ── 置信度 ── */
    const char *conf;
    switch (r->confidence)
    {
        case 0: conf = "HIGH";   break;
        case 1: conf = "MEDIUM"; break;
        default: conf = "LOW";   break;
    }
    snprintf(buf, sizeof(buf), "Confidence: %s\r\n", conf);
    HAL_UART_Transmit(&huart2, (uint8_t *)buf, strlen(buf), 1000);
}
