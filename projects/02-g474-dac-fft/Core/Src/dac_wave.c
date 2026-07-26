/**
 * dac_wave.c — DAC 任意波形输出驱动实现 (STM32G474)
 *
 * 原理:
 *   TIM6 以 100kHz 触发 DAC DMA, DMA 循环输出波形表。
 *   支持: 正弦/三角/方波/锯齿波 以及 多分量叠加。
 */

#include "dac_wave.h"
#include <math.h>      /* fmodf */

/* ============================================================
 * 外部引用 (CubeMX 生成)
 * ============================================================ */
extern DAC_HandleTypeDef hdac1;
extern TIM_HandleTypeDef htim6;

/* ============================================================
 * 静态变量 — 每通道状态
 * ============================================================ */

static uint16_t buf1[DAC_BUF_SIZE];   /* PA4 波形表 */
static uint16_t buf2[DAC_BUF_SIZE];   /* PA5 波形表 */

static uint16_t  ratio1,  ratio2;     /* 当前每周期点数 */
static float     freq1,   freq2;      /* 当前频率 */
static float     amp1,    amp2;       /* 当前幅度系数 */
static float     phase1,  phase2;     /* 当前相位偏移(度) */
static WaveType  wave1,   wave2;      /* 当前波形类型 */

/* 叠加分量存储 */
static WaveComponent comps1[MAX_COMPONENTS], comps2[MAX_COMPONENTS];
static uint8_t       comp_count1, comp_count2;

static uint8_t   inited = 0;          /* 是否已初始化 */

/* ============================================================
 * wave_sample — 返回波形在相位 t (0~1) 处的值 [-1, +1]
 * ============================================================ */
static float wave_sample(WaveType type, float t, float phase_rad)
{
    float phase = 2.0f * PI * t + phase_rad;

    switch (type)
    {
    case WAVE_SINE:
        return arm_cos_f32(phase);

    case WAVE_TRIANGLE:
        if (t < 0.25f)       return 4.0f * t;
        else if (t < 0.75f)  return 2.0f - 4.0f * t;
        else                 return 4.0f * t - 4.0f;

    case WAVE_SQUARE:
        return (t < 0.5f) ? 1.0f : -1.0f;

    case WAVE_SAWTOOTH:
        return 2.0f * t - 1.0f;

    default:
        return arm_cos_f32(phase);
    }
}

/* ============================================================
 * gen_waveform — 填充波形表
 * ============================================================ */
static void gen_waveform(uint16_t *buf, uint16_t ratio,
                         float amp, float phase, WaveType type,
                         uint32_t channel)
{
    float phase_rad = phase * PI / 180.0f;

    for (uint16_t i = 0; i < ratio; i++)
    {
        float t = (float)i / (float)ratio;
        float raw;

        if (type == WAVE_COMPOSITE)
        {
            /* 分量叠加: 依次采样每个分量, 累加 */
            WaveComponent *comps;
            uint8_t count;
            if (channel == DAC_CHANNEL_1)
                { comps = comps1; count = comp_count1; }
            else
                { comps = comps2; count = comp_count2; }

            float sum = 0.0f;
            for (uint8_t c = 0; c < count; c++)
            {
                /* 按频率倍数独立计算相位 */
                float tc = fmodf(t * comps[c].freq_multiplier, 1.0f);
                sum += wave_sample(comps[c].type, tc, 0.0f) * comps[c].amplitude;
            }

            /* 归一化: 总幅度 = 各分量幅度之和 */
            float max_sum = 0.0f;
            for (uint8_t c = 0; c < count; c++)
                max_sum += comps[c].amplitude;
            raw = (max_sum > 0.001f) ? (sum / max_sum) : 0.0f;
        }
        else
        {
            raw = wave_sample(type, t, phase_rad);
        }

        /* 映射到 DAC 范围, 留安全边距 */
        float value = 2048.0f + 2047.0f * amp * raw;
        if (value > 4000.0f) value = 4000.0f;
        if (value < 95.0f)   value = 95.0f;
        buf[i] = (uint16_t)value;
    }
}

/* ============================================================
 * DAC_Wave_Init
 * ============================================================ */
void DAC_Wave_Init(void)
{
    DAC_Wave_Config(DAC_CHANNEL_1, 1000.0f, 0.8f, 0.0f, WAVE_SINE);
    DAC_Wave_Config(DAC_CHANNEL_2, 1000.0f, 0.8f, 0.0f, WAVE_SINE);

    HAL_TIM_Base_Start(&htim6);

    HAL_DAC_Start_DMA(&hdac1, DAC_CHANNEL_1,
                      (uint32_t *)buf1, ratio1, DAC_ALIGN_12B_R);
    HAL_DAC_Start_DMA(&hdac1, DAC_CHANNEL_2,
                      (uint32_t *)buf2, ratio2, DAC_ALIGN_12B_R);

    inited = 1;
}

/* ============================================================
 * DAC_Wave_Config
 * ============================================================ */
float DAC_Wave_Config(uint32_t channel, float freq_hz, float amplitude,
                      float phase_deg, WaveType wave)
{
    uint16_t ratio = (uint16_t)(DAC_UPDATE_RATE / freq_hz);
    if (ratio < 2)  ratio = 2;
    if (ratio > DAC_BUF_SIZE) ratio = DAC_BUF_SIZE;

    float actual_freq = DAC_UPDATE_RATE / (float)ratio;
    uint16_t *buf = (channel == DAC_CHANNEL_1) ? buf1 : buf2;

    gen_waveform(buf, ratio, amplitude, phase_deg, wave, channel);

    if (channel == DAC_CHANNEL_1)
    {
        ratio1 = ratio; freq1 = actual_freq;
        amp1 = amplitude; phase1 = phase_deg; wave1 = wave;
    }
    else
    {
        ratio2 = ratio; freq2 = actual_freq;
        amp2 = amplitude; phase2 = phase_deg; wave2 = wave;
    }

    return actual_freq;
}

/* ============================================================
 * Set 系列 (只改参数, 不生效)
 * ============================================================ */
float DAC_Wave_SetFreq(uint32_t channel, float freq_hz)
{
    uint16_t ratio = (uint16_t)(DAC_UPDATE_RATE / freq_hz);
    if (ratio < 2) ratio = 2;
    if (ratio > DAC_BUF_SIZE) ratio = DAC_BUF_SIZE;

    if (channel == DAC_CHANNEL_1)
        { ratio1 = ratio; freq1 = DAC_UPDATE_RATE / ratio; return freq1; }
    else
        { ratio2 = ratio; freq2 = DAC_UPDATE_RATE / ratio; return freq2; }
}

void DAC_Wave_SetAmplitude(uint32_t channel, float amplitude)
{
    if (channel == DAC_CHANNEL_1) amp1 = amplitude;
    else                          amp2 = amplitude;
}

void DAC_Wave_SetPhase(uint32_t channel, float phase_deg)
{
    if (channel == DAC_CHANNEL_1) phase1 = phase_deg;
    else                          phase2 = phase_deg;
}

void DAC_Wave_SetType(uint32_t channel, WaveType wave)
{
    if (channel == DAC_CHANNEL_1) wave1 = wave;
    else                          wave2 = wave;
}

/* ============================================================
 * DAC_Wave_SetComposite — 配置叠加波形
 * ============================================================ */
void DAC_Wave_SetComposite(uint32_t channel, float freq_hz,
                           WaveComponent *components, uint8_t count)
{
    if (count > MAX_COMPONENTS) count = MAX_COMPONENTS;

    uint16_t ratio = (uint16_t)(DAC_UPDATE_RATE / freq_hz);
    if (ratio < 2) ratio = 2;
    if (ratio > DAC_BUF_SIZE) ratio = DAC_BUF_SIZE;

    if (channel == DAC_CHANNEL_1)
    {
        ratio1 = ratio; freq1 = DAC_UPDATE_RATE / ratio;
        wave1 = WAVE_COMPOSITE;
        comp_count1 = count;
        for (uint8_t i = 0; i < count; i++) comps1[i] = components[i];
    }
    else
    {
        ratio2 = ratio; freq2 = DAC_UPDATE_RATE / ratio;
        wave2 = WAVE_COMPOSITE;
        comp_count2 = count;
        for (uint8_t i = 0; i < count; i++) comps2[i] = components[i];
    }

    /* 立即生成并生效 */
    uint16_t *buf = (channel == DAC_CHANNEL_1) ? buf1 : buf2;
    float amp = (channel == DAC_CHANNEL_1) ? amp1 : amp2;
    gen_waveform(buf, (channel == DAC_CHANNEL_1) ? ratio1 : ratio2,
                 amp, 0.0f, WAVE_COMPOSITE, channel);

    if (inited)
    {
        HAL_DAC_Stop_DMA(&hdac1, channel);
        HAL_DAC_Start_DMA(&hdac1, channel, (uint32_t *)buf,
                          (channel == DAC_CHANNEL_1) ? ratio1 : ratio2,
                          DAC_ALIGN_12B_R);
    }
}

/* ============================================================
 * DAC_Wave_Update — 应用参数修改
 * ============================================================ */
void DAC_Wave_Update(uint32_t channel)
{
    if (!inited) return;

    uint16_t *buf;
    uint16_t ratio;
    float amp, phase;
    WaveType wave;

    if (channel == DAC_CHANNEL_1)
    {
        buf = buf1; ratio = ratio1;
        amp = amp1; phase = phase1; wave = wave1;
    }
    else
    {
        buf = buf2; ratio = ratio2;
        amp = amp2; phase = phase2; wave = wave2;
    }

    gen_waveform(buf, ratio, amp, phase, wave, channel);

    HAL_DAC_Stop_DMA(&hdac1, channel);
    HAL_DAC_Start_DMA(&hdac1, channel, (uint32_t *)buf, ratio, DAC_ALIGN_12B_R);
}

/* ============================================================
 * 启停控制
 * ============================================================ */
void DAC_Wave_Stop(uint32_t channel)
{
    HAL_DAC_Stop_DMA(&hdac1, channel);
}

void DAC_Wave_Start(uint32_t channel)
{
    uint16_t *buf  = (channel == DAC_CHANNEL_1) ? buf1 : buf2;
    uint16_t ratio = (channel == DAC_CHANNEL_1) ? ratio1 : ratio2;
    HAL_DAC_Start_DMA(&hdac1, channel, (uint32_t *)buf, ratio, DAC_ALIGN_12B_R);
}
