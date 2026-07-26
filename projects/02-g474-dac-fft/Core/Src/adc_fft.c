/**
 * adc_fft.c — ADC采集 + FFT频谱分析实现 (STM32G474)
 */

#include "adc_fft.h"
#include <math.h>
#include <string.h>

/* ============================================================
 * 外部引用 (CubeMX 生成)
 * ============================================================ */
extern ADC_HandleTypeDef hadc1;
extern TIM_HandleTypeDef htim2;

/* ============================================================
 * 静态变量
 * ============================================================ */

static uint16_t adc_buf[ADC_BUF_SIZE];          /* DMA 缓冲区 */
static volatile uint8_t data_ready = 0;          /* 数据就绪标志 */

/* FFT 工作区 */
static arm_rfft_fast_instance_f32 fft_inst;
static float32_t fft_in[FFT_SIZE];               /* 输入 (实数) */
static float32_t fft_out[FFT_SIZE * 2];          /* 输出 (复数: 实+虚交替) */
static float32_t fft_mag[FFT_SIZE];              /* 幅度谱 */

static float sample_rate = 0.0f;                 /* 实际采样率 */

/* ============================================================
 * 内部函数: 抛物线插值, 精细化峰值位置
 * ============================================================ */

/**
 * refine_peak — 三点抛物线插值
 *
 * 用峰值 bin (k) 及其左右邻居 (k-1, k+1) 拟合抛物线,
 * 返回亚 bin 精度的峰值位置偏移 delta (-0.5 ~ +0.5)
 */
static float refine_peak(float left, float center, float right)
{
    float denom = 2.0f * center - left - right;
    if (fabsf(denom) < 1e-9f) return 0.0f;
    return 0.5f * (left - right) / denom;
}

/* ============================================================
 * HAL 回调: DMA 传输完成
 * ============================================================ */

/**
 * HAL_ADC_ConvCpltCallback — ADC DMA 半满/全满回调
 *
 * HAL 库在 DMA 完成时自动调用。我们只置标志位,
 * 实际 FFT 计算在主循环中执行, 避免在中断里做浮点运算。
 */
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

/**
 * ADC_FFT_Init
 */
void ADC_FFT_Init(void)
{
    /* 计算实际采样率 */
    sample_rate = 160000000.0f
                / (float)(htim2.Instance->PSC + 1)
                / (float)(htim2.Instance->ARR + 1);

    /* 初始化 FFT */
    arm_rfft_fast_init_f32(&fft_inst, FFT_SIZE);

    /* ADC 校准 */
    HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED);

    /* 启动 TIM2 (ADC触发源) */
    HAL_TIM_Base_Start(&htim2);

    /* 启动 ADC DMA 循环采集 */
    HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_buf, ADC_BUF_SIZE);
}

/**
 * ADC_FFT_DataReady
 */
uint8_t ADC_FFT_DataReady(void)
{
    return data_ready;
}

/**
 * ADC_FFT_Analyze — FFT 分析主流程
 *
 *  1. ADC数据 → float (去偏置)
 *  2. 实数 FFT
 *  3. 计算幅度谱
 *  4. 归一化
 *  5. 找峰值 bin
 *  6. 抛物线插值求精
 *  7. 计算频率/幅度/相位
 *  8. 重启 ADC
 */
FFT_Result ADC_FFT_Analyze(void)
{
    FFT_Result result;
    memset(&result, 0, sizeof(result));

    /* ── 1. 计算 DC 偏置, 去直流 ── */
    float sum = 0.0f;
    for (uint16_t i = 0; i < FFT_SIZE; i++)
        sum += (float)adc_buf[i];
    float dc = sum / (float)FFT_SIZE;
    result.dc_offset = dc * 3.3f / 4096.0f;   /* 换算为电压 */

    /* ── 2. 填充 FFT 输入 (去直流 + 加窗) ── */
    for (uint16_t i = 0; i < FFT_SIZE; i++)
    {
        /* 去直流 */
        float val = (float)adc_buf[i] - dc;

        /* Hann 窗: 0.5*(1-cos(2*pi*n/N)), 抑制频谱泄漏 */
        float window = 0.5f * (1.0f - arm_cos_f32(2.0f * PI * i / (FFT_SIZE - 1)));
        fft_in[i] = val * window;
    }

    /* ── 3. 实数 FFT ── */
    arm_rfft_fast_f32(&fft_inst, fft_in, fft_out, 0);

    /* ── 4. 计算幅度谱 ── */
    arm_cmplx_mag_f32(fft_out, fft_mag, FFT_SIZE);

    /* ── 5. 幅度归一化 ── */
    /* DC: /N, 其余: /N*2 (单边谱), 再补偿 Hann 窗的幅度衰减 */
    float hann_gain = 0.5f;   /* Hann 窗幅度损失约 0.5 */
    for (uint16_t i = 0; i < FFT_SIZE; i++)
    {
        if (i == 0)
            fft_mag[i] = fft_mag[i] / (float)FFT_SIZE;
        else
            fft_mag[i] = fft_mag[i] / (float)FFT_SIZE * 2.0f;
        fft_mag[i] /= hann_gain;
    }

    /* ── 6. 找幅度峰值 (跳过 DC) ── */
    uint16_t peak = 1;
    for (uint16_t i = 2; i < FFT_SIZE / 2; i++)
        if (fft_mag[i] > fft_mag[peak])
            peak = i;

    /* ── 7. 抛物线插值 ── */
    float delta = refine_peak(fft_mag[peak - 1], fft_mag[peak], fft_mag[peak + 1]);
    float freq_frac = ((float)peak + delta) * sample_rate / (float)FFT_SIZE;

    /* ── 8. 相位 (在峰值 bin 处计算) ── */
    float real_part = fft_out[peak * 2];       /* 实部 */
    float imag_part = fft_out[peak * 2 + 1];   /* 虚部 */
    float phase_rad = atan2f(imag_part, real_part);
    float phase_deg = phase_rad * 180.0f / PI;

    /* ── 9. 填充结果 ── */
    result.freq_hz   = freq_frac;
    result.amplitude = fft_mag[peak] / 2048.0f;   /* 归一化到 0~1 (半满量程) */
    result.phase_deg = phase_deg;
    result.peak_idx  = peak;

    /* ── 10. 重启 ADC ── */
    data_ready = 0;
    HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_buf, ADC_BUF_SIZE);

    return result;
}

/**
 * ADC_FFT_GetSampleRate
 */
float ADC_FFT_GetSampleRate(void)
{
    return sample_rate;
}
