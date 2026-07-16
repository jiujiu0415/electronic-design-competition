# 电子设计大赛备赛工作台

> 大一下学期 | 目标 2027 全国大学生电子设计大赛国赛 | 仪表方向
> 
> GitHub: https://github.com/jiujiu0415/electronic-design-competition

---

## 仓库结构

```
electronic-design-competition/        ← GitHub 远程仓库
├── README.md                         ← 本文件
├── .gitignore
│
├── projects/                         ← 各个项目独立管理
│   └── ad9833-signal-gen/           ← 项目1: AD9833 信号源
│       ├── ad9833.h                  #   驱动头文件
│       ├── ad9833.c                  #   驱动实现
│       ├── main.c.reference          #   main.c 修改指南
│       ├── hardware_connection.md    #   杜邦线连接
│       ├── 开发全流程总结.md          #   完整开发记录
│       └── 经验总结-犯错误记录.md      #   bug复盘
│
├── references/                       ← 硬件参考资料（数据手册+原理图）
│   ├── ad9833-module/               #   AD9833 V40 模块
│   │   ├── schematic.png
│   │   └── datasheet/
│   └── stm32f103c8t6/               #   STM32 Blue Pill 核心板
│       └── schematic.png
│
└── docs/                             ← 通用学习文档
    ├── 备赛方法论.md                  #   备赛策略与规划
    └── SPI通信详解.md                #   SPI 协议入门
```

> **本地有但不上传 GitHub 的**：`tools/`（PDF渲染器、技能源码）、`skills-repos/`（各技能仓库克隆）、`node_modules/`

---

## 项目列表

| # | 项目 | 状态 | 说明 |
|---|------|------|------|
| 1 | [AD9833 信号源](projects/ad9833-signal-gen/) | ✅ 完成 | STM32F103C8T6 + AD9833，正弦/三角/方波，串口控制 |

---

## 环境

| 类别 | 工具 |
|------|------|
| MCU 平台 | STM32F103C8T6 (Blue Pill) |
| IDE | STM32CubeIDE (HAL库) |
| EDA | KiCad / 立创EDA |
| AI 助手 | Claude Code + 36个专项技能 |
| 烧录 | ST-Link / 串口 |

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

---

## 经验教训（来自 AD9833 项目）

1. **配 SPI 前先画时序图**：CPOL/CPHA 对照手册，不要凭感觉选
2. **改寄存器位用掩码**：`(old & ~BIT) | new`，不要写"看起来差不多"的新值
3. **测试所有状态组合**：一种波形对了不代表全对，正弦能用是巧合
