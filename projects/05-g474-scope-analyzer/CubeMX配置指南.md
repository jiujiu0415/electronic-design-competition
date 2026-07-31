# CubeMX/CubeIDE 配置指南 — 05-g474-scope-analyzer (v3)

> STM32G474RET6（裸芯片）| HSI → PLL → 170MHz
> **ADC1+2 Dual Interleaved 4MSPS** | ADC3 检波器 | TIM2 触发 | GPIO 模拟开关

---

## v2 → v3 核心变化

| | v2 (旧) | v3 (新) |
|---|---------|---------|
| ADC1 | PA0，独立 2MSPS | **PA0，Master，交替 4MSPS** |
| ADC2 | PA1，独立 2MSPS（检波器） | **PA0，Slave，与 ADC1 交替** |
| ADC3 | — | **PA1，独立采集检波器直流** |
| DMA | 两个独立 16-bit DMA | **ADC1 单路 32-bit DMA（读 CDR）** |
| FFT | 4096 @2MSPS | **8192 @4MSPS** |
| 模拟开关 | — | **PA4 GPIO 控制 u_J 接入** |

---

## ═══ 第1步：新建工程 ═══

1. STM32CubeIDE → File → New → STM32 Project
2. 搜索 `STM32G474RET6` → 选中 → Next
3. Project Name: `05-g474-scope-analyzer`
4. Targeted Language: **C**
5. ⚠️ 弹窗 "Initialize all IPs with their default mode?" → **Yes**

---

## ═══ 第2步：调试接口 + 系统配置 ═══

### 2.1 SYS — 调试口

System Core → **SYS**:
| 参数 | 值 |
|------|-----|
| Debug | **Serial Wire** |

> ⚠️ 裸芯片必须手动开 SWD，否则 PA13/PA14 复用为 GPIO → 无法烧录

### 2.2 RCC — 时钟源

System Core → **RCC**:
| 参数 | 值 |
|------|-----|
| High Speed Clock (HSE) | **Disable** (无外部晶振) |
| High Speed Clock (HSI) | **开启** (内部 16MHz RC) |

---

## ═══ 第3步：时钟树 (Clock Configuration) ═══

```
HSI (16MHz)
  ↓ PLLM=/4 → 4MHz
  ↓ PLLN=×85 → 340MHz VCO ✅ (96~344MHz)
  ↓ PLLR=/2 → 170MHz SYSCLK
  ↓
SYSCLK Mux = PLLRCLK
```

| 参数 | 值 |
|------|-----|
| PLL Source Mux | **HSI** |
| PLLM | **/4** |
| PLLN | **×85** |
| PLLR | **/2** |
| SYSCLK Mux | **PLLRCLK** |
| **→ SYSCLK = 170 MHz** | |
| AHB Prescaler | **/1** |
| APB1 Prescaler | **/1** |
| APB2 Prescaler | **/1** |

> APB1=APB2=/1 → TIM2 时钟 = 170MHz。ADC 时钟 = SYSCLK/4 = 42.5MHz。

---

## ═══ 第4步：ADC1 + ADC2 — 双交替配置（信号采集）═══

> **核心**：ADC1 和 ADC2 都接 PA0，交替采样同一信号。
> TIM2 TRGO @2MHz 触发 ADC1 → ADC2 自动跟随（延迟 T/2=250ns）→ 等效 4MSPS。

### 4.1 引脚模式

Pinout & Configuration → Analog → **ADC1**:
- IN1 (PA0) → **IN1 Single-ended**

Pinout & Configuration → Analog → **ADC2**:
- IN1 (PA0) → **IN1 Single-ended**（和 ADC1 同一个引脚）

> **验证 PA0**：在 Pinout 视图点击 PA0，右侧应同时列出 `ADC1_IN1` 和 `ADC2_IN1`。

> **ADC2 如何成为 Slave？** 不需要在 ADC2 单独设置。只要 ADC1 的 ADCs_Common_Settings 里选了 "Interleaved mode only"，CubeMX 自动把 ADC2 当作 Slave。ADC2 的 External Trigger 等选项会被自动隐藏——这就是它已成为 Slave 的标志。

### 4.2 ADC1 参数（Master）

ADC1 → **Parameter Settings**:

| 参数 | 值 | 说明 |
|------|-----|------|
| Clock Prescaler | **Synchronous clock mode divided by 4** | 170/4=42.5MHz |
| Resolution | **12 bits (15 ADC clock cycles)** | |
| Data Alignment | **Right alignment** | |
| Scan Conversion Mode | **Disable** | |
| Continuous Conversion Mode | **Disable** | TIM2 触发 |
| Discontinuous Conversion Mode | Disable | |
| DMA Continuous Requests | **Enable** | |
| End Of Conversion Selection | **End of single conversion** | |
| Overrun behaviour | **Overrun data overwritten** | |

触发源：

| 参数 | 值 |
|------|-----|
| External Trigger Conversion Source | **Timer 2 Trigger Out event** |
| External Trigger Conversion Edge | **Trigger detection on the rising edge** |

Rank 配置（Number Of Conversion = **1**）：

| Rank | Channel | Sampling Time |
|------|---------|---------------|
| 1 | **Channel 1 (PA0)** | **2.5 cycles** |

### 4.3 ADC1 Dual Mode 配置（ADCs_Common_Settings）

> ⚠️ 关键！在 ADC1 Parameter Settings 底部找到 "ADCs_Common_Settings" 区域。

| 参数 | 值 | HAL 常量 | 寄存器位 |
|------|-----|----------|---------|
| **Dual regular conversion mode** | **Interleaved mode only** | `ADC_DUALMODE_INTERL` | DUAL[4:0]=00111 |
| **DMA Access Mode** | **Enabled** | — | MDMA[1:0]=11（自动根据 DMA 位宽设定） |
| **Delay between two sampling phases** | **11 ADC clock cycles** | `ADC_TWOSAMPLINGDELAY_11CYCLES` | DELAY[3:0]=1011 |

> **Delay 计算**：触发周期 500ns → 理想交替间距 = 250ns。
> 250ns / (1/42.5MHz) = 10.6 cycles → 取 11 cycles = 258.8ns（偏差 8.8ns 可忽略）。

> **MDMA 模式说明**：CubeMX 中 DMA Access Mode 只有 Enabled/Disabled。
> MDMA 具体是 mode 1（16-bit 交替）还是 mode 2（32-bit 打包）由 **DMA Data Width** 自动决定：
> - DMA Data Width = **Half Word** → CDR 按 16-bit 交替读 → uint16_t[8192]
> - DMA Data Width = **Word** → CDR 按 32-bit 打包读 → uint32_t[4096]（每字含 ADC1[15:0]+ADC2[31:16]）
>
> **本文选择 Word (32-bit)**，配合后面 DMA Settings 一致。

### 4.4 ADC2 参数（Slave）

ADC2 → **Parameter Settings**：

> ⚠️ ADC2 设为 Interleaved 模式的 Slave 后，CubeMX 会**自动隐藏/锁定**部分选项。
> 这是正常的：
> - **External Trigger** 被隐藏 → Slave 的触发由 Master（ADC1）的双模式机制自动管理，不从外部源取触发
> - **DMA Continuous Requests** 灰色/无法 Enable → Slave 不需要独立 DMA，数据统一进 ADC_CDR 由 ADC1 的 DMA 搬运
> - **End Of Conversion Selection** 和 **Overrun behaviour** → 保持默认即可，Dual 模式下由 Master 统一控制

只需配置以下选项（与 ADC1 保持一致）：

| 参数 | 值 |
|------|-----|
| Clock Prescaler | **Synchronous clock mode divided by 4** |
| Resolution | **12 bits** |
| Scan Conversion Mode | **Disable** |
| Continuous Conversion Mode | **Disable** |
| DMA Continuous Requests | **Disable**（Slave 不需要 DMA） |

Rank（Number Of Conversion = **1**）：

| Rank | Channel | Sampling Time |
|------|---------|---------------|
| 1 | **Channel 1 (PA0)** | **2.5 cycles** |

> ⚠️ **ADC2 不添加 DMA Settings 标签页的任何条目！** Dual 模式数据通过 ADC_CDR 统一由 ADC1 的 DMA 搬运。

### 4.5 ADC1 DMA Settings

> ⚠️ 铁律：DMA 在 **ADC1 自己的 DMA Settings 标签** 里添加，不是 System Core → DMA！

ADC1 → **DMA Settings** → **Add**：

| 参数 | 值 | 说明 |
|------|-----|------|
| DMA Request | **ADC1** | |
| Channel | **DMA1 Channel 1** | CubeMX 自动分配 |
| Direction | **Peripheral To Memory** | |
| Mode | **Circular** | |
| Data Width (Peripheral) | **Word** | ⚠️ 32-bit，读 ADC_CDR 全字 |
| Data Width (Memory) | **Word** | |
| Priority | **High** | |
| Increment Address (Peripheral) | **不勾** | 固定地址 ADC_CDR |
| Increment Address (Memory) | **勾** | 自动递增 |

> **DMA 数据格式**（MDMA mode 2, 32-bit）：
> ```
> CDR[31:16] = ADC2 转换值 (12-bit, 右对齐)
> CDR[15:0]  = ADC1 转换值 (12-bit, 右对齐)
> ```
> 1 个 DMA Word = 2 个采样点。4096 Words × 2 = 8192 总采样点 @4MSPS。

---

## ═══ 第5步：ADC3 — 检波器独立采集 ═══

> ADC1+2 被交替模式占满后，检波器信号转到 PA1 → **ADC3_IN1**。
> 检波器是直流，不需要高速采集，软件触发单次转换即可。

### 5.1 引脚模式

Pinout & Configuration → Analog → **ADC3**:
- IN1 (PA1) → **IN1 Single-ended**

> PA1 在 G474 上的 ADC 功能：ADC1_IN2, ADC2_IN2, **ADC3_IN1**。选 ADC3_IN1 因为 ADC1/2 已被交替模式占用。
> 参考：数据手册 DS12288 Rev 6，PA1 = Fast Channel（ADC3_IN1）。

### 5.2 ADC3 参数

ADC3 → **Parameter Settings**：

| 参数 | 值 | 说明 |
|------|-----|------|
| Clock Prescaler | **Synchronous clock mode divided by 4** | |
| Resolution | **12 bits** | |
| Data Alignment | Right alignment | |
| Scan Conversion Mode | **Disable** | |
| Continuous Conversion Mode | **Disable** | 软件触发单次 |
| External Trigger Conversion Source | **Regular Conversion launched by software** | |
| Low Power Auto Wait | Disable | |

Rank（Number Of Conversion = **1**）：

| Rank | Channel | Sampling Time |
|------|---------|---------------|
| 1 | **Channel 1 (PA1)** | **2.5 cycles** |

### 5.3 ADC3 DMA

> **不需要 DMA。** 检波器是直流信号，软件触发后 `HAL_ADC_PollForConversion()` 读取即可。

---

## ═══ 第6步：TIM2 — 交替触发源 ═══

> TIM2 TRGO @2MHz → 每个边沿触发 ADC1 → ADC2 自动延迟 11 cycles 后跟随。
> 2M 触发/秒 × 2 采样/触发 = **4.0 MSPS**。

Pinout & Configuration → Timers → **TIM2**:

### Mode

| 参数 | 值 |
|------|-----|
| Clock Source | **Internal Clock** |
| Channel1~4 | 全部 **Disable** |

### Configuration

| 参数 | 值 | 计算 |
|------|-----|------|
| Prescaler (PSC) | **0** | TIM2_CLK = 170MHz |
| Counter Mode | **Up** | |
| Counter Period (ARR) | **84** | Fs = 170M/(0+1)/(84+1) = **2.0 MHz** |
| Auto-reload preload | **Enable** | |
| **Trigger Output (TRGO)** | **Update Event** | 每次溢出触发 |

> **验证**：交替模式下 2MHz 触发 × 2 ADC = 4MSPS ✓
> 数据手册（DS12288 §3.18）：G474 ADC 最大 4 MSPS@12-bit ✓

---

## ═══ 第7步：模拟开关 GPIO（PA4）═══

> 用 PA4 控制模拟开关的通断：
> - **要求1/2**（无干扰）→ GPIO 高/低 → 模拟开关断开 u_J
> - **要求3**（有干扰）→ GPIO 翻转 → 模拟开关闭合，u_J 接入加法器

Pinout & Configuration → System Core → **GPIO**:
- PA4 → **GPIO_Output**

| 参数 | 值 |
|------|-----|
| GPIO output level | **Low**（默认断开 u_J） |
| GPIO mode | **Output Push Pull** |
| GPIO Pull-up/Pull-down | **No pull-up and no pull-down** |
| Maximum output speed | **Low**（慢速足够，开关切换不频繁） |
| User Label | `ANALOG_SWITCH`（可选，便于代码中查找） |

---

## ═══ 第8步：USART2（串口打印）═══

Pinout & Configuration → Connectivity → **USART2**:

| 参数 | 值 |
|------|-----|
| Mode | **Asynchronous** |
| Baud Rate | **115200** Bits/s |
| Word Length | 8 Bits |
| Parity | None |
| Stop Bits | 1 |

| 引脚 | 信号 |
|------|------|
| PA2 | USART2_TX |
| PA3 | USART2_RX（可选） |

---

## ═══ 第9步：NVIC ═══

Pinout & Configuration → System Core → **NVIC**:

DMA1 Channel1 中断**自动勾选且灰色**（ADC1 DMA 的 Circular 模式必需），保持默认即可。

其他中断使用默认优先级，无需手动调整。

---

## ═══ 第10步：CMSIS-DSP（FFT 依赖）═══

> ⚠️ FFT 8192 需要 CMSIS-DSP 库。不加 = 编译满屏 `undefined reference`。

### 方法一：CubeMX Software Packs（推荐）

Pinout & Configuration → **Software Packs** → **Select Components**:
- CMSIS Pack → 展开 → 勾选 **CMSIS DSP Library**

### 方法二：手动配置

生成代码后，右键工程 → Properties → C/C++ Build → Settings:

**① Preprocessor** → Defined symbols:
```
ARM_MATH_CM4
```

**② Include paths:**
```
../Drivers/CMSIS/DSP/Include
```

**③ Linker** → Libraries:
```
arm_cortexM4lf_math
```
Library search path: `../Drivers/CMSIS/DSP/Lib/GCC`

### 验证

```c
#include "arm_math.h"   // Ctrl+Click 能跳转 = 成功
```

---

## ═══ 第11步：Project Manager ═══

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

## ═══ 第12步：保存 → 生成代码 → 验证 ═══

1. **Ctrl+S** 保存 .ioc
2. **截图**（电赛现场重配会快很多）：
   - Clock Configuration 时钟树
   - Pinout 视图（确认 PA0 双 ADC + PA1 ADC3 + PA4 GPIO）
   - ADC1 ADCs_Common_Settings
3. **Project → Generate Code**

### 生成后验证

| 检查项 | 文件/位置 | 期望值 |
|--------|----------|--------|
| ADC1 Dual Mode | `MX_ADC1_Init()` | `Multimode = ADC_DUALMODE_INTERL` |
| ADC1 MDMA | `MX_ADC1_Init()` | `DMAAccessMode = ADC_DMAACCESSMODE_12_10_BITS` (DMA Access Mode=Enabled 时自动设为 mode 2) |
| ADC1 TwoSamplingDelay | `MX_ADC1_Init()` | `TwoSamplingDelay = ADC_TWOSAMPLINGDELAY_11CYCLES` |
| ADC1 DMA Word | `MX_DMA_Init()` | `hdma_adc1.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD` |
| TIM2 ARR=84 | `MX_TIM2_Init()` | `htim2.Init.Period = 84` |
| TIM2 TRGO | `MX_TIM2_Init()` | `TIM_TRGO_UPDATE` |
| ADC3 独立 | `MX_ADC3_Init()` | 无 Dual Mode 字段 |
| SWD 开启 | SYS 初始化 | Debug = Serial Wire |
| SYSCLK=170M | Clock Configuration | HSI → PLL → 170MHz |

---

## ═══ 第13步：内存布局确认 ═══

> STM32G474RET6 SRAM 分配（数据手册 §3.5）：

| SRAM 区域 | 大小 | 用途 |
|-----------|------|------|
| SRAM1 | 80 KB | DMA 缓冲 (16KB) + HAL/栈/堆 |
| SRAM2 | 16 KB | 校准表、状态变量 |
| CCM SRAM | 32 KB | FFT 输入/输出缓冲 float32[8192] = 32KB（**不可 DMA**） |

> ⚠️ CCM SRAM 不能做 DMA 目标。DMA 缓冲必须在 SRAM1。
> FFT 处理不涉及 DMA，放 CCM 速度最快。

---

## ═══ 引脚总表（v3）═══

| Pin | Signal | Function | 备注 |
|-----|--------|----------|------|
| PA0 | ADC1_IN1 + ADC2_IN1 | 信号输入（经 AGC+偏置） | **双 ADC 交替** |
| PA1 | ADC3_IN1 | 检波器直流（跟随器后） | 独立 ADC3 |
| PA2 | USART2_TX | 串口打印 | |
| PA4 | GPIO_Output | 模拟开关控制 | LOW=断开, HIGH=闭合 |
| PA13 | SYS_SWDIO | ST-LINK 调试 | |
| PA14 | SYS_SWCLK | ST-LINK 调试 | |

---

## ═══ 代码调用顺序 ═══

建立在 CubeMX 生成的 HAL 基础上，`scope_adc.c` 按以下顺序操作：

```c
// 1. 校准 ADC1, ADC2, ADC3
HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED);
HAL_ADCEx_Calibration_Start(&hadc2, ADC_SINGLE_ENDED);
HAL_ADCEx_Calibration_Start(&hadc3, ADC_SINGLE_ENDED);

// 2. 启动 TIM2
HAL_TIM_Base_Start(&htim2);

// 3. 先启动 ADC2 (Slave)，再启动 ADC1 (Master) with DMA
HAL_ADC_Start(&hadc2);     // Slave 先跑
HAL_ADCEx_MultiModeStart_DMA(&hadc1, (uint32_t *)dmabuf, 4096);

// 4. 等待 DMA 完成 (HAL_ADC_ConvCpltCallback)
// 5. ADC3 单次转换读检波器
HAL_ADC_Start(&hadc3);
HAL_ADC_PollForConversion(&hadc3, 10);
uint16_t vd_raw = HAL_ADC_GetValue(&hadc3);
```
