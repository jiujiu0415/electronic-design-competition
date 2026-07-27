# CubeMX/CubeIDE 配置指南 — 04-g474-lcr-sweep-fft

> STM32G474RET6 | HSI → PLL → 170MHz
> 每步操作建议截图保存，电赛现场重配能省一半时间

---

## ├── 第1步：新建 STM32CubeIDE 工程

1. 打开 STM32CubeIDE
2. File → New → STM32 Project
3. 在 MCU/MPU Selector 搜索 `STM32G474RET6`
4. 选中 → Next
5. Project Name: `04-g474-lcr-sweep-fft`
6. 保存位置选 `D:\CURSOR\Electronic Design\projects\`
7. Targeted Language: **C**
8. Finish → 等待初始化
9. ⚠️ 弹出 "Initialize all IPs with their default mode?" → **Yes**

---

## ├── 第2步：时钟配置 (Clock Configuration)

### 2.1 时钟源选择

Pinout & Configuration → System Core → RCC:
- High Speed Clock (HSE): **Disable** (无外部晶振)
- High Speed Clock (HSI): **Crystal/Ceramic Resonator** (使用内部 16MHz RC)

### 2.2 PLL 配置

切换到 **Clock Configuration** 标签页:

```
HSI (16MHz)
  ↓
PLL Source Mux = HSI
  ↓
/PLLM = /4  →  4MHz
  ↓
×PLLN = ×85  →  340MHz VCO  ✅ (96~344MHz 范围内)
  ↓
/PLLR = /2  →  170MHz
  ↓
SYSCLK Mux = PLLRCLK
```

具体参数填入:
| 参数 | 值 |
|------|-----|
| PLL Source | HSI |
| PLLM | /4 |
| PLLN | ×85 |
| PLLR | /2 |
| SYSCLK Source | PLLRCLK |
| **SYSCLK** | **170 MHz** |
| HCLK (AHB) | /1 → 170 MHz |
| APB1 Prescaler | /1 → 170 MHz |
| APB2 Prescaler | /1 → 170 MHz |

⚠️ 要点: APB1=APB2=/1，这样 TIM2 时钟 = 170MHz

---

## ├── 第3步：SPI1 配置（AD9833 DDS）

### 3.1 模式选择

Pinout & Configuration → Connectivity → SPI1:
- Mode: **Transmit Only Master** ← AD9833 只写不读，省下 MISO 引脚
- NSS: **Software** (用 GPIO 手动控制 FSYNC)
- Hardware NSS Signal: 不用管

### 3.2 引脚映射

| 信号 | 引脚 | AD9833 连接 |
|------|------|------------|
| SPI1_SCK | **PA5** | AD9833 SCLK |
| SPI1_MOSI | **PA7** | AD9833 SDATA |

> ⚠️ Transmit Only 模式只有 SCK+MOSI，没有 MISO

### 3.3 参数设置

| 参数 | 值 | 说明 |
|------|-----|------|
| Frame Format | Motorola | 标准 SPI |
| Data Size | **16 Bits** | AD9833 命令就是 16-bit 字 (参考项目03) |
| First Bit | **MSB First** | AD9833 先收高位 |
| **CPOL** | **High** | 空闲时 SCLK=高 ✅ AD9833 手册 |
| **CPHA** | **1 Edge** | 第1个边沿采样 ✅ 即 CPHA=1 |
| Prescaler | /32 | 170/32 ≈ 5.3MHz (单模块,比项目03的664kHz快很多) |
| → Baud Rate | 5.31 MBits/s | |

> ⚠️ 注意: CubeMX 界面 CPHA 选 **1 Edge**（CubeMX 的 1 Edge = 数据手册的 CPHA=1）
> 项目03 因为两颗 AD9833 并联、SCK 负载大，用了 /256 分频(664kHz)。本项目只有一颗，/32(5.3MHz) 安全。

---

## ├── 第4步：SPI2 配置（TFT LCD ST7789）

### 4.1 模式选择

Pinout & Configuration → Connectivity → SPI2:
- Mode: **Transmit Only Master** ← TFT 画图只需写命令+数据，不用读回，省下 PB14
- NSS: **Software** (用 GPIO 手动控制 CS)
- Hardware NSS Signal: 不用管

⚠️ **引脚冲突处理**：CubeMX 可能会把 SPI2_SCK 自动分到 PF1，因为 PB13 默认被占用了（NUCLEO 板上的用户按钮）。如果你是裸芯片/自己画的板子：

1. 在 Pinout 视图里找到 **PF1** → 右键 → **Reset Pin**（清除分配）
2. 找到 **PB13** → 右键 → 选 **SPI2_SCK**
3. 如果 PB13 上还有残留功能（黄色/橙色标记），先右键把它清掉再分配 SPI2_SCK

### 4.2 引脚映射

| 信号 | 引脚 | TFT 连接 |
|------|------|----------|
| SPI2_SCK | **PB13** | TFT SC (时钟) |
| SPI2_MOSI | **PB15** | TFT ADS (数据) |

> ⚠️ Transmit Only 模式下没有 MISO 引脚，TFT 的 SDO(ODS) 不接

### 4.3 参数设置

| 参数 | 值 | 说明 |
|------|-----|------|
| Frame Format | Motorola | 标准 SPI |
| Data Size | **8 Bit** | |
| First Bit | **MSB First** | |
| **CPOL** | **Low** | 空闲时 SCLK=低 ✅ ST7789 |
| **CPHA** | **1 Edge** | 第1个边沿采样 ✅ ST7789 |
| Prescaler | /4 | 170/4 ≈ 42.5MHz (ST7789 理论最大 62.5M，保守用 40M) |
| → Baud Rate | 42.5 MBits/s | |

---

## ├── 第5步：ADC1 配置（双通道）

### 5.1 引脚模式设置 ⚠️ G4 特有，不设无法往下配

在 Pinout 视图里选中 PA0、PA1，或者在 **Pinout & Configuration → Analog → ADC1**：

1. 看左侧 ADC1 通道列表 → 找到 **IN1** 和 **IN2**
2. 每个通道的模式默认是 **Disabled**，勾选为:

| 通道 | 模式 | 说明 |
|------|------|------|
| IN1 (PA0) | **IN1 Single-ended** | 单端采集，对 GND 测电压 |
| IN2 (PA1) | **IN2 Single-ended** | 同上 |

> ⚠️ 不是 **Differential**（差分模式）。差分是把两个引脚之间的电压差，单端才是我们需要的"每个脚独立对地测"。

只有设完 Single-ended 之后，Rank 的 Channel 下拉列表里才会出现 IN1 和 IN2。

### 5.2 基本设置

Pinout & Configuration → Analog → ADC1 → Parameter Settings 标签:

| 参数 | 值 | 说明 |
|------|-----|------|
| Clock Prescaler | **Synchronous clock divided by 4** | ADC 时钟 = 170/4 = 42.5MHz |
| Resolution | **12 bits** | |
| Data Alignment | Right alignment | |

> ⚠️ 注意: `/2` 选项灰掉是正常的。170MHz/2=85MHz 超过 G474 ADC 最大时钟(60MHz)，CubeMX 自动限制了。`/4=42.5MHz` 在规格内，完全够用。
| Scan Conversion Mode | **Enable** | 设 Number Of Conversion≥2 后自动开 |
| Continuous Conversion Mode | **Disable** | TIM 触发控制采样 |
| Discontinuous Conversion Mode | Disable | |
| DMA Continuous Requests | **Enable** | |
| End Of Conversion Selection | **End of sequence of conversion** | 双通道扫完后统一触发 DMA |
| Overrun behaviour | Overrun data overwritten | |
| Low Power Auto Wait | Disable | |

> ⚠️ CubeMX 实际只有两个选项: `End of single conversion` 和 `End of sequence of conversion`。
> 选后者——DMA 在两路通道都转换完后再搬数据，保证 CH1/CH2 在缓冲区里正确交错。

### 5.3 ADC 常规转换 (Regular Conversion)

⚠️ **Number Of Conversion** 设为 **2** → Scan Mode 自动启用

| Rank | Channel | Sampling Time |
|------|---------|---------------|
| Rank 1 | **Channel 1** (PA0) | **2.5 cycles** (实际: 2.5+12.5 = 15 cycles @42.5MHz ≈ 0.35μs) |
| Rank 2 | **Channel 2** (PA1) | **2.5 cycles** |

> 单通道转换时间 = (2.5 + 12.5) / 42.5MHz ≈ 0.35μs，双通道 ≈ 0.7μs。
> 500kHz 采样率下触发间隔 = 2μs，ADC 有充足时间完成双通道转换。

### 5.4 ADC 触发设置

| 参数 | 值 |
|------|-----|
| External Trigger Conversion Source | **Timer 2 Trigger Out event** |
| External Trigger Conversion Edge | **Rising edge** |
| Timer Trigger Detection | 自动 |

### 5.5 ADC DMA Settings 标签 → **Add**

⚠️ 铁律提醒: **DMA 在 ADC 自己的 DMA Settings 标签里加，不是 System Core → DMA**

| 参数 | 值 |
|------|-----|
| DMA Request | ADC1 |
| DMA Mode | **Circular** |
| DMA Direction | Peripheral To Memory |
| DMA Data Width (Peripheral) | **Half Word** (16-bit, ADC 12位) |
| DMA Data Width (Memory) | **Half Word** (16-bit) |
| DMA Priority | High |

> DMA 把 CH1/CH2 交错写入 adc_buf[ ]:
> buf[0]=CH1₀, buf[1]=CH2₀, buf[2]=CH1₁, buf[3]=CH2₁, …

---

## ├── 第6步：TIM2 配置（ADC 采样触发）

Pinout & Configuration → Timers → TIM2:

| 参数 | 值 | 说明 |
|------|-----|------|
| Clock Source | **Internal Clock** | |
| Prescaler (PSC) | **0** | |
| Counter Mode | Up | |
| Counter Period (ARR) | **339** | 170M / 340 = 500kHz 采样率 |
| Auto-reload preload | Enable | |
| **Trigger Output (TRGO)** | **Update Event** | ← 关键! 用于触发 ADC |

- 采样率 Fs = 170MHz / (0+1) / (339+1) = **500 kHz**
- FFT_SIZE = 2048 → 频率分辨率 = 500k/2048 ≈ **244 Hz**
- 奈奎斯特频率 = 250 kHz → 可测量到 100 kHz (扫频上限)
- 低频段 (如 100Hz) 分辨率不足 244Hz → 后续可动态降采样率

---

## ├── 第7步：USART2 配置（调试串口）

Pinout & Configuration → Connectivity → USART2:

| 参数 | 值 |
|------|-----|
| Mode | **Asynchronous** |
| Baud Rate | **115200** Bits/s |
| Word Length | 8 Bits |
| Parity | None |
| Stop Bits | 1 |
| Data Direction | Transmit only (或收发都行) |

引脚:
| 信号 | 引脚 |
|------|------|
| USART2_TX | **PA2** |
| USART2_RX | **PA3** |

---

## ├── 第8步：GPIO 配置（控制引脚）

Pinout & Configuration → System Core → GPIO:

| 引脚 | 功能 | GPIO 配置 | 用途 |
|------|------|-----------|------|
| **PA4** | GPIO_Output | Label: `AD9833_FSYNC` | AD9833 片选 (FSYNC) |
| **PA6** | GPIO_Output | Label: `MCP41010_CS` | AD9833 模块上 MCP41010 数字电位器片选 |
| **PB0** | GPIO_Output | Label: `TFT_DC` | TFT 命令/数据选择 |
| **PB1** | GPIO_Output | Label: `TFT_CS` | TFT 片选 |
| **PA8** | GPIO_Output | Label: `TFT_RST` | TFT 复位 |

所有 GPIO 输出设置:
- GPIO output level: **High** (初始高电平 = 不选中/不复位)
- GPIO mode: **Output Push Pull**
- GPIO Pull-up/Pull-down: **No pull-up and no pull-down** (TFT 模块有板载上拉)
- Maximum output speed: **Low** (普通 GPIO 够用)

---

## ├── 第9步：NVIC 优先级

Pinout & Configuration → System Core → NVIC:

目前不需要中断优先级的特别调整。DMA 中断和 ADC 中断使用默认优先级即可。

---

## ├── 第10步：项目设置

Project Manager → Project:
- Project Name: `04-g474-lcr-sweep-fft`
- Application Structure: **Basic**

Project Manager → Code Generator:
- 勾选 **Copy only the necessary library files**
- 勾选 **Generate peripheral initialization as a pair of '.c/.h' files per peripheral**
- ⚠️ 务必勾选 **Keep User Code when re-generating**（重新生成代码时不覆盖用户代码区）

---

## ├── 第11步：CMSIS-DSP 库启用

这是 STM32G4 的额外步骤——G4 系列自带 CMSIS-DSP 硬件加速，需要在工程属性里启用:

1. 右键工程 → Properties → C/C++ Build → Settings
2. Tool Settings → MCU GCC Linker → Libraries
3. Libraries (-l): 添加 `arm_cortexM4lf_math`
4. Library search path (-L): `${cubeide_dir}/../Middlewares/ST/ARM/DSP/Lib`

或者在 CubeMX 里:
- Pinout & Configuration → Middleware → 找 DSP 相关选项
- 如果没有界面选项，就在代码里手动 `#include "arm_math.h"` 并在链接设置里加库

> STM32CubeIDE 通常在 `Software Packs → Select Components` 里勾选 CMSIS DSP

---

## ├── 第12步：保存 + 生成代码

1. **Ctrl+S** 保存 .ioc 文件
2. ⚠️ 截图保存: Clock Configuration 页面 + Pinout 视图 + 每个外设的 Param Settings
3. **Project → Generate Code** (或工具栏齿轮图标)
4. 等待生成完成

---

## └── 检查清单（生成代码后验证）

生成完成后检查以下几点:

- `main.c` 里 MX_SPI1_Init() 的 CPOL=High, CPHA=1Edge, DataSize=16Bit
- `main.c` 里 MX_SPI2_Init() 的 CPOL=Low, CPHA=1Edge, DataSize=8Bit
- `main.c` 里 MX_ADC1_Init() 有 2 个 Regular Conversion Rank
- `main.c` 里 MX_DMA_Init() 包含 ADC1 的 DMA 配置
- `main.c` 里 MX_TIM2_Init() PSC=0, ARR=339, TRGO=Update Event
- `stm32g4xx_hal_msp.c` 里各外设的 GPIO 引脚正确
- CMSIS-DSP 库能正确 `#include "arm_math.h"`

---

## 速查：最终引脚分配总表

| Pin | Signal | Function | Target |
|-----|--------|----------|--------|
| PA0 | ADC1_IN1 | 模数采集 CH1 | LCR 输入参考 |
| PA1 | ADC1_IN2 | 模数采集 CH2 | LCR 输出响应 |
| PA2 | USART2_TX | 调试输出 | ST-LINK VCP |
| PA3 | USART2_RX | 调试输入 | ST-LINK VCP |
| PA4 | GPIO_OUT | AD9833 片选 | AD9833 FSYNC |
| PA5 | SPI1_SCK | SPI 时钟 | AD9833 SCLK |
| PA6 | GPIO_OUT | MCP41010 片选 | AD9833 模块 J1-6 (CS) |
| PA7 | SPI1_MOSI | SPI 数据 | AD9833 SDATA |
| PA8 | GPIO_OUT | TFT 复位 | TFT RST |
| PB0 | GPIO_OUT | TFT 命令/数据 | TFT DC |
| PB1 | GPIO_OUT | TFT 片选 | TFT CS |
| PB13 | SPI2_SCK | SPI 时钟 | TFT SC |
| PB15 | SPI2_MOSI | SPI 数据 | TFT SDA |
