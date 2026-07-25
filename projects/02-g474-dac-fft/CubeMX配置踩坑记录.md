# CubeMX 配置踩坑记录

> 每条记录包含：**实际 CubeMX 界面长什么样** vs **我写的指南写成了什么**，
> 以及为什么错了。以后写指南前先对照实际界面。

---

## 坑 #1：DAC Trigger 选项名

| 指南写的是 | 实际 CubeMX |
|-----------|------------|
| External Trigger: Timer 6 TRGO | **Trigger**: Timer 6 Trigger Out event |
| Trigger polarity: 上升沿触发 | ❌ 根本没有这个选项！ |

**教训**：G4 系列 DAC 在 CubeMX 里叫 `Trigger`，不是 `External Trigger`。也没有极性选项——硬件固定。

---

## 坑 #2：DAC Mode 选项名

| 指南写的是 | 实际 CubeMX |
|-----------|------------|
| Connected to external pin only | **Connected to external pin and to on chip-peripherals** |

**教训**：CubeMX 里没有 "external pin only" 这个选项，最小范围是 "external pin + on chip"。

---

## 坑 #3：DAC Wave Generation Mode 漏掉

**严重性**：⚠️ 致命

指南完全没提这个选项。如果默认值不是 `Disabled`，硬件波形发生器会覆盖 DMA 数据，DAC 根本不会输出 DMA 波形表里的数据。

**教训**：配 DAC+DMA 输出自定义波形时，`Wave Generation Mode` 必须确认 = `Disabled`。

---

## 坑 #4：DMA Increment Address 漏掉

指南最初没提 `Increment Address`，但这是 DMA 配置里最核心的概念：

| | Peripheral | Memory |
|---|-----------|--------|
| DAC DMA | ☐ 不递增（DAC寄存器固定） | ☑ 递增（波形数组） |
| ADC DMA | ☐ 不递增（ADC寄存器固定） | ☑ 递增（采集数组） |

**教训**：DMA 配置必须同时列出 Increment Address 两个复选框的状态，不能省略。

---

## 坑 #5：ADC Clock Prescaler 选项名

| 指南写的是 | 实际 CubeMX |
|-----------|------------|
| PLLP divided by 4 | **Synchronous clock mode divided by 4** |

**教训**：CubeMX 里 ADC 时钟分两套模式：
- **Synchronous clock mode** divided by N（同步模式，AHB时钟分频，N只有1/2/4可选）
- **Asynchronous clock mode** divided by N（异步模式，PLLP分频，选项很多）

用 TIM 触发 ADC 采样时选**同步模式**，避免跨时钟域抖动。选项格式是 `Synchronous clock mode divided by X`。

---

## 坑 #6：DAC DMA 方向写反

| 指南最初写的 | 正确值 |
|-------------|--------|
| Peripheral to Memory | **Memory to Peripheral**（内存→外设） |

**教训**：DAC 是输出，数据从内存流向 DAC；ADC 是输入，数据从 ADC 流向内存。方向相反。

---

## 坑 #7：DAC1 global interrupt 灰掉

用户看到 NVIC 里 DAC1 global interrupt 灰色不可选，以为是问题。

**实际情况**：DAC 用 DMA 循环模式，DMA 自己搬运数据不需要中断。CubeMX 自动灰掉——**灰着 = 对的**。

**教训**：灰色选项 = 当前模式下不可用/不需要，不是 bug。在指南里提前说明。

---

## 坑 #8：NVIC 灰色复选框的两种含义

| 状态 | 含义 | 举例 |
|------|------|------|
| ☑ 勾上 + 灰色 | **强制开启**，CubeMX 不让你关 | ADC DMA Channel 1 interrupt |
| ☐ 不勾 + 灰色 | **不可用**，当前模式不需要 | DAC1 global interrupt（DMA模式下） |

**教训**：不能看到灰色就说"不需要"。先看勾没勾上，再判断。☑灰色 = 已经生效，☐灰色 = 不需要。

---

## 坑 #9：TIM2/TIM6 顶部 Mode 区容易忽略

TIM 配置页分成两部分：
- **上方 Mode 区**：Clock Source（选 Internal Clock）+ Channel 模式
- **下方 Configuration 区**：PSC、ARR、TRGO 等参数

指南只写了下方参数，漏了上方 Mode 区：
- Clock Source 必须是 **Internal Clock**（默认就是，但需确认）
- Channel 1~4 全部 **Disable**（不需要 PWM/输入捕获，只要 TRGO 触发）

**教训**：CubeMX TIM 页面分上下两个区域，指南要分别说明。

---

## 通用教训

### 写 CubeMX 指南时的原则

1. **打开 CubeMX 对着实际界面写**，不要凭记忆或参考其他芯片的界面
2. **每个选项名和下拉框值都要一字不差**，因为 CubeMX 里差一个字就找不到
3. **分两张表**：
   - "需要改的" — 列出要动手改的项
   - "确认默认" — 列出可能踩坑的关键默认选项
4. **灰色选项提前解释**：为什么灰着、要不要管

### 平台差异

- STM32G4 ≠ STM32F1，CubeMX 界面选项名不同
- 同一芯片系列不同版本 CubeMX 界面也可能微调
- 截图是最好的文档——Clock、Pinout、每个外设的参数页
