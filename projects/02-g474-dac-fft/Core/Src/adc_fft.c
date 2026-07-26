/**
 * adc_fft.c — ADC采集 + FFT频谱分析实现 (STM32G474)
 *
 * 依赖: 仅 stm32g4xx_hal.h + arm_math.h (通过 adc_fft.h)
 * 不额外 include 任何标准库头文件, 保持和 dac_wave.c 一致的模式
 */
#include "adc_fft.h"

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
 * 工具宏 (避免依赖 math.h)
 * ============================================================ */
#define ABS_F(x)  ((x) < 0.0f ? -(x) : (x))

static void str_copy(char *dst, const char *src)
{
    uint8_t i;
    for (i = 0; src[i] && i < 15; i++) dst[i] = src[i];
    dst[i] = '\0';
}

/* ============================================================
 * 过零检测测频
 * ============================================================ */
static float zero_cross_freq(uint16_t *buf, uint16_t len, float dc)
{
    uint32_t cross = 0;
    uint16_t start = 0;
    float diff;

    /* 跳过直流附近的采样点 */
    while (start < len)
    {
        diff = (float)buf[start] - dc;
        if (ABS_F(diff) >= 3.0f) break;
        start++;
    }
    if (start >= len) return 0.0f;

    uint8_t above = (buf[start] > dc);

    for (uint16_t i = start + 1; i < len; i++)
    {
        float val = (float)buf[i];
        if (above && val < dc - 3.0f)  { cross++; above = 0; }
        if (!above && val > dc + 3.0f) { cross++; above = 1; }
    }

    if (cross < 2) return 0.0f;
    return (float)cross * 0.5f * sample_rate / (float)len;
}

/* ============================================================
 * 波形类型识别 (基于谐波比值)
 * ============================================================ */
static void classify_waveform(FFT_Result *r)
{
    if (r->peak_count < 1) { str_copy(r->waveform, "NONE"); return; }

    float fund = r->peaks[0].amplitude;
    float f0   = r->peaks[0].freq_hz;
    float h2 = 0.0f, h3 = 0.0f, h5 = 0.0f;
    float ratio;

    for (uint8_t i = 0; i < r->peak_count; i++)
    {
        ratio = r->peaks[i].freq_hz / f0;
        if      (ratio > 1.90f && ratio < 2.10f) h2 = r->peaks[i].amplitude;
        else if (ratio > 2.90f && ratio < 3.10f) h3 = r->peaks[i].amplitude;
        else if (ratio > 4.90f && ratio < 5.10f) h5 = r->peaks[i].amplitude;
    }

    float r2 = (fund > 0.001f) ? h2 / fund : 0.0f;
    float r3 = (fund > 0.001f) ? h3 / fund : 0.0f;
    float r5 = (fund > 0.001f) ? h5 / fund : 0.0f;
    float th = r2 + r3 + r5;

    if      (th < 0.06f)                              str_copy(r->waveform, "SINE");
    else if (r2 < 0.08f && r3 > 0.05f && r3 > r5*2)  str_copy(r->waveform, "TRIANGLE");
    else if (r2 < 0.10f && r3 > 0.20f && r5 > 0.10f) str_copy(r->waveform, "SQUARE");
    else if (r2 > 0.30f)                              str_copy(r->waveform, "SAWTOOTH");
    else                                              str_copy(r->waveform, "COMPOSITE");
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
    {
        uint8_t *p = (uint8_t *)&result;
        for (uint16_t i = 0; i < sizeof(FFT_Result); i++) p[i] = 0;
    }

    /* ── 1. DC ── */
    float sum = 0.0f;
    for (uint16_t i = 0; i < FFT_SIZE; i++)
        sum += (float)adc_buf[i];
    float dc = sum / (float)FFT_SIZE;
    result.dc_offset_v = dc * 3.3f / 4096.0f;

    /* ── 2. Zero-cross ── */
    result.freq_zc = zero_cross_freq(adc_buf, FFT_SIZE, dc);

    /* ── 3. Hann window + FFT input ── */
    for (uint16_t i = 0; i < FFT_SIZE; i++)
    {
        float val = (float)adc_buf[i] - dc;
        float w = 0.5f * (1.0f - arm_cos_f32(2.0f * PI * i / (float)(FFT_SIZE - 1)));
        fft_in[i] = val * w;
    }

    /* ── 4. FFT + magnitude ── */
    arm_rfft_fast_f32(&fft_inst, fft_in, fft_out, 0);
    arm_cmplx_mag_f32(fft_out, fft_mag, FFT_SIZE);

    /* ── 5. Normalize ── */
    for (uint16_t i = 0; i < FFT_SIZE; i++)
    {
        fft_mag[i] = (i == 0)
            ? fft_mag[i] / (float)FFT_SIZE
            : fft_mag[i] / (float)FFT_SIZE * 2.0f;
        fft_mag[i] /= 0.5f;   /* Hann 窗增益补偿 */
    }

    /* ── 6. 多峰值检测 ── */
    /* 先找最大值 */
    float max_mag = 0.0f;
    for (uint16_t i = 1; i < FFT_SIZE / 2; i++)
        if (fft_mag[i] > max_mag) max_mag = fft_mag[i];

    float threshold = max_mag * 0.02f;
    result.peak_count = 0;

    for (uint16_t k = 1; k < FFT_SIZE / 2 - 1 && result.peak_count < MAX_PEAKS; k++)
    {
        /* 必须大于左右邻居 */
        if (fft_mag[k] <= fft_mag[k - 1]) continue;
        if (fft_mag[k] <= fft_mag[k + 1]) continue;
        if (fft_mag[k] < threshold) continue;

        /* 抛物线插值 */
        float a = fft_mag[k - 1], b = fft_mag[k], c = fft_mag[k + 1];
        float d = 0.0f;
        float denom = 2.0f * b - a - c;
        if (ABS_F(denom) > 1e-9f) d = 0.5f * (a - c) / denom;

        float freq  = ((float)k + d) * sample_rate / (float)FFT_SIZE;
        float real  = fft_out[k * 2];
        float imag  = fft_out[k * 2 + 1];
        float phase = atan2f(imag, real) * 180.0f / PI;

        uint8_t n = result.peak_count;
        result.peaks[n].freq_hz   = freq;
        result.peaks[n].amplitude  = fft_mag[k] / 2048.0f;
        result.peaks[n].phase_deg  = phase;
        result.peak_count++;
    }

    /* ── 7. 波形识别 ── */
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
