/**
 * @file    scope_calib.h
 * @brief   信号链路校准 — AGC增益 + 加法器/滤波器频响修正
 * @author  jiujiu0415
 * @date    2026-07-31
 *
 * 包含两组校准:
 *   1. ScopeAGC_ComputeGain()   — 检波器直流 → AGC放大倍数 (二次拟合)
 *   2. ScopeCalib_GetHchain()   — 频率 → 加法器+滤波器链增益 (三次样条)
 */

#ifndef SCOPE_CALIB_H
#define SCOPE_CALIB_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── AGC 增益校准 ───────────────────────────────────────────── */

/**
 * @brief  从检波器直流电压计算 AGC 放大倍数
 * @param  vd_mV  检波器输出电压 (mV), 范围 [470, 660]
 * @return AGC 线性增益 G, 范围 [11, 61]
 *
 * 二次拟合: G = 474.947 - 1.31554*Vd + 0.0009279*Vd²
 * R² = 0.99935, MAE = 0.305 (42点实测)
 */
float ScopeAGC_ComputeGain(float vd_mV);

/* ── 加法器+滤波器 频响修正 ─────────────────────────────────── */

/**
 * @brief  获取加法器+滤波器链的频率响应修正系数
 * @param  freq_hz  信号频率 (Hz), 范围 [10k, 500k], 超范围钳位
 * @return H_chain 修正系数, 范围 [0.97, 1.06]
 *
 * 自然三次样条插值, 7节点, 归一化频率
 * 用途: V_original = V_measured / ScopeAGC_ComputeGain(Vd) / H_chain(f)
 */
float ScopeCalib_GetHchain(float freq_hz);

#ifdef __cplusplus
}
#endif

#endif /* SCOPE_CALIB_H */
