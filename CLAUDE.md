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

## 常用技能（Claude Code Skills）

嵌入式: `stm32-baremetal` `spi-i2c-baremetal` `adc-dac-baremetal` `dma-baremetal` `timers-pwm-baremetal` `interrupts-baremetal` `embedded-systems`
电路: `circuit-debugger` `eda-schematics` `eda-pcb` `spice-simulation`
元器件: `lcsc-parts` `jlcpcb` `datasheet-interpreter`
信号处理: `matlab-digital-filter`
通用: `math` `official-document-writer`

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
