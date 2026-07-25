# 电子设计大赛备赛工作台

> 大一下学期 | 目标 2027 全国大学生电子设计大赛国赛 | 仪表方向
>
> GitHub: https://github.com/jiujiu0415/electronic-design-competition

---

## 仓库结构

```
electronic-design-competition/
├── README.md
├── .gitignore
│
├── projects/                         ← 各项目独立管理，序号按创建时间
│   ├── 01-f103-ad9833-signal-gen/   ← STM32F103 + AD9833 信号源
│   ├── 02-g474-dac-fft/             ← STM32G474 内部DAC自闭环FFT
│   └── 03-g474-dual-ad9833-fft/     ← STM32G474 双AD9833 + ADC DMA FFT
│
├── references/                       ← 硬件参考资料
│   ├── ad9833-module/               ← AD9833 V40 模块: 手册+原理图
│   ├── stm32f103c8t6/               ← STM32 Blue Pill 原理图
│   └── mspm0g3507/                  ← MSPM0G3507: 手册+实验指导书+原理图
│
└── docs/                             ← 通用学习文档
    ├── 项目经验总结.md               ← 跨项目共性教训（必读）
    ├── SPI通信详解.md               ← SPI 协议入门
    ├── AD835乘法器详解.md            ← AD835 模拟乘法器知识
    └── 备赛方法论.md                 ← 备赛策略与规划
```

> `tools/`、`skills-repos/`、`node_modules/` 不上传 GitHub

---

## 项目列表

| # | 项目 | MCU | 核心外设 | 状态 |
|---|------|-----|---------|------|
| 1 | [01-f103-ad9833-signal-gen](projects/01-f103-ad9833-signal-gen/) | STM32F103C8T6 | SPI + AD9833 DDS | ✅ 完成 |
| 2 | [02-g474-dac-fft](projects/02-g474-dac-fft/) | STM32G474RET6 | DAC + ADC + FFT | 🔲 待开始 |
| 3 | [03-g474-dual-ad9833-fft](projects/03-g474-dual-ad9833-fft/) | STM32G474RET6 | SPI×2 + ADC DMA + FFT | 🔲 进行中 |

---

## 开发环境

| 类别 | 工具 |
|------|------|
| MCU IDE | STM32CubeIDE (HAL 库) |
| FFT 分析 | CMSIS-DSP (arm_rfft_f32) |
| EDA | KiCad / 立创EDA |
| AI 助手 | Claude Code + 专项技能 |
| 调试器 | ST-LINK/V3 (板载) / CMSIS-DAP |
| 版本管理 | Git + GitHub (SSH 推送) |

---

## 命名规范（新项目遵守）

```
projects/{序号}-{mcu}-{功能描述}/

序号: 01, 02, 03... 按创建时间递增
mcu:  f103 / g474 / mspm0g3507 等简写
功能: 小写+连字符, 突出核心外设/用途, 3-4词

每个项目至少包含: README.md + 接线文档 + 配置指南
```

---

## 经验教训（详细 → [项目经验总结](docs/项目经验总结.md)）

1. **SPI 配置前先画时序图** — CPOL/CPHA 对照手册，不要凭感觉
2. **改寄存器位用掩码** — `(old & ~BIT) | new`，测试所有状态组合
3. **SWD 能检测 ≠ 能烧录** — Flash 写入需要降时钟/改复位/确认 RAM for Algorithm
4. **CubeIDE DMA 在外设标签页里加** — 不是 System Core → DMA

---

## Claude Code 技能速查（36个）

### 嵌入式开发（14个）
`stm32-baremetal` `gpio-baremetal` `timers-pwm-baremetal` `adc-dac-baremetal` `uart-serial-baremetal` `spi-i2c-baremetal` `dma-baremetal` `interrupts-baremetal` `mmio-bit-manipulation` `datasheet-reading` `baremetal-startup` `embedded-systems` `freertos` `freertos-patterns`

### 电路设计（8个）
`eda-schematics` `eda-pcb` `eda-drc` `circuit-debugger` `spice-simulation` `kicad` `tscircuit` `i2c-diagnostician`

### 元器件与采购（7个）
`lcsc-parts` `jlcpcb` `bom-generator` `bom-generator-kicad` `datasheets-kicad` `datasheet-interpreter` `battery-selector`

### 传感器与测量（2个）
`sensor-calibration` `power-budget-calculator`

### 信号处理（1个）
`matlab-digital-filter`

### 文档与辅助（4个）
`official-document-writer` `math` `find-skills` `skill-creator`
