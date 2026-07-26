/**
 * adc_fft.h — ADC采集 + FFT频谱分析 (STM32G474)
 */
#ifndef __ADC_FFT_H
#define __ADC_FFT_H

#include "stm32g4xx_hal.h"
#include "arm_math.h"

/* ============================================================
 * 宏定义
 * ============================================================ */
#define ADC_BUF_SIZE    2048
#define FFT_SIZE        2048
#define MAX_PEAKS       8

/* ============================================================
 * 数据结构
 * ============================================================ */
typedef struct {
    float freq_hz;
    float amplitude;
    float phase_deg;
} FFT_Peak;

typedef struct {
    FFT_Peak peaks[MAX_PEAKS];
    uint8_t  peak_count;
    float    dc_offset_v;
    float    freq_zc;
    char     waveform[16];
} FFT_Result;

/* ============================================================
 * API
 * ============================================================ */
void       ADC_FFT_Init(void);
uint8_t    ADC_FFT_DataReady(void);
FFT_Result ADC_FFT_Analyze(void);
float      ADC_FFT_GetSampleRate(void);

#endif
