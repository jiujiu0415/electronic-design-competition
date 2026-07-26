/**
 * dac_wave.h — DAC 任意波形输出驱动 (STM32G474)
 *
 * 功能: 通过 TIM6 + DMA 驱动 DAC1 双通道, 输出任意频率/幅度/相位的波形。
 *
 * 依赖:
 *   - CubeMX 配置好 DAC1_OUT1(PA4) + DAC1_OUT2(PA5)
 *   - TIM6 作为 DAC 触发源 (TRGO = Update Event)
 *   - DAC DMA: Peripheral = Word, Memory = Half Word, Circular
 *
 * 频率精度:
 *   输出频率 = DAC更新率 / 每周期点数
 *   例: TIM6=100kHz, 100点/周期 → 1kHz
 *   DAC_RATIO 必须为整数，实际频率会取最近似的整数值。
 */

#ifndef __DAC_WAVE_H
#define __DAC_WAVE_H

#include "stm32g4xx_hal.h"
#include "arm_math.h"

/* ============================================================
 * 宏定义
 * ============================================================ */

#define DAC_BUF_SIZE    2048
#define DAC_UPDATE_RATE 100000.0f  /* TIM6: 100kHz */

/* 波形类型 */
typedef enum {
    WAVE_SINE = 0,
    WAVE_TRIANGLE,
    WAVE_SQUARE,
    WAVE_SAWTOOTH,
    WAVE_COMPOSITE          /* 多分量叠加 */
} WaveType;

/* 最大叠加分量数 */
#define MAX_COMPONENTS 4

/* 叠加波形的一个分量 */
typedef struct {
    WaveType type;              /* 该分量的波形 */
    float    amplitude;         /* 该分量的幅度 (0~1) */
    float    freq_multiplier;   /* 频率倍数 (1=基频, 2=2倍频, 3=3倍频...) */
} WaveComponent;

/* ============================================================
 * API 函数
 * ============================================================ */

/**
 * DAC_Wave_Init — 初始化并启动双通道 DAC 输出
 *
 * 默认: 两路都为 1kHz 正弦, 幅度 0.8
 */
void DAC_Wave_Init(void);

/**
 * DAC_Wave_Config — 配置一个 DAC 通道的所有参数
 *
 * @param channel   DAC_CHANNEL_1 (PA4) 或 DAC_CHANNEL_2 (PA5)
 * @param freq_hz   目标频率 (Hz), 实际会取 DAC_UPDATE_RATE 能整除的最近值
 * @param amplitude 幅值系数 0.0~1.0 (1.0 = 满幅 Vpp≈3.3V)
 * @param phase_deg 相位偏移 (度), 0~360
 * @param wave      波形类型: WAVE_SINE / TRIANGLE / SQUARE / SAWTOOTH
 * @return          实际输出的频率 (Hz)
 */
float DAC_Wave_Config(uint32_t channel, float freq_hz, float amplitude,
                      float phase_deg, WaveType wave);

/**
 * DAC_Wave_SetFreq — 单独修改频率
 * @return 实际频率 (Hz)
 */
float DAC_Wave_SetFreq(uint32_t channel, float freq_hz);

/**
 * DAC_Wave_SetAmplitude — 单独修改幅度系数 (0.0 ~ 1.0)
 */
void  DAC_Wave_SetAmplitude(uint32_t channel, float amplitude);

/**
 * DAC_Wave_SetPhase — 单独修改相位偏移 (度)
 */
void  DAC_Wave_SetPhase(uint32_t channel, float phase_deg);

/**
 * DAC_Wave_SetType — 单独修改波形类型
 */
void  DAC_Wave_SetType(uint32_t channel, WaveType wave);

/**
 * DAC_Wave_Update — 应用参数修改，重新生成波形并重启 DMA
 *
 * 调用 SetFreq/SetAmplitude/SetPhase/SetType 后
 * 需要调用此函数才会生效。
 */
void  DAC_Wave_Update(uint32_t channel);

/**
 * DAC_Wave_SetComposite — 配置叠加波形 (多个分量相加)
 *
 * @param channel    DAC_CHANNEL_1 或 DAC_CHANNEL_2
 * @param freq_hz    基频 (Hz)
 * @param components 分量数组，每个分量指定 {波形, 幅度, 频率倍数}
 * @param count      分量个数 (1 ~ MAX_COMPONENTS)
 *
 * 示例: 三角波基频 + 3倍频正弦波叠加
 *   WaveComponent comps[] = {
 *       {WAVE_TRIANGLE, 0.6f, 1.0f},   // 基频三角波 60%
 *       {WAVE_SINE,     0.2f, 3.0f},   // 3倍频正弦波 20%
 *   };
 *   DAC_Wave_SetComposite(DAC_CHANNEL_1, 1000.0f, comps, 2);
 */
void  DAC_Wave_SetComposite(uint32_t channel, float freq_hz,
                            WaveComponent *components, uint8_t count);

/**
 * DAC_Wave_Stop — 停止某路 DAC
 */
void  DAC_Wave_Stop(uint32_t channel);

/**
 * DAC_Wave_Start — 启动（恢复）某路 DAC
 */
void  DAC_Wave_Start(uint32_t channel);

#endif /* __DAC_WAVE_H */
