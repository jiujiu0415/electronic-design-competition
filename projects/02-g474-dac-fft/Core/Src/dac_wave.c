/**
 * dac_wave.c — DAC 任意波形输出驱动实现 (STM32G474)
 *
 * 原理:
 *   TIM6 以 100kHz 触发 DAC DMA, DMA 循环输出波形表。
 *   波形表包含一个完整周期的数据, 点数 = DAC更新率 / 输出频率。
 *   改频率 = 改点数 + 重新生成波形表。
 */

#include "dac_wave.h"

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

static uint8_t   inited = 0;          /* 是否已初始化 */

/* ============================================================
 * 内部函数: 生成波形数据到缓冲区
 * ============================================================ */

/**
 * gen_waveform — 填充波形表
 *
 * @param buf      目标缓冲区
 * @param ratio    每周期点数
 * @param amp      幅度系数 (0~1)
 * @param phase    相位偏移 (度)
 * @param type     波形类型
 */
static void gen_waveform(uint16_t *buf, uint16_t ratio,
                         float amp, float phase, WaveType type)
{
    float phase_rad = phase * PI / 180.0f;

    for (uint16_t i = 0; i < ratio; i++)
    {
        float t = (float)i / (float)ratio;       /* 0.0 ~ 1.0 相位归一化 */
        float phase_now = 2.0f * PI * t + phase_rad;
        float raw = 0.0f;

        switch (type)
        {
        case WAVE_SINE:
            raw = arm_cos_f32(phase_now);         /* [-1, +1] */
            break;

        case WAVE_TRIANGLE:
            /* 三角波: 0→1→0→-1→0 */
            if (t < 0.25f)
                raw = 4.0f * t;                   /*  0 → +1 */
            else if (t < 0.75f)
                raw = 2.0f - 4.0f * t;            /* +1 → -1 */
            else
                raw = 4.0f * t - 4.0f;            /* -1 →  0 */
            break;

        case WAVE_SQUARE:
            raw = (t < 0.5f) ? 1.0f : -1.0f;
            break;

        case WAVE_SAWTOOTH:
            raw = 2.0f * t - 1.0f;                /* -1 → +1 */
            break;

        default:
            raw = arm_cos_f32(phase_now);
            break;
        }

        /* 映射到 DAC 范围 [0, 4095] */
        float value = 2048.0f + 2047.0f * amp * raw;
        if (value > 4095.0f) value = 4095.0f;
        if (value < 0.0f)    value = 0.0f;
        buf[i] = (uint16_t)value;
    }
}

/* ============================================================
 * API 实现
 * ============================================================ */

/**
 * DAC_Wave_Init — 初始化双通道, 默认输出 1kHz 正弦
 */
void DAC_Wave_Init(void)
{
    /* 默认配置 */
    DAC_Wave_Config(DAC_CHANNEL_1, 1000.0f, 0.8f, 0.0f, WAVE_SINE);
    DAC_Wave_Config(DAC_CHANNEL_2, 1000.0f, 0.8f, 0.0f, WAVE_SINE);

    /* 启动 TIM6 */
    HAL_TIM_Base_Start(&htim6);

    /* 启动双通道 DAC DMA */
    HAL_DAC_Start_DMA(&hdac1, DAC_CHANNEL_1,
                      (uint32_t *)buf1, ratio1, DAC_ALIGN_12B_R);
    HAL_DAC_Start_DMA(&hdac1, DAC_CHANNEL_2,
                      (uint32_t *)buf2, ratio2, DAC_ALIGN_12B_R);

    inited = 1;
}

/**
 * DAC_Wave_Config — 配置一个通道的全部参数并生成波形
 *
 * @return 实际输出频率
 */
float DAC_Wave_Config(uint32_t channel, float freq_hz, float amplitude,
                      float phase_deg, WaveType wave)
{
    /* 计算每周期点数 (必须为整数) */
    uint16_t ratio = (uint16_t)(DAC_UPDATE_RATE / freq_hz);
    if (ratio < 2)  ratio = 2;           /* 最少2个点 */
    if (ratio > DAC_BUF_SIZE) ratio = DAC_BUF_SIZE;

    float actual_freq = DAC_UPDATE_RATE / (float)ratio;

    /* 选择缓冲区 */
    uint16_t *buf = (channel == DAC_CHANNEL_1) ? buf1 : buf2;

    /* 生成波形 */
    gen_waveform(buf, ratio, amplitude, phase_deg, wave);

    /* 保存状态 */
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

/* ── 以下 4 个 Set 函数只修改参数，不立即生效 ── */

float DAC_Wave_SetFreq(uint32_t channel, float freq_hz)
{
    uint16_t ratio = (uint16_t)(DAC_UPDATE_RATE / freq_hz);
    if (ratio < 2)  ratio = 2;
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

/**
 * DAC_Wave_Update — 应用参数修改
 *
 * 流程: 停止 DMA → 重新生成波形 → 重启 DMA
 */
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

    /* 重新生成波形 */
    gen_waveform(buf, ratio, amp, phase, wave);

    /* 重启 DAC DMA */
    HAL_DAC_Stop_DMA(&hdac1, channel);
    HAL_DAC_Start_DMA(&hdac1, channel, (uint32_t *)buf, ratio, DAC_ALIGN_12B_R);
}

/* ── 启停控制 ── */

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
