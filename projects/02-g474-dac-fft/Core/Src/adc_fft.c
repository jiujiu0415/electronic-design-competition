/**
 * adc_fft.c — ADC采集 + FFT频谱分析 v2 实现 (STM32G474)
 */

#include "adc_fft.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

/* ============================================================
 * 外部引用
 * ============================================================ */
extern ADC_HandleTypeDef hadc1;
extern TIM_HandleTypeDef htim2;

/* ============================================================
 * 静态变量
 * ============================================================ */
static uint16_t adc_buf[ADC_BUF_SIZE];
static volatile uint8_t data_ready = 0;

static arm_rfft_fast_instance_f32 fft_inst;
static float32_t fft_in[FFT_SIZE];
static float32_t fft_out[FFT_SIZE * 2];
static float32_t fft_mag[FFT_SIZE];
static float sample_rate = 0.0f;

/* ============================================================
 * 峰值检测辅助
 * ============================================================ */
static int cmp_desc(const void *a, const void *b)
{
    float fa = *(float *)a, fb = *(float *)b;
    return (fa < fb) ? 1 : ((fa > fb) ? -1 : 0);
}

/* ============================================================
 * 过零检测: 统计 ADC 缓冲区中穿越 DC 均值的次数
 * 返回频率 (Hz), 比 FFT 分辨率精确得多
 * ============================================================ */
static float zero_cross_freq(uint16_t *buf, uint16_t len, float dc)
{
    uint32_t crossings = 0;
    uint8_t above;

    /* 找到第一个非直流点 */
    uint16_t start = 0;
    while (start < len && fabsf((float)buf[start] - dc) < 3.0f) start++;
    if (start >= len) return 0.0f;

    above = (buf[start] > dc);

    for (uint16_t i = start + 1; i < len; i++)
    {
        float val = (float)buf[i];
        if (above && val < dc - 3.0f)  { crossings++; above = 0; }
        if (!above && val > dc + 3.0f) { crossings++; above = 1; }
    }

    if (crossings < 2) return 0.0f;

    /* 周期数 = 过零次数 / 2,  时长 = len / sample_rate */
    float cycles = (float)crossings * 0.5f;
    return cycles * sample_rate / (float)len;
}

/* ============================================================
 * 谐波分析 → 识别波形类型
 * ============================================================ */
static void classify_waveform(FFT_Result *r)
{
    if (r->peak_count < 1) { strcpy(r->waveform, "NONE"); return; }

    float fund = r->peaks[0].amplitude;
    float h2 = 0, h3 = 0, h4 = 0, h5 = 0;

    /* 在各次谐波频率附近找对应峰 */
    float f0 = r->peaks[0].freq_hz;
    for (uint8_t i = 0; i < r->peak_count; i++)
    {
        float ratio = r->peaks[i].freq_hz / f0;
        if      (ratio > 1.90f && ratio < 2.10f) h2 = r->peaks[i].amplitude;
        else if (ratio > 2.90f && ratio < 3.10f) h3 = r->peaks[i].amplitude;
        else if (ratio > 3.90f && ratio < 4.10f) h4 = r->peaks[i].amplitude;
        else if (ratio > 4.90f && ratio < 5.10f) h5 = r->peaks[i].amplitude;
    }

    float r2 = h2 / fund, r3 = h3 / fund, r5 = h5 / fund;
    float total_harm = r2 + r3 + r5;

    if (total_harm < 0.06f)                    strcpy(r->waveform, "SINE");
    else if (r2 < 0.08f && r3 > 0.05f && r5 > 0.01f
             && r3 > r5 * 2.0f)                strcpy(r->waveform, "TRIANGLE");
    else if (r2 < 0.10f && r3 > 0.20f
             && r5 > 0.10f)                     strcpy(r->waveform, "SQUARE");
    else if (r2 > 0.30f)                        strcpy(r->waveform, "SAWTOOTH");
    else                                        strcpy(r->waveform, "COMPOSITE");
}

/* ============================================================
 * HAL 回调
 * ============================================================ */
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == ADC1)
    {
        HAL_ADC_Stop_DMA(&hadc1);
        data_ready = 1;
    }
}

/* ============================================================
 * API 实现
 * ============================================================ */

void ADC_FFT_Init(void)
{
    sample_rate = 160000000.0f
                / (float)(htim2.Instance->PSC + 1)
                / (float)(htim2.Instance->ARR + 1);

    arm_rfft_fast_init_f32(&fft_inst, FFT_SIZE);
    HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED);
    HAL_TIM_Base_Start(&htim2);
    HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_buf, ADC_BUF_SIZE);
}

uint8_t ADC_FFT_DataReady(void)
{
    return data_ready;
}

FFT_Result ADC_FFT_Analyze(void)
{
    FFT_Result result;
    memset(&result, 0, sizeof(result));

    /* ── 1. DC 偏置 ── */
    float sum = 0.0f;
    for (uint16_t i = 0; i < FFT_SIZE; i++) sum += (float)adc_buf[i];
    float dc = sum / (float)FFT_SIZE;
    result.dc_offset_v = dc * 3.3f / 4096.0f;

    /* ── 2. 过零检测测频 (精度远高于 FFT bin 分辨率) ── */
    result.freq_zc = zero_cross_freq(adc_buf, FFT_SIZE, dc);

    /* ── 3. FFT 输入: 去直流 + Hann 窗 ── */
    for (uint16_t i = 0; i < FFT_SIZE; i++)
    {
        float val = (float)adc_buf[i] - dc;
        float window = 0.5f * (1.0f - arm_cos_f32(2.0f * PI * i / (FFT_SIZE - 1)));
        fft_in[i] = val * window;
    }

    /* ── 4. 实数 FFT ── */
    arm_rfft_fast_f32(&fft_inst, fft_in, fft_out, 0);
    arm_cmplx_mag_f32(fft_out, fft_mag, FFT_SIZE);

    /* ── 5. 归一化 ── */
    float hann_gain = 0.5f;
    for (uint16_t i = 0; i < FFT_SIZE; i++)
    {
        fft_mag[i] = (i == 0)
            ? fft_mag[i] / (float)FFT_SIZE
            : fft_mag[i] / (float)FFT_SIZE * 2.0f;
        fft_mag[i] /= hann_gain;
    }

    /* ── 6. 多峰值检测 ── */
    /* 用拷贝排序, 找出峰值对应的索引 */
    float mag_sorted[FFT_SIZE / 2];
    uint16_t indices[FFT_SIZE / 2];
    for (uint16_t i = 1; i < FFT_SIZE / 2; i++)
    {
        mag_sorted[i - 1] = fft_mag[i];
        indices[i - 1] = i;
    }
    /* 冒泡找出最大的 MAX_PEAKS*3 个候选峰 (简单实现) */
    for (uint16_t i = 0; i < (uint16_t)(MAX_PEAKS * 3) && i < FFT_SIZE / 2 - 1; i++)
    {
        uint16_t max_j = i;
        for (uint16_t j = i + 1; j < FFT_SIZE / 2 - 1; j++)
            if (mag_sorted[j] > mag_sorted[max_j]) max_j = j;
        /* swap */
        float tf = mag_sorted[i]; mag_sorted[i] = mag_sorted[max_j]; mag_sorted[max_j] = tf;
        uint16_t ti = indices[i]; indices[i] = indices[max_j]; indices[max_j] = ti;
    }

    /* 从候选峰中筛选: 必须是局部极大值, 且高于阈值 */
    float max_mag = mag_sorted[0];
    result.peak_count = 0;
    for (uint16_t c = 0; c < (uint16_t)(MAX_PEAKS * 3) && result.peak_count < MAX_PEAKS; c++)
    {
        uint16_t k = indices[c];
        if (k < 1 || k >= FFT_SIZE / 2 - 1) continue;

        /* 必须同时大于左右邻居 */
        if (fft_mag[k] <= fft_mag[k - 1] || fft_mag[k] <= fft_mag[k + 1])
            continue;

        /* 必须高于阈值 */
        if (fft_mag[k] < max_mag * PEAK_THRESHOLD) continue;

        /* 抛物线插值 */
        float alpha = fft_mag[k - 1], beta = fft_mag[k], gamma = fft_mag[k + 1];
        float delta = 0.0f;
        float denom = 2.0f * beta - alpha - gamma;
        if (fabsf(denom) > 1e-9f) delta = 0.5f * (alpha - gamma) / denom;

        float freq = ((float)k + delta) * sample_rate / (float)FFT_SIZE;
        float real = fft_out[k * 2], imag = fft_out[k * 2 + 1];
        float phase_deg = atan2f(imag, real) * 180.0f / PI;

        result.peaks[result.peak_count].freq_hz   = freq;
        result.peaks[result.peak_count].amplitude  = fft_mag[k] / 2048.0f;
        result.peaks[result.peak_count].phase_deg  = phase_deg;
        result.peak_count++;
    }

    /* ── 7. 波形类型识别 ── */
    classify_waveform(&result);

    /* ── 8. 重启 ── */
    data_ready = 0;
    HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_buf, ADC_BUF_SIZE);

    return result;
}

float ADC_FFT_GetSampleRate(void)
{
    return sample_rate;
}
