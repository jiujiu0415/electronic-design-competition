/**
 * @file    scope_calib.c
 * @brief   信号链路校准实现 — 2026-08-01 拟合方程更新
 * @author  jiujiu0415
 * @date    2026-08-01
 *
 * 校准数据来源 (2026-08-01 重测):
 *   1. 信号发生器 → 加法器 → 滤波器 → 输出峰峰值 (加法器另一路短路)
 *      6 个输入电平 × 7 个频率 = 42 点, H_chain = Vout_filter / Vin_signal_gen
 *   2. AD603-AGC 增益 vs 检波器输出 Vd
 *      Vd ∈ [533, 722] mV, G ∈ [11.4, 63.1], 42 点
 */

#include "scope_calib.h"

/* ================================================================
 *  AGC 增益校准: G = f(Vd)
 *  二次多项式拟合, 42点实测 (2026-08-01 更新)
 *  R² = 0.99971, MAE = 0.244, MaxE = 0.523
 *  G 范围: [11.4, 63.1], Vd 范围: [533, 722] mV
 * ================================================================ */

float ScopeAGC_ComputeGain(float vd_mV)
{
    /* 钳位到标定范围 */
    if (vd_mV < 533.0f) vd_mV = 533.0f;
    if (vd_mV > 722.0f) vd_mV = 722.0f;

    return 570.75618007f
         + (-1.45473655f) * vd_mV
         + 0.00094137026f * vd_mV * vd_mV;
}

/* ================================================================
 *  加法器+滤波器链频响修正: H = spline(f)
 *  自然三次样条, 7节点, 归一化频率 fn = (f - 10k) / 490k ∈ [0, 1]
 *  42点实测 (2026-08-01 更新), 6输入电平 × 7频率
 *  H_chain 范围: [0.979, 1.058]
 *  std 范围: [0.0024, 0.0082] (6个输入电平间的标准差)
 * ================================================================ */

/* 样条节点 (归一化频率 fn = (f - 10k) / 490k) */
#define SPLINE_FMIN   10000.0f
#define SPLINE_FMAX  500000.0f
#define SPLINE_SCALE (1.0f / 490000.0f)   /* 1/(FMAX-FMIN) */

/* 归一化节点位置 */
#define FN0  0.00000000f   /*  10 kHz */
#define FN1  0.08163265f   /*  50 kHz */
#define FN2  0.18367347f   /* 100 kHz */
#define FN3  0.38775510f   /* 200 kHz */
#define FN4  0.59183673f   /* 300 kHz */
#define FN5  0.79591837f   /* 400 kHz */
/* FN6 = 1.0 (500 kHz) — 末段显式处理, 不需要宏 */

float ScopeCalib_GetHchain(float freq_hz)
{
    /* 钳位 */
    if (freq_hz <= SPLINE_FMIN) return 1.03522926f;
    if (freq_hz >= SPLINE_FMAX) return 0.97868544f;

    /* 归一化频率 */
    float fn = (freq_hz - SPLINE_FMIN) * SPLINE_SCALE;
    float dx;

    /* 段0: 10k-50kHz, fn ∈ [0, 0.08163265] — c2≈0 (natural边界) */
    if (fn <= FN1) {
        dx = fn - FN0;
        /* Horner: c0 + dx*(c1 + dx*(c2 + dx*c3)), c2=0 */
        return 1.03522926f + dx * (0.03561475f + dx * dx * 0.08133061f);
    }

    /* 段1: 50k-100kHz, fn ∈ [0.08163265, 0.18367347] */
    if (fn <= FN2) {
        dx = fn - FN1;
        return 1.03818083f + dx * (0.03724069f + dx * (0.01991770f + dx * 0.63001516f));
    }

    /* 段2: 100k-200kHz, fn ∈ [0.18367347, 0.38775510] */
    if (fn <= FN3) {
        dx = fn - FN2;
        return 1.04285767f + dx * (0.06098530f + dx * (0.21277948f + dx * -0.77313850f));
    }

    /* 段3: 200k-300kHz, fn ∈ [0.38775510, 0.59183673] */
    if (fn <= FN4) {
        dx = fn - FN3;
        return 1.05759420f + dx * (0.05123201f + dx * (-0.26057062f + dx * 0.07045357f));
    }

    /* 段4: 300k-400kHz, fn ∈ [0.59183673, 0.79591837] */
    if (fn <= FN5) {
        dx = fn - FN4;
        return 1.05779597f + dx * (-0.04632032f + dx * (-0.21743579f + dx * -0.52939615f));
    }

    /* 段5: 400k-500kHz, fn ∈ [0.79591837, 1.0] */
    {
        dx = fn - FN5;
        return 1.03478700f + dx * (-0.20121657f + dx * (-0.54155587f + dx * 0.88454125f));
    }
}

/* ================================================================
 *  LPF 相位校准: φ_LPF(f)
 *  巴特沃斯二阶有源低通滤波器 (Butterworth, Sallen-Key拓扑)
 *  运放 OPA2140AID 单位增益跟随器, 双电源
 *  R1=1.3kΩ, R2=1.8kΩ, C1=200pF, C2=100pF
 *  Q ≈ 0.698 (理论 Butterworth Q=1/√2≈0.707, 误差<2%)
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
