/**
 * @file    scope_calib.c
 * @brief   信号链路校准实现 — 2026-08-01 第二次重测更新
 * @author  jiujiu0415
 * @date    2026-08-01 (v2: 新数据 + Vref修正3.29V)
 *
 * 校准数据来源 (2026-08-01 第二次重测, 滤波器已移除):
 *   信号发生器 → 加法器 → AD603 AGC → 直流偏置 → ADC
 *   11 个输入电平 (50-250 mVpp) × 11 个频率 (10k-500k Hz) = 121 点
 *   拟合目标: G_total = V_adc_input / V_signal_gen,  输入变量: Vd, freq
 *   参考电压修正: 3.30V → 3.29V (实测)
 */

#include "scope_calib.h"

/* ================================================================
 *  AGC 总增益校准: G_total = f(Vd, freq)
 *
 *  模型 (M8, 121点最优 — 2阶Vd + 2阶fn + 全部交叉项):
 *    G_total = a0 + a1·Vd + a2·Vd² + a3·fn + a4·fn·Vd + a5·fn² + a6·fn²·Vd
 *    fn = (freq_hz − 10000) / 490000  ∈ [0, 1]
 *
 *  拟合精度 (121点实测, 11 Vpp × 11 freq):
 *    R² = 0.99941, G_MaxE = 1.14%
 *    V_original 还原: MAE = 1.15%, MaxE = 5.21% (@250mVpp/10kHz边界)
 *
 *  G 范围: [12.1, 60.6]  (Vd ∈ [549, 743] mV, f ∈ [10k, 500k] Hz)
 *  参考电压: 3.29V (实测)
 *  fn ∈ [0, 1] 归一化保证 float32 下系数为 O(1), 无精度损失
 *
 *  物理含义:
 *    a0+a1·Vd+a2·Vd²      — 基础增益曲线 (Vd 控制 AGC, 二次型)
 *    a3·fn+a5·fn²          — 纯频率响应 (检波器频率特性)
 *    a4·fn·Vd+a6·fn²·Vd   — 频率×幅度交叉项 (检波器效率同时依赖f和Vpp)
 * ================================================================ */

float ScopeAGC_ComputeGain(float vd_mV, float freq_hz)
{
    /* 钳位到标定范围 */
    if (vd_mV < 549.0f) vd_mV = 549.0f;
    if (vd_mV > 743.0f) vd_mV = 743.0f;
    if (freq_hz < 10000.0f) freq_hz = 10000.0f;
    if (freq_hz > 500000.0f) freq_hz = 500000.0f;

    /* 归一化频率 fn = (f − 10k) / 490k ∈ [0, 1] */
    float fn = (freq_hz - 10000.0f) * (1.0f / 490000.0f);

    /* Horner 形式:
     * a0 + Vd*(a1 + a2*Vd) + fn*(a3 + a4*Vd + fn*(a5 + a6*Vd)) */
    return 610.54968794f
         + vd_mV * (-1.50081549f + 0.000934667694f * vd_mV)
         + fn * (4.70191333f - 0.00456630205f * vd_mV
               + fn * (-36.91316507f + 0.04859799434f * vd_mV));
}

/* ================================================================
 *  滤波器频响修正 — 当前已旁路 (要求1/2 无滤波器)
 *
 *  滤波器移除前:
 *    H_chain(f) = 三次样条插值, 7节点 (10k-500kHz)
 *    H ∈ [0.979, 1.058], 带通特性, 中心 ~250kHz
 *    R² = 1.000 (样条精确过节点)
 *
 *  恢复滤波器时: 从 git 历史 d1713fc 还原完整样条实现
 * ================================================================ */

float ScopeCalib_GetHchain(float freq_hz)
{
    (void)freq_hz;
    return 1.0f;  /* 滤波器已移除 — 无频响修正 */
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
