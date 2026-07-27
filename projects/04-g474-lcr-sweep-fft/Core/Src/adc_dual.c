/**
 * adc_dual.c — ADC1 双通道同步采集 + FFT 分析实现 (STM32G474)
 *
 * 流程:
 *   1. TIM2 触发 ADC1, 双通道扫描 (CH1→CH2)
 *   2. DMA 循环写入 adc_buf[4096]: CH1₀,CH2₀, CH1₁,CH2₁, ...
 *   3. 采集满 2048 对后停止 DMA
 *   4. 分离两通道 → 去直流 → Hann 窗 → FFT
 *   5. 在目标频率 bin 提取幅度和相位
 *   6. 计算增益 + 相位差 → 返回
 *   7. 重启 DMA, 准备下一轮
 */

#include "adc_dual.h"

/* ============================================================
 * 外部引用 — CubeMX 生成在 main.c
 * ============================================================ */
extern ADC_HandleTypeDef hadc1;
extern TIM_HandleTypeDef htim2;

/* ============================================================
 * 静态变量
 * ============================================================ */

/* DMA 缓冲区: 双通道交错, 4096 个 uint16_t (2048 对) */
static uint16_t adc_buf[ADC_BUF_SIZE];
static volatile uint8_t data_ready = 0;

/* FFT 相关 — 复用一个 FFT 实例, 两个通道串行处理 */
static arm_rfft_fast_instance_f32 fft_inst;
static float32_t fft_in[FFT_SIZE];
static float32_t fft_out[FFT_SIZE * 2];
static float32_t fft_mag[FFT_SIZE];
static float sample_rate = 0.0f;

/* TIM2 输入时钟 = APB1 Timer Clock = 170MHz (APB1=/1) */
#define TIM2_CLK_HZ  170000000.0f

/* 每个频点目标采集的周期数
 * 低频信号弱, 需要更多周期平均 → 调到 40
 * 高频信号强, 20 个周期就够
 * 这个值越大 → DMA 等待越久, 但低频测量越稳 */
#define TARGET_CYCLES_LO  50.0f   /* f < 1kHz: 低频多用周期 */
#define TARGET_CYCLES_HI  20.0f   /* f ≥ 1kHz: 高频少等 */

/* 采样率上下限
 * 下限 5kHz: 给 100Hz 留足周期 (100*2048/50=4096, 钳到 5000 也可接受)
 * 上限 500kHz: 保证 ADC 扫描有足够时间 */
#define FS_MIN_HZ   5000.0f
#define FS_MAX_HZ  500000.0f

/* 工具宏: 避免依赖 math.h */
#define ABS_F(x)  ((x) < 0.0f ? -(x) : (x))

/* ============================================================
 * 工具函数: 分离 CH1 / CH2
 *
 * adc_buf 布局: [C1₀, C2₀, C1₁, C2₁, C1₂, C2₂, ...]
 * ch_buf[i]   = adc_buf[i * 2 + ch_offset]
 *   ch_offset: 0=CH1, 1=CH2
 * ============================================================ */
static void deinterleave(uint16_t *src, float32_t *dst, uint8_t channel)
{
    uint16_t offset = (channel == 1) ? 0 : 1;  /* CH1=0, CH2=1 */
    for (uint16_t i = 0; i < FFT_SIZE; i++)
    {
        dst[i] = (float32_t)src[i * 2 + offset];
    }
}

/* ============================================================
 * 工具函数: 计算直流分量
 * ============================================================ */
static float calc_dc(float32_t *buf, uint16_t len)
{
    double sum = 0.0;
    for (uint16_t i = 0; i < len; i++)
        sum += (double)buf[i];
    return (float)(sum / (double)len);
}

/* ============================================================
 * 工具函数: 在目标频率所在的 FFT bin 提取幅值和相位
 *
 * @param mag       FFT 幅度谱 (已归一化)
 * @param fft_cpx   FFT 复数输出 (arm_rfft_fast_f32 的输出)
 * @param target_hz 目标频率 (Hz)
 * @param bin_idx   输出: 最佳 bin 索引
 * @param mag_out   输出: 插值后的幅度
 * @param phase_out 输出: 相位 (度)
 * ============================================================ */
static void extract_at_freq(float32_t *mag, float32_t *fft_cpx,
                            float target_hz, uint16_t *bin_idx,
                            float *mag_out, float *phase_out)
{
    float bin_f = target_hz * (float)FFT_SIZE / sample_rate;
    int k = (int)(bin_f + 0.5f);

    /* 边界保护: bin 0 是 DC, 跳过; 不超过 Nyquist */
    if (k < 1) k = 1;
    if (k >= FFT_SIZE / 2) k = FFT_SIZE / 2 - 1;

    /* 抛物线插值: 用 k-1, k, k+1 三点修正频率和幅度 */
    float a = mag[k - 1], b = mag[k], c = mag[k + 1];
    float d = 0.0f;
    float denom = 2.0f * b - a - c;
    if (ABS_F(denom) > 1e-9f)
        d = 0.5f * (a - c) / denom;

    *bin_idx   = (uint16_t)k;
    *mag_out   = mag[k] + d * 0.25f * (a - c);  /* 幅度小幅修正 */
    *phase_out = atan2f(fft_cpx[k * 2 + 1], fft_cpx[k * 2]) * 180.0f / PI;
}

/* ============================================================
 * 单通道 FFT 分析
 *
 * 步骤:
 *   adc_raw[] → 分离到 fft_in[] → 去直流 → Hann 窗 → FFT → 归一化 → 提取
 * ============================================================ */
static ADC_ChannelResult analyze_channel(uint8_t channel, float target_hz)
{
    ADC_ChannelResult result;
    {
        /* 清零整个结构体 */
        uint8_t *p = (uint8_t *)&result;
        for (uint8_t i = 0; i < sizeof(ADC_ChannelResult); i++) p[i] = 0;
    }

    /* ── 1. 从 ADC 原始缓冲区分理出该通道 ── */
    deinterleave(adc_buf, fft_in, channel);

    /* ── 2. DC ── */
    float dc = calc_dc(fft_in, FFT_SIZE);
    result.dc_offset_v = dc * 3.3f / 4096.0f;

    /* ── 3. 去直流 + Hann 窗 ── */
    for (uint16_t i = 0; i < FFT_SIZE; i++)
    {
        float val = fft_in[i] - dc;
        float w   = 0.5f * (1.0f - arm_cos_f32(2.0f * PI * i / (float)(FFT_SIZE - 1)));
        fft_in[i] = val * w;
    }

    /* ── 4. FFT + 幅度 ── */
    arm_rfft_fast_f32(&fft_inst, fft_in, fft_out, 0);
    arm_cmplx_mag_f32(fft_out, fft_mag, FFT_SIZE);

    /* ── 5. 归一化 + Hann 窗补偿 ── */
    for (uint16_t i = 0; i < FFT_SIZE; i++)
    {
        fft_mag[i] = (i == 0)
            ? fft_mag[i] / (float)FFT_SIZE
            : fft_mag[i] / (float)FFT_SIZE * 2.0f;
        fft_mag[i] /= 0.5f;   /* Hann 窗增益补偿 */
    }

    /* ── 6. 在目标频率提取幅值/相位 ── */
    uint16_t bin;
    float mag_val, phase_val;
    extract_at_freq(fft_mag, fft_out, target_hz, &bin, &mag_val, &phase_val);

    result.freq_hz    = target_hz;
    result.amplitude  = mag_val / 2048.0f;
    result.phase_deg  = phase_val;

    return result;
}

/* ============================================================
 * HAL 回调 — DMA 完成时触发
 * ============================================================ */
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == ADC1)
    {
        HAL_ADC_Stop_DMA(&hadc1);   /* 暂停采集, 让 CPU 安心处理 */
        data_ready = 1;
    }
}

/* ============================================================
 * API 实现
 * ============================================================ */

void ADC_Dual_Init(void)
{
    /* 计算实际采样率 (每通道) */
    sample_rate = TIM2_CLK_HZ
                / (float)(htim2.Instance->PSC + 1)
                / (float)(htim2.Instance->ARR + 1);

    /* 初始化 FFT */
    arm_rfft_fast_init_f32(&fft_inst, FFT_SIZE);

    /* 校准 ADC */
    HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED);

    /* 启动触发源和 ADC */
    HAL_TIM_Base_Start(&htim2);
    HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_buf, ADC_BUF_SIZE);
}

void ADC_Dual_SetSampleRate(float target_freq_hz)
{
    /* 低频用更多周期, 高频用少点 (省 DMA 等待时间) */
    float cycles = (target_freq_hz < 1000.0f) ? TARGET_CYCLES_LO : TARGET_CYCLES_HI;

    float desired_fs = target_freq_hz * (float)FFT_SIZE / cycles;
    if (desired_fs < FS_MIN_HZ) desired_fs = FS_MIN_HZ;
    if (desired_fs > FS_MAX_HZ) desired_fs = FS_MAX_HZ;

    /* 计算 TIM2 ARR: Fs = TIM2_CLK / (PSC+1) / (ARR+1), PSC=0 */
    uint32_t arr = (uint32_t)(TIM2_CLK_HZ / desired_fs) - 1;
    if (arr < 10)  arr = 10;      /* 不能太小 */
    if (arr > 65535) arr = 65535; /* 16-bit 上限 */

    /* 停掉 ADC DMA + TIM2 */
    HAL_ADC_Stop_DMA(&hadc1);
    HAL_TIM_Base_Stop(&htim2);

    /* 更新 TIM2 自动重载值 */
    __HAL_TIM_SET_AUTORELOAD(&htim2, arr);
    htim2.Instance->ARR = arr;   /* 同步 HAL 句柄 */

    /* 更新采样率记录 */
    sample_rate = TIM2_CLK_HZ / (float)(arr + 1);

    /* 重启 */
    data_ready = 0;
    HAL_TIM_Base_Start(&htim2);
    HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_buf, ADC_BUF_SIZE);
}

uint8_t ADC_Dual_DataReady(void)
{
    return data_ready;
}

ADC_DualResult ADC_Dual_Analyze(float target_freq_hz)
{
    ADC_DualResult result;
    {
        uint8_t *p = (uint8_t *)&result;
        for (uint8_t i = 0; i < sizeof(ADC_DualResult); i++) p[i] = 0;
    }

    result.freq_hz = target_freq_hz;

    /* ── CH1: 输入参考 ── */
    result.ch1 = analyze_channel(1, target_freq_hz);

    /* ── CH2: 输出响应 ── */
    result.ch2 = analyze_channel(2, target_freq_hz);

    /* ── 计算传输函数 H(jω) = Vout / Vin ── */
    if (result.ch1.amplitude > 0.0001f)
    {
        result.gain    = result.ch2.amplitude / result.ch1.amplitude;
        result.gain_db = 20.0f * log10f(result.gain);
    }
    else
    {
        result.gain    = 0.0f;
        result.gain_db = -100.0f;
    }

    result.phase_diff_deg = result.ch2.phase_deg - result.ch1.phase_deg;

    /* 相位规整到 [-180, +180] */
    while (result.phase_diff_deg >  180.0f) result.phase_diff_deg -= 360.0f;
    while (result.phase_diff_deg < -180.0f) result.phase_diff_deg += 360.0f;

    /* ── 重启 DMA, 准备下一轮 ── */
    data_ready = 0;
    HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_buf, ADC_BUF_SIZE);

    return result;
}

float ADC_Dual_GetSampleRate(void)
{
    return sample_rate;
}
