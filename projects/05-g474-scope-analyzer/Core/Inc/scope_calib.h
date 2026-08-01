/**
 * @file    scope_calib.h
 * @brief   信号链路校准 — AGC总增益(含频率修正) + 滤波器频响(已旁路) + LPF相位
 * @author  jiujiu0415
 * @date    2026-08-01 (v2: 新数据121点 + Vref修正3.29V)
 *
 * 信号链路 (要求1/2, 滤波器已移除):
 *   信号发生器 → 加法器 → AD603 AGC → 直流偏置 → ADC
 *
 * 校准函数:
 *   1. ScopeAGC_ComputeGain(vd_mV, freq_hz) — 检波器直流 + 频率 → AGC总增益
 *   2. ScopeCalib_GetHchain(freq_hz)        — 滤波器频响 (已旁路, 返回1.0)
 *   3. ScopeCalib_GetLPFPhase(freq_hz)      — LPF相位偏移 (Sallen-Key电路模型)
 *
 * 计算链:
 *   V_orig = V_adc_peak / ScopeAGC_ComputeGain(Vd, f)
 *   φ_orig = φ_measured − ScopeCalib_GetLPFPhase(f)
 */

#ifndef SCOPE_CALIB_H
#define SCOPE_CALIB_H

#include <stdint.h>
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── AGC 总增益校准 (含频率修正) ──────────────────────────────── */

/**
 * @brief  从检波器直流电压 + 信号频率计算 AGC 总增益
 * @param  vd_mV    检波器输出电压 (mV), 范围 [549, 743]
 * @param  freq_hz  信号频率 (Hz), 范围 [10k, 500k], 超范围钳位
 * @return AGC 线性增益 G_total (含加法器+AGC+偏置电路, 从信号源到ADC输入)
 *
 * 拟合模型 (M8, 2026-08-01 v2 重测, 121点, Vref=3.29V):
 *   G_total = a0 + a1·Vd + a2·Vd² + a3·fn + a4·fn·Vd + a5·fn² + a6·fn²·Vd
 *   fn = (freq_hz − 10000) / 490000  ∈ [0, 1]
 *
 * 121点实测 (11输入电平 × 11频率, 10k-500kHz):
 *   R² = 0.99941, MAE(G) = 1.14%, MaxE(G) = 5.21%
 *   V_original 还原: MAE = 1.15%, MaxE = 5.21%
 *
 * Vd 由 ADC3 独立采集 (PA1), 与 ADC1 同步触发
 */
float ScopeAGC_ComputeGain(float vd_mV, float freq_hz);

/* ── 滤波器频响修正 (已旁路) ─────────────────────────────────── */

/**
 * @brief  滤波器链频响修正 — 当前已旁路 (要求1/2 无滤波器)
 * @param  freq_hz  信号频率 (Hz)
 * @return 恒为 1.0
 *
 * 注: 滤波器移除前使用三次样条插值 (7节点, 10k-500kHz, H∈[0.979,1.058])
 *     如需恢复滤波器, 将旧实现从 git 历史 (d1713fc) 还原
 */
float ScopeCalib_GetHchain(float freq_hz);

/* ── LPF 相位校准 ────────────────────────────────────────────── */

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
