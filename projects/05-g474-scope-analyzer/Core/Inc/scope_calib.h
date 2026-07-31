/**
 * @file    scope_calib.h
 * @brief   信号链路校准 — AGC增益 + 加法器/滤波器频响 + LPF相位
 * @author  jiujiu0415
 * @date    2026-08-01
 *
 * 包含三组校准:
 *   1. ScopeAGC_ComputeGain()   — 检波器直流 → AGC放大倍数 (二次拟合)
 *   2. ScopeCalib_GetHchain()   — 频率 → 加法器+滤波器链增益 (三次样条)
 *   3. ScopeCalib_GetLPFPhase() — 频率 → LPF相位偏移 (巴特沃斯电路模型, Sallen-Key拓扑)
 */

#ifndef SCOPE_CALIB_H
#define SCOPE_CALIB_H

#include <stdint.h>
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── AGC 增益校准 ───────────────────────────────────────────── */

/**
 * @brief  从检波器直流电压计算 AGC 放大倍数
 * @param  vd_mV  检波器输出电压 (mV), 范围 [533, 722]
 * @return AGC 线性增益 G, 范围 [11.4, 63.1]
 *
 * 二次拟合: G = 570.756 - 1.45474*Vd + 0.00094137*Vd²
 * R² = 0.99971, MAE = 0.244 (42点实测, 2026-08-01更新)
 */
float ScopeAGC_ComputeGain(float vd_mV);

/* ── 加法器+滤波器 频响修正 ─────────────────────────────────── */

/**
 * @brief  获取加法器+滤波器链的频率响应修正系数
 * @param  freq_hz  信号频率 (Hz), 范围 [10k, 500k], 超范围钳位
 * @return H_chain 修正系数, 范围 [0.979, 1.058]
 *
 * 自然三次样条插值, 7节点, 归一化频率 (2026-08-01更新)
 * 用途: V_original = V_measured / ScopeAGC_ComputeGain(Vd) / H_chain(f)
 */
float ScopeCalib_GetHchain(float freq_hz);

/* ── LPF 相位校准 (新增) ────────────────────────────────────── */

/**
 * @brief  巴特沃斯二阶有源低通滤波器相位偏移 φ_LPF(f) (Sallen-Key拓扑)
 * @param  freq_hz  信号频率 (Hz), 范围 0 ~ 2 MHz
 * @return 相位偏移 (弧度), 负值 (LPF 输出滞后于输入)
 *
 * 电路: OPA2140AID 单位增益跟随器, R1=1.3kΩ, R2=1.8kΩ,
 *       C1=200pF (反馈), C2=100pF (对地)
 *
 * 传递函数:
 *   H(s) = 1 / (1 + s·(R1+R2)·C2 + s²·R1·R2·C1·C2)
 * 相位:
 *   φ(f) = −atan2(2πf·(R1+R2)·C2,  1 − (2πf)²·R1·R2·C1·C2)
 *        = −atan2((f/f₀)/Q,         1 − (f/f₀)²)
 *
 * f₀ = 735.8 kHz (1/(2π√(R1·R2·C1·C2)))
 * Q  = 0.698
 *
 * 用途: φ_original[k] = φ_measured[k] − φ_LPF(k·f₁)
 *       (信号源谐波相位均为0, 修正后相位应为0)
 */
float ScopeCalib_GetLPFPhase(float freq_hz);

#ifdef __cplusplus
}
#endif

#endif /* SCOPE_CALIB_H */
