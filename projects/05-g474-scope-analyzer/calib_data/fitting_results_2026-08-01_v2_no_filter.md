# 2026-08-01 校准拟合结果 — 滤波器移除后 (要求1/2)

## 变更说明

滤波器已从信号链路移除:
- **旧链路**: 信号发生器 → 加法器 → **滤波器** → AGC → 直流偏置 → ADC
- **新链路**: 信号发生器 → 加法器 → AGC → 直流偏置 → ADC

校准从两段 (AGC + H_chain) 合并为一段 (G_total)。

## 实测数据

- 信号发生器: 11 个 Vpp 设定 (50, 70, 90, 110, 130, 150, 170, 190, 210, 230, 250 mVpp)
- 频率: 7 个点 (10k, 50k, 100k, 200k, 300k, 400k, 500k Hz)
- 总计: 11 × 7 = 77 个数据点
- 数据文件: `calib_data/total_gain_calib_2026-08-01.csv`
- 原始Excel: `d:\29282\桌面\ADC输入.xlsx`, `d:\29282\桌面\检波器输出.xlsx`

## 模型选择

测试了 6 个模型，以 **V_original 最大还原误差** 为评判标准:

| 模型 | R² | Vpp_MAE% | Vpp_MaxE% |
|------|-----|----------|-----------|
| M1: a0+a1·Vd+a2·Vd² | 0.9908 | 3.00% | 11.02% |
| M2: +fn+fn² | 0.9961 | 2.95% | 18.41% |
| **M3: +fn+fn·Vd** | **0.9984** | **1.82%** | **4.60%** |
| M4: +fn·Vd only | 0.9944 | 3.36% | 12.50% |
| M5: multiplicative | 0.9981 | 2.13% | 6.52% |
| M6: full 2nd order | 0.9991 | 1.48% | 8.83% |

**选用 M3** — Vpp_MaxE 最小 (4.60%), 所有参数物理意义明确。

## 最终公式

```
G_total(Vd, f) = a0 + a1·Vd + a2·Vd² + a3·fn + a4·fn·Vd

其中:
  Vd  ∈ [522, 718] mV         检波器直流输出
  fn   = (f − 10000) / 490000  归一化频率 ∈ [0, 1]
  f   ∈ [10000, 500000] Hz    信号频率

系数:
  a0 =  568.94797956
  a1 =   -1.43691566
  a2 =    0.000919591597
  a3 =  -31.003815
  a4 =    0.04386305
```

## 拟合精度

| 指标 | 值 |
|------|-----|
| R² | 0.9984 |
| G_eff MAE | 0.43 (G单位) |
| G_eff MaxE | 2.11 |
| V_original MAE | 1.82% |
| **V_original MaxE** | **4.60%** |
| V_original RMSE | 3.97 mVpp |

## 物理含义

```
a0 + a1·Vd + a2·Vd²   → 基础 AGC 增益曲线 (Vd 控制增益)
a3·fn                  → 纯频率偏移 (检波器频响随频率变化)
a4·fn·Vd               → 频率×幅度交叉项 (检波器效率同时依赖 f 和 Vpp)
```

## C 实现

见 `Core/Src/scope_calib.c`:

```c
float ScopeAGC_ComputeGain(float vd_mV, float freq_hz)
{
    if (vd_mV < 522.0f) vd_mV = 522.0f;
    if (vd_mV > 718.0f) vd_mV = 718.0f;
    if (freq_hz < 10000.0f) freq_hz = 10000.0f;
    if (freq_hz > 500000.0f) freq_hz = 500000.0f;

    float fn = (freq_hz - 10000.0f) * (1.0f / 490000.0f);

    return 568.94797956f
         + vd_mV * (-1.43691566f + 0.000919591597f * vd_mV)
         + fn * (-31.003815f + 0.04386305f * vd_mV);
}
```

## 使用方式

```
V_original = V_adc_peak / ScopeAGC_ComputeGain(Vd, f)
φ_original = φ_measured − ScopeCalib_GetLPFPhase(f)
```

滤波器已移除，`ScopeCalib_GetHchain()` 恒返回 1.0。
