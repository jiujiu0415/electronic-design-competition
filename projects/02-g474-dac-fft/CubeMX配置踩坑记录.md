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

## 坑 #10：hdma_adc1 变量不存在 → 不要手改 it.c，用 HAL 回调

**现象**：在 stm32g4xx_it.c 中引用 `hdma_adc1`，编译报错 `'hdma_adc1' undeclared`。

adc.h 中 **只有** `extern ADC_HandleTypeDef hadc1;`，没有 `hdma_adc1`。CubeMX 不给 DMA 句柄单独暴露 extern。

**正确做法**：不在 it.c 里手写 DMA 中断代码，改用 HAL 的 ADC 转换完成回调：
```c
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)  // 写在 main.c 里
```

**教训**：CubeMX 生成的 DMA 句柄不一定有 extern 声明。优先用 HAL 回调而不是直接操作 DMA 寄存器。回调在 main.c 里写，不需要知道 DMA 句柄的名字。

---

## 坑 #11：以为用了回调就不需要 extern — 错！

**错误判断**：既然回调 `HAL_ADC_ConvCpltCallback` 写在 main.c 里，而 `adc_full_flag` 也在 main.c 的 PV 区，应该不需要 extern。

**但用户编译验证**：不添加 `extern volatile uint8_t adc_full_flag;` 到 main.h → 编译报错；添加后 → 通过。

**原因**：HAL 的 DMA 中断回调链路涉及多个 .c 文件（stm32g4xx_it.c → HAL DMA → HAL ADC → 回调），`adc_full_flag` 在链接阶段被 it.c 侧的代码路径间接引用。编译器的优化和链接行为不是纯按文件隔离的。

**教训**：
- **跨文件的全局变量永远要在头文件写 extern**，不要凭"看起来不需要"就删掉
- 用户说"编译报错需要这个"比我的推理更可信——编译器不会骗人
- 先让代码编译通过，再优化设计，不要反过来

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

---

## DSP 库配置方法（来源：STM32数字信号处理_v1.1.pdf 第9-10页）

> PDF 原教程基于 Keil MDK，以下是适配到 STM32CubeIDE 的方法。
> 用户已验证通过，编译无报错。

### CubeIDE 启用 CMSIS-DSP 步骤

**方法：CubeIDE 软件内配置**

1. 右键工程 → **Properties** → **C/C++ Build → Settings**
2. **MCU GCC Linker → Libraries**：
   - Libraries(-l) 添加：`arm_cortexM4lf_math`
   - Library search path(-L) 添加：指向 `Drivers/CMSIS/DSP/Lib/GCC/` 目录
3. **MCU GCC Compiler → Include paths**：添加 `../Drivers/CMSIS/DSP/Include`
4. **MCU GCC Compiler → Preprocessor → Defined symbols**：添加 `ARM_MATH_CM4`

### 关键来自 PDF 的提示

| 提示 | 来源 | 说明 |
|------|------|------|
| 优化等级设为 **O1** | PDF第10页 | O1平衡速度和安全。太高(O2/O3)可能导致DSP库函数被优化掉→HardFault；太低(O0)编译慢、内存不足 |
| 宏 `ARM_MATH_CM4` 必须定义 | PDF第10页 | DSP库通过这个宏选择Cortex-M4的代码路径 |
| 头文件 `arm_math.h` | PDF第12页 | `#include "arm_math.h"` 放在 USER CODE Includes 区 |

### 验证方法

```c
/* USER CODE BEGIN Includes */
#include "arm_math.h"
/* USER CODE END Includes */
```
Ctrl+B 编译 → 不报错 = DSP 库配置成功。
