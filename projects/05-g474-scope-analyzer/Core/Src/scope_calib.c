/**
 * @file    scope_calib.c
 * @brief   信号链路校准实现
 * @author  jiujiu0415
 * @date    2026-07-31
 */

#include "scope_calib.h"

/* ================================================================
 *  AGC 增益校准: G = f(Vd)
 *  二次多项式拟合, 42点实测
 *  R² = 0.99935, MAE = 0.305
 * ================================================================ */

float ScopeAGC_ComputeGain(float vd_mV)
{
    /* 钳位到标定范围 */
    if (vd_mV < 470.0f) vd_mV = 470.0f;
    if (vd_mV > 660.0f) vd_mV = 660.0f;

    return 474.947464f
         + (-1.315542f) * vd_mV
         + 0.0009279000f * vd_mV * vd_mV;
}

/* ================================================================
 *  加法器+滤波器链频响修正: H = spline(f)
 *  自然三次样条, 7节点, 归一化频率
 *  留一法交叉验证: MAE=0.0115
 *  H_chain 范围: [0.970, 1.057]
 * ================================================================ */

/* 样条节点 (归一化频率 fn = (f - 10k) / 490k) */
#define SPLINE_FMIN   10000.0f
#define SPLINE_FMAX  500000.0f
#define SPLINE_SCALE (1.0f / 490000.0f)   /* 1/(FMAX-FMIN) */

/* 归一化节点位置 */
#define FN0  0.00000000f
#define FN1  0.08163265f
#define FN2  0.18367347f
#define FN3  0.38775510f
#define FN4  0.59183673f
#define FN5  0.79591837f
/* FN6 = 1.0 (no need for macro, last segment handled explicitly) */

float ScopeCalib_GetHchain(float freq_hz)
{
    /* 钳位 */
    if (freq_hz <= SPLINE_FMIN) return 1.0360289f;
    if (freq_hz >= SPLINE_FMAX) return 0.9702421f;

    /* 归一化频率 */
    float fn = (freq_hz - SPLINE_FMIN) * SPLINE_SCALE;
    float dx, dx2;

    /* 段0: 10k-50kHz, fn ∈ [0, 0.0816] */
    if (fn <= FN1) {
        dx = fn - FN0;
        dx2 = dx * dx;
        return 1.0360289f + dx * (-0.0004647f + dx2 * 1.9690003f);
    }

    /* 段1: 50k-100kHz, fn ∈ [0.0816, 0.1837] */
    if (fn <= FN2) {
        dx = fn - FN1;
        dx2 = dx * dx;
        return 1.0370621f + dx * (0.0388989f + dx * (0.4822041f + dx * -1.7546260f));
    }

    /* 段2: 100k-200kHz, fn ∈ [0.1837, 0.3878] */
    if (fn <= FN3) {
        dx = fn - FN2;
        dx2 = dx * dx;
        return 1.0441879f + dx * (0.0824987f + dx * (-0.0549263f + dx * -0.2010046f));
    }

    /* 段3: 200k-300kHz, fn ∈ [0.3878, 0.5918] */
    if (fn <= FN4) {
        dx = fn - FN3;
        dx2 = dx * dx;
        return 1.0570283f + dx * (0.0349647f + dx * (-0.1779903f + dx * -0.0305553f));
    }

    /* 段4: 300k-400kHz, fn ∈ [0.5918, 0.7959] */
    if (fn <= FN5) {
        dx = fn - FN4;
        dx2 = dx * dx;
        return 1.0564910f + dx * (-0.0415022f + dx * (-0.1966977f + dx * -0.7700261f));
    }

    /* 段5: 400k-500kHz, fn ∈ [0.7959, 1.0] */
    {
        dx = fn - FN5;
        dx2 = dx * dx;
        return 1.0332837f + dx * (-0.2180002f + dx * (-0.6681422f + dx * 1.0912990f));
    }
}

/* ================================================================
 *  LPF 相位校准: φ_LPF(f)
 *  萨伦-基二阶有源低通滤波器 (Sallen-Key)
 *  运放 OPA2140AID 单位增益跟随器, 双电源
 *  R1=1.3kΩ, R2=1.8kΩ, C1=200pF, C2=100pF
 *
 *  传递函数:
 *    H(s) = ω₀² / (s² + s·ω₀/Q + ω₀²)
 *    H(jω) = 1 / (1 − ω²·R1·R2·C1·C2 + jω·(R1+R2)·C2)
 *
 *  相位 (从电路模型直接推导):
 *    φ(f) = −atan2(ω·(R1+R2)·C2,  1 − ω²·R1·R2·C1·C2)
 *         = −atan2((f/f₀)/Q,       1 − (f/f₀)²)
 *
 *  其中:
 *    ω  = 2π·f
 *    f₀ = 1/(2π√(R1·R2·C1·C2)) = 735.8 kHz
 *    Q  = √(R1·R2·C1·C2)/((R1+R2)·C2) = 0.698
 *
 *  信号源约束: 基波+谐波相位恒为0
 *    → φ_original[k] = φ_measured[k] − φ_LPF(k·f₁) = 0
 *    → 实测φ_measured应等于预测φ_LPF, 可验证电路模型精度
 * ================================================================ */

/* Sallen-Key 元件参数 (来自原理图 SCH_Schematic2_1-P1_2026-07-31) */
#define LPF_R1   1300.0f           /* Ω */
#define LPF_R2   1800.0f           /* Ω */
#define LPF_C1   200e-12f          /* F  — 反馈电容 (R1-R2结点→输出) */
#define LPF_C2   100e-12f          /* F  — 对地电容 (同相端→GND)   */

/* 由元件参数导出的特征量 */
#define LPF_F0   735789.0f         /* Hz — 自然频率 ω₀/(2π) */
#define LPF_Q    0.6978f           /*    — 品质因数 */

/* 分子系数: ω·(R1+R2)·C2 = 2π·f·3100·100pF = 1.9478×10⁻⁶·f
 * 优化: 直接用 (f/f₀)/Q = f / (f₀·Q) = f / 513407 */
#define LPF_F0Q  513407.0f         /* Hz — f₀ × Q */

float ScopeCalib_GetLPFPhase(float freq_hz)
{
    float ratio   = freq_hz / LPF_F0;          /* f/f₀        */
    float num     = freq_hz / LPF_F0Q;         /* (f/f₀)/Q    = ω·(R1+R2)·C2 */
    float den     = 1.0f - ratio * ratio;      /* 1−(f/f₀)²   = 1−ω²·R1·R2·C1·C2 */

    /* atan2f(num, den) 自动处理象限:
     *   f < f₀: den > 0, 相位 ∈ (0°, −90°)
     *   f = f₀: den = 0, 相位 = −90°
     *   f > f₀: den < 0, 相位 ∈ (−90°, −180°) — atan2f 自动加 π
     */
    return -atan2f(num, den);
}
