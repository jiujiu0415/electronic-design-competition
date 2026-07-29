# CubeMX/CubeIDE 配置指南 — 05-g474-scope-analyzer

> STM32G474RET6（裸芯片）| HSI → PLL → 170MHz
> ADC 高速单通道 | TIM2 触发 @2MSPS | DMA Circular | 4096 FFT

---

## ═══ 第1步：新建 STM32CubeIDE 工程 ═══

1. 打开 STM32CubeIDE
2. File → New → STM32 Project
3. MCU/MPU Selector 搜索 `STM32G474RET6`
4. 选中 → Next
5. Project Name: `05-g474-scope-analyzer`
6. 保存位置: `D:\CURSOR\Electronic Design\projects\`
7. Targeted Language: **C**
8. Finish
9. ⚠️ 弹窗 "Initialize all IPs with their default mode?" → **Yes**

---

## ═══ 第2步：调试接口 + 系统配置 ═══

### 2.1 调试接口

> ⚠️ 裸芯片必须手动开 SWD，否则 PA13/PA14 被复用为普通 GPIO → 无法烧录！

Pinout & Configuration → System Core → **SYS**:
| 参数 | 值 |
|------|-----|
| Debug | **Serial Wire** |

### 2.2 RCC 时钟源

Pinout & Configuration → System Core → **RCC**:
| 参数 | 值 |
|------|-----|
| High Speed Clock (HSE) | **Disable** (无外部晶振) |
| High Speed Clock (HSI) | **开启** (内部 16MHz RC) |

---

## ═══ 第3步：时钟树配置 (Clock Configuration) ═══

切换到 **Clock Configuration** 标签页，按下面填：

```
HSI (16MHz)
  ↓
PLL Source Mux = HSI
  ↓
/PLLM = /4  →  4MHz
  ↓
×PLLN = ×85  →  340MHz VCO ✅ (96~344MHz)
  ↓
/PLLR = /2  →  170MHz SYSCLK
  ↓
SYSCLK Mux = PLLRCLK
```

| 参数 | 值 |
|------|-----|
| PLL Source Mux | **HSI** |
| PLLM | **/4** |
| PLLN | **×85** |
| PLLR | **/2** |
| SYSCLK Mux | **PLLRCLK** (即 PLLR) |
| **→ SYSCLK 应为 170 MHz** | |
| AHB Prescaler | **/1** → 170 MHz |
| APB1 Prescaler | **/1** → 170 MHz |
| APB2 Prescaler | **/1** → 170 MHz |

> ⚠️ APB1=APB2=/1，TIM2 在 APB1 上，时钟 = 170MHz。这对后续采样率计算至关重要。

---

## ═══ 第4步：ADC1 配置（信号采样，PA0）═══

> ADC1 负责采样主信号（经 AGC + 1.6V 偏置后的 0~3.2V 单极性波形）。
> 单通道，TIM2 TRGO 触发，2MSPS，DMA Circular。

### 4.1 引脚模式 ⚠️ G4 特有，必须先做

Pinout & Configuration → Analog → **ADC1**:
- 左侧通道列表 → 找到 **IN1 (PA0)** → 改为 **IN1 Single-ended**

> 不设 Single-ended，后面 Rank 的 Channel 下拉列表找不到 IN1。

### 4.2 ADC1 参数设置

ADC1 → **Parameter Settings**:

| 参数 | 值 | 说明 |
|------|-----|------|
| Clock Prescaler | **Synchronous clock mode divided by 4** | 170/4=42.5MHz |
| Resolution | **12 bits (15 ADC clock cycles)** | |
| Data Alignment | **Right alignment** | |
| Scan Conversion Mode | **Disable** | 单通道 |
| Continuous Conversion Mode | **Disable** | TIM2 触发 |
| Discontinuous Conversion Mode | Disable | |
| DMA Continuous Requests | **Enable** | |
| End Of Conversion Selection | **End of single conversion** | |
| Overrun behaviour | **Overrun data overwritten** | |
| Low Power Auto Wait | Disable | |

### 4.3 ADC1 触发源

| 参数 | 值 |
|------|-----|
| External Trigger Conversion Source | **Timer 2 Trigger Out event** |
| External Trigger Conversion Edge | **Trigger detection on the rising edge** |

### 4.4 ADC1 Rank 配置

Number Of Conversion = **1**:

| Rank | Channel | Sampling Time |
|------|---------|---------------|
| Rank 1 | **Channel 1 (PA0)** | **2.5 cycles** |

> 单次转换: 2.5+12.5=15 cycles ÷ 42.5MHz ≈ 0.35μs < 0.5μs 触发间隔 ✅

### 4.5 ADC1 DMA Settings

ADC1 → **DMA Settings** 标签 → **Add**:

| 参数 | 值 |
|------|-----|
| DMA Request | **ADC1** |
| Channel | **DMA1 Channel 1** (CubeMX 自动分配) |
| Direction | **Peripheral To Memory** |
| Mode | **Circular** |
| Data Width (Peripheral) | **Half Word** |
| Data Width (Memory) | **Half Word** |
| Priority | **High** |

---

## ═══ 第5步：ADC2 配置（检波器采样，PA1）═══

> ADC2 负责采样检波器输出（直流电压，= 总信号 Vpp）。
> **和 ADC1 同一套参数**，单通道，同一 TIM2 TRGO 触发，2MSPS，独立 DMA。

### 5.1 引脚模式

Pinout & Configuration → Analog → **ADC2**:
- 左侧通道列表 → 找到 **IN2 (PA1)** → 改为 **IN2 Single-ended**

> PA1 在 G474 上只能设为 ADC2_IN2，不能设为 ADC2_IN1。

### 5.2 ADC2 参数设置

ADC2 → **Parameter Settings**：**全部和 ADC1 一样**。

| 参数 | 值 |
|------|-----|
| Clock Prescaler | Synchronous clock mode divided by 4 |
| Resolution | 12 bits |
| Scan Conversion Mode | **Disable** |
| Continuous Conversion Mode | **Disable** |
| DMA Continuous Requests | **Enable** |
| End Of Conversion Selection | End of single conversion |
| Overrun behaviour | Overrun data overwritten |

### 5.3 ADC2 触发源

| 参数 | 值 |
|------|-----|
| External Trigger Conversion Source | **Timer 2 Trigger Out event** |
| External Trigger Conversion Edge | Trigger detection on the rising edge |

### 5.4 ADC2 Rank 配置

Number Of Conversion = **1**:

| Rank | Channel | Sampling Time |
|------|---------|---------------|
| Rank 1 | **Channel 2 (PA1)** | **2.5 cycles** |

### 5.5 ADC2 DMA Settings

ADC2 → **DMA Settings** 标签 → **Add**:

| 参数 | 值 |
|------|-----|
| DMA Request | **ADC2** |
| Channel | **DMA1 Channel 2** (CubeMX 自动分配) |
| Direction | **Peripheral To Memory** |
| Mode | **Circular** |
| Data Width (Peripheral) | **Half Word** |
| Data Width (Memory) | **Half Word** |
| Priority | **High** |

> ⚠️ 铁律: DMA 在每个 ADC 自己的 DMA Settings 标签里加，**不是** System Core → DMA。

---

## ═══ 第6步：TIM2 配置（双 ADC 同步触发）═══

Pinout & Configuration → Timers → **TIM2**:

### 6.1 Mode 区

| 参数 | 值 |
|------|-----|
| Clock Source | **Internal Clock** |
| Channel1~4 | 全部 **Disable** |

### 6.2 Configuration 区

| 参数 | 值 | 计算 |
|------|-----|------|
| Prescaler (PSC) | **0** | TIM2 时钟 = 170MHz |
| Counter Mode | **Up** | |
| Counter Period (ARR) | **84** | Fs = 170M/85 = **2.0 MHz** |
| Auto-reload preload | **Enable** | |
| **Trigger Output (TRGO)** | **Update Event** | 每次溢出同时触发 ADC1 和 ADC2 |

> **为什么能跑 2MSPS**：两个独立 ADC 各自只采一个通道，转换时间 0.35μs < 触发间隔 0.5μs。
> TIM2 TRGO 同时触发两个 ADC，硬件并行转换，互不阻塞。

---

## ═══ 第7步：USART2 配置（串口打印）═══

Pinout & Configuration → Connectivity → **USART2**:

| 参数 | 值 |
|------|-----|
| Mode | **Asynchronous** |
| Baud Rate | **115200** Bits/s (后续可提高到 921600) |
| Word Length | 8 Bits |
| Parity | None |
| Stop Bits | 1 |

引脚:
| 信号 | 引脚 |
|------|------|
| USART2_TX | **PA2** |
| USART2_RX | **PA3** (可选，只发不收可以不接) |

---

## ═══ 第8步：NVIC 优先级 ═══

Pinout & Configuration → System Core → **NVIC**:

不需要手动调整。DMA 和 USART 中断使用默认优先级即可。

> ⚠️ DMA 中断在 CubeMX 里显示为 ☑灰色（强制开启），这是对的，不用管。

---

## ═══ 第9步：CMSIS-DSP 库启用 ⚠️ 必须！FFT 依赖！═══

> ⚠️ 这是最容易漏掉的一步。不加 DSP 库 = FFT 代码编译报错，满屏 undefined reference。

### 方法一：CubeMX 界面直接勾选（推荐）

Pinout & Configuration → **Software Packs** → **Select Components**:
- 找到 **CMSIS Pack** → 展开
- 勾选 **CMSIS DSP Library**
- CubeMX 会自动配置 include 路径和链接库

如果 Software Packs 里找不到，用方法二。

### 方法二：工程属性手动配置

生成代码后，右键工程 → **Properties** → **C/C++ Build** → **Settings**:

**① MCU GCC Compiler** → **Preprocessor** → Defined symbols:
```
添加: ARM_MATH_CM4
```

**② MCU GCC Compiler** → **Include paths**:
```
添加: ../Drivers/CMSIS/DSP/Include
```

**③ MCU GCC Linker** → **Libraries**:
- Libraries (-l): 添加 `arm_cortexM4lf_math`
- Library search path (-L): 添加 `../Drivers/CMSIS/DSP/Lib/GCC`

### 验证

```c
#include "arm_math.h"   // Ctrl+Click 能跳转到头文件 = 成功
```
Ctrl+B 编译 → 0 Error → DSP 库就绪。

---

## ═══ 第10步：项目设置 ═══

**Project Manager → Project**:
| 参数 | 值 |
|------|-----|
| Project Name | `05-g474-scope-analyzer` |
| Application Structure | **Basic** |

**Project Manager → Code Generator**:
- ✅ **Copy only the necessary library files**
- ✅ **Generate peripheral initialization as a pair of '.c/.h' files per peripheral**
- ✅ **Keep User Code when re-generating** ⚠️ 必须勾！

---

## ═══ 第11步：保存 + 生成代码 ═══

1. **Ctrl+S** 保存 .ioc 文件
2. ⚠️ 截图: Clock Configuration（时钟树）+ Pinout 视图
3. **Project → Generate Code**（齿轮图标）

---

## ═══ 第12步：生成后验证 ═══

| 检查项 | 怎么看 |
|--------|--------|
| ADC1 时钟分频 | `MX_ADC1_Init()` — `ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4` |
| ADC1 Scan=Disable | `MX_ADC1_Init()` — `ScanConvMode = ADC_SCAN_DISABLE` |
| ADC1 NbrOfConversion=1 | `MX_ADC1_Init()` — `NbrOfConversion = 1` |
| ADC2 时钟分频 | `MX_ADC2_Init()` — `ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4` |
| ADC2 Scan=Disable | `MX_ADC2_Init()` — `ScanConvMode = ADC_SCAN_DISABLE` |
| ADC2 NbrOfConversion=1 | `MX_ADC2_Init()` — `NbrOfConversion = 1` |
| TIM2 ARR=84 | `MX_TIM2_Init()` — `htim2.Init.Period = 84` |
| TIM2 TRGO | `MX_TIM2_Init()` — `AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE` |
| ADC1 DMA Circular | `MX_DMA_Init()` — `hdma_adc1.Init.Mode = DMA_CIRCULAR` |
| ADC2 DMA Circular | `MX_DMA_Init()` — `hdma_adc2.Init.Mode = DMA_CIRCULAR` |
| SWD 开启 | SYS 初始化 — Debug = Serial Wire |
| HSI 使能 | Clock Configuration — SYSCLK = 170MHz, Source = HSI+PLL |

---

## ═══ 引脚总表 ═══

| Pin | Signal | Function |
|-----|--------|----------|
| PA0 | ADC1_IN1 | 信号输入（经 AGC + 偏置，0~3.2V） |
| PA1 | ADC2_IN2 | 检波器输出（直流，= 总信号 Vpp） |
| PA2 | USART2_TX | 串口打印 |
| PA13 | SYS_SWDIO | ST-LINK 调试 |
| PA14 | SYS_SWCLK | ST-LINK 调试 |

