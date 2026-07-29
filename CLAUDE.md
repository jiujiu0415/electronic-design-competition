# CLAUDE.md — 电子设计大赛备赛工作台

## 背景

- 大一学生，备赛 2027 全国大学生电子设计大赛（仪表方向）
- GitHub: jiujiu0415/electronic-design-competition
- IDE: STM32CubeIDE (HAL 库)，调试器: ST-LINK/V3
- MCU: STM32F103C8T6 / STM32G474RET6 / MSPM0G3507

---

## 项目命名规范

```
projects/{序号}-{mcu}-{功能描述}/

序号: 两位数字（01, 02, 03...），按创建时间递增
mcu:  芯片简写，如 f103 / g474 / mspm0g3507
功能: 小写+连字符，3-4个词，突出核心外设/用途

每个项目至少包含:
  README.md         — 目标 + 硬件列表 + 引脚表 + 当前状态（✅/🔲）
  接线文档           — 杜邦线怎么连
  配置指南           — CubeMX/CubeIDE 怎么配
  驱动代码           — 头文件 + 实现
```

### 已有项目

| # | 目录 | 芯片 | 功能 | 状态 |
|---|------|------|------|------|
| 1 | `projects/01-f103-ad9833-signal-gen/` | STM32F103C8T6 | AD9833 信号源 | ✅ |
| 2 | `projects/02-g474-dac-fft/` | STM32G474RET6 | 内部 DAC 自闭环 FFT | 🔲 |
| 3 | `projects/03-g474-dual-ad9833-fft/` | STM32G474RET6 | 双 AD9833 + ADC DMA FFT | 🔲 |
| 4 | `projects/04-g474-lcr-sweep-fft/` | STM32G474RET6 | LCR 网络扫频分析仪 | 🟡 90% |
| 5 | `projects/05-g474-scope-analyzer/` | STM32G474RET6 | **电赛G题：周期信号测量分析装置** | 🔲 进行中 |

---

## 五项铁律（来自跨项目经验总结）

详见 `docs/项目经验总结.md`，这里只列口诀：

1. **SPI: 先画时序图 → 对照手册 → 再填 CPOL/CPHA，不要猜**
   - 口诀: CPOL=空闲电平, CPHA=第几个边沿采样
   - 例: AD9833 是 CPOL=High, CPHA=1 Edge (Mode 2)

2. **寄存器: 改一位用掩码 `(old & ~BIT) | new`，不写全新的值**
   - 测试所有状态组合，一种波形 pass 不代表全对

3. **烧录: SWD 能检测 ≠ 能烧录**
   - 排查链: 降时钟 → 改复位 → RAM for Algorithm → Mass Erase

4. **CubeIDE 陷阱:**
   - G4 Scan Mode 灰着 → 设 Number Of Conversion ≥2 自动启用
   - DMA 在 ADC 自己的 DMA Settings 标签添加，不是 System Core → DMA
   - 无外部晶振 → 用 HSI，PLL Source 别选 HSE

5. **项目管理:**
   - CubeMX/CubeIDE 每一步截图，电赛现场重配能省一半时间
   - 驱动和配置放同一个项目目录，不散落

---

## 常用技能（Claude Code Skills） — 共 41 个

> ⚠️ 完成电赛相关任务时，优先调用对应技能，不要裸写。技能即知识库，不调用 = 浪费。

### 嵌入式开发（16）
| 技能 | 用途 | 何时调用 |
|------|------|----------|
| `stm32-baremetal` | STM32 裸机开发全流程 | 新建 STM32 工程、HAL 库配置 |
| `spi-i2c-baremetal` | SPI/I2C 通信驱动 | SPI 时序、I2C 地址扫描 |
| `adc-dac-baremetal` | ADC 采样 / DAC 输出 | 模拟信号采集、波形生成 |
| `dma-baremetal` | DMA 数据传输 | 高速 ADC 采集、内存搬运 |
| `timers-pwm-baremetal` | 定时器与 PWM | 波形生成、频率/占空比控制 |
| `interrupts-baremetal` | 中断与 NVIC | 按键中断、定时器中断 |
| `gpio-baremetal` | GPIO 输入输出 | 引脚配置、LED/按键驱动 |
| `uart-serial-baremetal` | UART 串口通信 | 调试打印、模块通信 |
| `mmio-bit-manipulation` | 寄存器位操作 | 直接操作寄存器、掩码运算 |
| `baremetal-startup` | 启动文件与向量表 | .s 文件、VTOR、堆栈初始化 |
| `embedded-systems` | 嵌入式系统设计 | 系统架构、功耗分析 |
| `freertos` | FreeRTOS 内核 | 任务创建、调度、队列 |
| `freertos-patterns` | FreeRTOS 设计模式 | 任务间通信、信号量、互斥锁 |
| `i2c-diagnostician` | I2C 总线诊断 | I2C 通信故障排查 |
| `sensor-calibration` | 传感器标定 | ADC 值 → 物理量转换 |
| `power-budget-calculator` | 功耗预算计算 | 电池续航估算、电源设计 |

### 电路与 EDA（7）
| 技能 | 用途 | 何时调用 |
|------|------|----------|
| `eda-schematics` | 原理图设计 | 画电路图、选型 |
| `eda-pcb` | PCB 布局布线 | 画板子、走线 |
| `eda-drc` | 设计规则检查 | 投产前 DRC/ERC 验证 |
| `spice-simulation` | 电路仿真 | 运放、滤波电路仿真 |
| `circuit-debugger` | 电路故障诊断 | 焊接后板子不工作 |
| `kicad` | KiCad 工具链 | KiCad 原理图/PCB 操作 |
| `tscircuit` | TypeScript 电路设计 | 代码化电路设计 |

### 元器件与制造（5）
| 技能 | 用途 | 何时调用 |
|------|------|----------|
| `lcsc-parts` | 立创商城选料 | 查价格、封装、库存 |
| `jlcpcb` | 嘉立创打板 | 投板参数、拼版、SMT |
| `bom-generator` | BOM 清单生成 | 整理物料清单 |
| `bom-generator-kicad` | KiCad BOM 导出 | 从 KiCad 导出 BOM |
| `battery-selector` | 电池选型 | 便携设备供电方案 |

### 数据手册（3）
| 技能 | 用途 | 何时调用 |
|------|------|----------|
| `datasheet-interpreter` | 手册解读 | 看不懂参数、寄存器 |
| `datasheet-reading` | 手册查阅方法 | 如何高效读手册 |
| `datasheets-kicad` | KiCad 关联数据手册 | 链接手册到元件 |

### 信号处理与数学（3）
| 技能 | 用途 | 何时调用 |
|------|------|----------|
| `matlab-digital-filter` | 数字滤波器设计 | FIR/IIR 滤波器系数 |
| `matlab-agentic-toolkit` | MATLAB 工具链 | 数据处理、算法验证 |
| `math` | 数学推导与公式 | 误差分析、公式推导 |

### 文档与报告（6）⭐ 电赛关键
| 技能 | 用途 | 何时调用 |
|------|------|----------|
| `official-document-writer` | 中文公文/报告 | 设计报告正文撰写 |
| `academic-paper` | 学术论文全流程（12 agent） | 完整论文撰写、润色、审稿回复 |
| `academic-writer` | 顶会/期刊论文 + AIGC 降重 | 中文期刊论文（CCF/知网格式） |
| `latex-paper-conversion` | LaTeX 模板转换 | 不同期刊模板互转 |
| `ppt-creator` | PPT 演示文稿 | 答辩 PPT 制作 |
| `dataviz` | 数据可视化 | 测试曲线、误差棒图、频谱图 |

### 元工具（2）
| 技能 | 用途 | 何时调用 |
|------|------|----------|
| `find-skills` | 搜索安装新技能 | 发现缺工具 |
| `skill-creator` | 创建自定义技能 | 封装重复流程

---

## 参考资源

- `docs/SPI通信详解.md` — SPI 协议入门
- `docs/项目经验总结.md` — 完整经验教训
- `docs/AD835乘法器详解.md` — AD835 基础知识
- `references/` — 各芯片/模块的数据手册和原理图

---

## Git 约定

- Commit message 用中文，描述"做了什么"而不是"改了什么文件"
- 一个阶段完成 → 一次 commit
- 示例: `新增: AD9833 双模块驱动适配 STM32G474`
