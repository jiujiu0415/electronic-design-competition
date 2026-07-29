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

## ═══ 第4步：ADC1 配置（高速单通道）═══

### 4.1 引脚模式 ⚠️ G4 特有，必须先做

Pinout & Configuration → Analog → **ADC1**:
- 左侧通道列表 → 找到 **IN1 (PA0)**
- 默认 **Disabled** → 改为 **IN1 Single-ended**

> 不设 Single-ended，后面 Rank 的 Channel 下拉列表找不到 IN1。

### 4.2 ADC 参数设置

ADC1 → **Parameter Settings** 标签:

| 参数 | 值 | 说明 |
|------|-----|------|
| Clock Prescaler | **Synchronous clock mode divided by 4** | 170/4=42.5MHz（<60MHz max） |
| Resolution | **12 bits (15 ADC clock cycles)** | 精度最高 |
| Data Alignment | **Right alignment** | |
| Scan Conversion Mode | **Disable** | 单通道不需要扫描 |
| Continuous Conversion Mode | **Disable** | 由 TIM2 触发，不是连续 |
| Discontinuous Conversion Mode | Disable | |
| DMA Continuous Requests | **Enable** | DMA 循环搬运 |
| End Of Conversion Selection | **End of single conversion** | 单通道选这个，不是 sequence |
| Overrun behaviour | **Overrun data overwritten** | 新数据覆盖旧数据 |
| Low Power Auto Wait | Disable | |

### 4.3 触发源

ADC1 → Parameter Settings → **Regular Conversion** 区域:

| 参数 | 值 |
|------|-----|
| External Trigger Conversion Source | **Timer 2 Trigger Out event** |
| External Trigger Conversion Edge | **Trigger detection on the rising edge** |

### 4.4 Rank 配置

Regular Conversion 区域 → Number Of Conversion = **1**:

| Rank | Channel | Sampling Time |
|------|---------|---------------|
| Rank 1 | **Channel 1 (PA0)** | **2.5 cycles** |

> 单通道 2.5+12.5=15 cycles ÷ 42.5MHz ≈ 0.35μs。TIM2 触发间隔 0.5μs → 充足。

### 4.5 DMA Settings ⚠️ 在 ADC1 自己的标签里添加

ADC1 → **DMA Settings** 标签 → 点击 **Add**:

| 参数 | 值 |
|------|-----|
| DMA Request | **ADC1** |
| Channel | (自动分配，不用改) |
| Direction | **Peripheral To Memory** |
| Mode | **Circular** |
| Data Width (Peripheral) | **Half Word** (16-bit) |
| Data Width (Memory) | **Half Word** (16-bit) |
| Priority | **High** |

> ⚠️ 铁律: DMA 在 ADC 自己的 DMA Settings 标签里加，**不是** System Core → DMA。

---

## ═══ 第5步：TIM2 配置（ADC 采样触发）═══

Pinout & Configuration → Timers → **TIM2**:

### 5.1 Mode 区（页面上方）

| 参数 | 值 |
|------|-----|
| Clock Source | **Internal Clock** |
| Channel1~4 | 全部 **Disable** (不需要 PWM/捕获) |

### 5.2 Configuration 区（页面下方）

| 参数 | 值 | 计算 |
|------|-----|------|
| Prescaler (PSC) | **0** | TIM2 时钟 = 170MHz / (0+1) = 170MHz |
| Counter Mode | **Up** | |
| Counter Period (ARR) | **84** | Fs = 170M/(0+1)/(84+1) = **2.0 MHz** ✅ |
| Auto-reload preload | **Enable** | |
| **Trigger Output (TRGO)** | **Update Event** | 每次溢出触发一次 ADC |

> ARR=84 的解释: 170MHz ÷ 85 = 2.0MHz。每个 Update Event = ADC 采一个点。

---

## ═══ 第6步：USART2 配置（串口打印）═══

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

## ═══ 第7步：NVIC 优先级 ═══

Pinout & Configuration → System Core → **NVIC**:

不需要手动调整。DMA 和 USART 中断使用默认优先级即可。

> ⚠️ DMA 中断在 CubeMX 里显示为 ☑灰色（强制开启），这是对的，不用管。

---

## ═══ 第8步：CMSIS-DSP 库启用 ⚠️ 必须！FFT 依赖！═══

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

## ═══ 第9步：项目设置 ═══

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

## ═══ 第10步：保存 + 生成代码 ═══

1. **Ctrl+S** 保存 .ioc 文件
2. ⚠️ 截图: Clock Configuration（时钟树）+ Pinout 视图
3. **Project → Generate Code**（齿轮图标）

---

## ═══ 第11步：生成后验证 ═══

| 检查项 | 怎么看 |
|--------|--------|
| ADC 时钟分频 | `main.c` 里 `MX_ADC1_Init()` — `ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4` |
| TIM2 ARR=84 | `MX_TIM2_Init()` — `htim2.Init.Period = 84` |
| TIM2 TRGO | `MX_TIM2_Init()` — `htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE` |
| DMA Circular | `MX_DMA_Init()` — `hdma_adc1.Init.Mode = DMA_CIRCULAR` |
| SWD 开启 | `main.c` 顶部 `__HAL_AFIO_REMAP_SWJ_ENABLE()` 或 SYS 初始化 |
| HSI 使能 | Clock Configuration 截图 — SYSCLK = 170MHz, Source = HSI+PLL |

---

## ═══ 引脚总表 ═══

| Pin | Signal | Function |
|-----|--------|----------|
| PA0 | ADC1_IN1 | 信号输入 |
| PA2 | USART2_TX | 串口打印 |
| PA13 | SYS_SWDIO | ST-LINK 调试 |
| PA14 | SYS_SWCLK | ST-LINK 调试 |

