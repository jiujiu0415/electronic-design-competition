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
 * classify_group — 根据谐波比值判断一组峰的波形类型
 * ============================================================ */
static void classify_group(FFT_Result *r, uint8_t *group_id,
                           uint8_t gid, float fund_amp,
                           char *out_waveform)
{
    float h2 = 0.0f, h3 = 0.0f, h5 = 0.0f;

    for (uint8_t i = 0; i < r->peak_count; i++)
    {
        if (group_id[i] != gid) continue;
        uint8_t h = r->peaks[i].harmonic;
        if      (h == 2) h2 = r->peaks[i].amplitude;
        else if (h == 3) h3 = r->peaks[i].amplitude;
        else if (h == 5) h5 = r->peaks[i].amplitude;
    }

    float r2 = (fund_amp > 0.001f) ? h2 / fund_amp : 0.0f;
    float r3 = (fund_amp > 0.001f) ? h3 / fund_amp : 0.0f;
    float r5 = (fund_amp > 0.001f) ? h5 / fund_amp : 0.0f;
    float th = r2 + r3 + r5;

    if      (th < 0.06f)                              str_copy(out_waveform, "SINE");
    else if (r3 < 0.05f && r5 < 0.05f)                str_copy(out_waveform, "SINE");   /* 失真正弦: 有谐波但无奇次谐波(3/5次) */
    else if (r2 < 0.08f && r3 > 0.05f && r3 > r5*2)  str_copy(out_waveform, "TRIANGLE");
    else if (r2 < 0.10f && r3 > 0.20f && r5 > 0.10f) str_copy(out_waveform, "SQUARE");
    else if (r2 > 0.30f)                              str_copy(out_waveform, "SAWTOOTH");
    else                                              str_copy(out_waveform, "COMPOSITE");
}

/* ============================================================
 * classify_peaks — 谐波分组 + 每组独立识别波形
 *
 * 原理: 单根谱线看不出波形类型, 必须看谐波关系。
 *   - 先把所有峰按"谁是谁的谐波"分组 (低频→高频扫描)
 *   - 每组独立用谐波幅值比值判断波形 (和旧的 classify_waveform 规则一样)
 *   - 每个峰得到独立的 waveform 标签 + 谐波次数
 *   - 多于1组 → 整体判定 COMPOSITE (混合信号)
 *
 * 无法处理的情况 (FFT 物理极限):
 *   两个信号恰好在同一频率/谐波频率重叠 → 能量混在一起, 无法拆分
 * ============================================================ */
static void classify_peaks(FFT_Result *r)
{
    uint8_t i, g;

    if (r->peak_count < 1)
    {
        str_copy(r->waveform, "NONE");
        return;
    }

    /* 先按频率升序排 (谐波分组依赖频率顺序) */
    for (i = 0; i < r->peak_count; i++)
    {
        for (uint8_t j = i + 1; j < r->peak_count; j++)
        {
            if (r->peaks[j].freq_hz < r->peaks[i].freq_hz)
            {
                FFT_Peak tmp = r->peaks[i];
                r->peaks[i]  = r->peaks[j];
                r->peaks[j]  = tmp;
            }
        }
    }

    /* ── 步骤A: 给峰分配谐波分组 (低频→高频) ── */
    uint8_t group_id[MAX_PEAKS];
    float   group_f0[MAX_PEAKS];
    uint8_t num_groups = 0;

    for (i = 0; i < r->peak_count; i++)
        group_id[i] = 0;

    for (i = 0; i < r->peak_count; i++)
    {
        /* 先检查: 当前峰是不是某个已有分组的谐波? */
        uint8_t assigned = 0;
        for (g = 0; g < num_groups; g++)
        {
            float ratio = r->peaks[i].freq_hz / group_f0[g];
            int   near  = (int)(ratio + 0.5f);
            if (near < 2 || near > 10) continue;
            float err   = ABS_F(ratio - (float)near);
            if (err < 0.05f)
            {
                group_id[i] = g + 1;
                r->peaks[i].harmonic = (uint8_t)near;
                assigned = 1;
                break;
            }
        }
        if (assigned) continue;

        /* 不是任何已有组的谐波 → 新建分组, 这个峰就是基波 */
        num_groups++;
        group_id[i] = num_groups;
        group_f0[num_groups - 1] = r->peaks[i].freq_hz;
        r->peaks[i].harmonic = 1;
    }

    /* ── 步骤B: 每组独立分类 ── */
    for (g = 0; g < num_groups; g++)
    {
        /* 找这个组里基波的幅度 */
        float fund_amp = 0.0f;
        for (i = 0; i < r->peak_count; i++)
        {
            if (group_id[i] == g + 1 && r->peaks[i].harmonic == 1)
            {
                fund_amp = r->peaks[i].amplitude;
                break;
            }
        }

        char grp_wave[16];
        classify_group(r, group_id, g + 1, fund_amp, grp_wave);

        /* 同组所有峰打上相同的波形标签 */
        for (i = 0; i < r->peak_count; i++)
        {
            if (group_id[i] == g + 1)
                str_copy(r->peaks[i].waveform, grp_wave);
        }
    }

    /* ── 步骤C: 整体判定 ── */
    if (num_groups > 1)
        str_copy(r->waveform, "COMPOSITE");
    else
        str_copy(r->waveform, r->peaks[0].waveform);
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

    /* 阈值 = 最大峰值的 5%
       选 5% 而非更低, 是为了过滤 FFT 噪声地板的随机波动。
       纯正弦波的真实谐波 < 1%, 低于阈值不会被误抓。
       三角波 3 次谐波 ~11%、方波 ~33%, 5% 阈值能正确捕获。 */
    float threshold = max_mag * 0.05f;
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

    /* ── 7. 谐波分组 + 波形识别 (内部先按频率排序再做分组) ── */
    classify_peaks(&result);

    /* 按幅度降序排列 (显示用, peak_count ≤ 8) */
    for (uint8_t i = 0; i < result.peak_count; i++)
    {
        for (uint8_t j = i + 1; j < result.peak_count; j++)
        {
            if (result.peaks[j].amplitude > result.peaks[i].amplitude)
            {
                FFT_Peak tmp = result.peaks[i];
                result.peaks[i] = result.peaks[j];
                result.peaks[j] = tmp;
            }
        }
    }

    /* ── 8. 重启 ── */
    data_ready = 0;
    HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_buf, ADC_BUF_SIZE);

    return result;
}

float ADC_FFT_GetSampleRate(void)
{
    return sample_rate;
}
