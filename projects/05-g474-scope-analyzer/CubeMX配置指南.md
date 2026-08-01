# CubeMX/CubeIDE 配置指南 — 05-g474-scope-analyzer

> STM32G474RET6（裸芯片）| HSI → PLL → 170MHz
> **ADC1 独立 2MSPS + ADC2 检波器** | TIM2 触发 | GPIO 模拟开关

---

## 方案概述

```
PA0 → ADC1_IN1: 信号波形, TIM2 TRGO @2.0MSPS, DMA Circular, 4096点
PA1 → ADC2_IN2: 检波器直流, 软件触发单次转换 (无 DMA)
PA4 → GPIO: 模拟开关控制 u_J
PA2 → USART2_TX: 串口屏通信 (TJC USART HMI, 115200bps)
PA3 → USART2_RX: 串口屏触摸事件接收
```

| 参数 | 值 |
|------|-----|
| ADC1 采样率 | 2.0 MSPS |
| FFT 点数 | 4096 |
| 频率分辨率 | **488 Hz** ≤500Hz ✅ |
| Nyquist | 1.0 MHz |
| ADC2 | 软件触发单次，只读直流 |
| 模拟开关 | PA4 GPIO Output |

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
  ↓ PLLN=×85 → 340MHz VCO
  ↓ PLLR=/2 → 170MHz SYSCLK
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

---

## ═══ 第4步：ADC1 — 信号采集 ═══

> ADC1 负责采集信号波形（经 AGC + DC 偏置后的 0~3.2V 单极性信号）。
> 单通道，TIM2 TRGO 触发 @2.0 MSPS，DMA Circular 4096 点。

### 4.1 引脚模式

Pinout & Configuration → Analog → **ADC1**:
- IN1 (PA0) → **IN1 Single-ended**

### 4.2 ADC1 参数

ADC1 → **Parameter Settings**:

| 参数 | 值 |
|------|-----|
| Clock Prescaler | **Synchronous clock mode divided by 4** |
| Resolution | **12 bits (15 ADC clock cycles)** |
| Data Alignment | **Right alignment** |
| Scan Conversion Mode | **Disable** |
| Continuous Conversion Mode | **Disable** |
| Discontinuous Conversion Mode | **Disable** |
| DMA Continuous Requests | **Enable** |
| End Of Conversion Selection | **End of single conversion** |
| Overrun behaviour | **Overrun data overwritten** |
| External Trigger Conversion Source | **Timer 2 Trigger Out event** |
| External Trigger Conversion Edge | **Trigger detection on the rising edge** |

Rank（Number Of Conversion = **1**）：

| Rank | Channel | Sampling Time |
|------|---------|---------------|
| 1 | **Channel 1 (PA0)** | **2.5 cycles** |

> 单次转换: (2.5 + 12.5) / 42.5MHz = 353ns < 500ns 触发间隔 ✅

### 4.3 ADC1 DMA Settings

> ⚠️ 铁律：DMA 在 **ADC1 自己的 DMA Settings 标签**里添加，不是 System Core → DMA！

ADC1 → **DMA Settings** → **Add**：

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

## ═══ 第5步：ADC2 — 检波器直流 ═══

> ADC2 只读检波器输出的直流电压（慢速信号），不需要 DMA。
> **PA1 → ADC2_IN2**（注意：是 IN2 不是 IN1，因为 PA1 在 G474 上只能连 ADC2_IN2）。

### 5.1 引脚模式

Pinout & Configuration → Analog → **ADC2**:
- **IN2 (PA1)** → **IN2 Single-ended**

> ⚠️ G474 上 PA1 只能设为 ADC2_IN2，不能设为 ADC2_IN1。
> 参考 git commit c410d89。

### 5.2 ADC2 参数

ADC2 → **Parameter Settings**：

| 参数 | 值 | 说明 |
|------|-----|------|
| Clock Prescaler | **Synchronous clock mode divided by 4** | |
| Resolution | **12 bits** | |
| Scan Conversion Mode | **Disable** | |
| Continuous Conversion Mode | **Disable** | |
| DMA Continuous Requests | **Disable** | 不需要 DMA |
| External Trigger Conversion Source | **Regular Conversion launched by software** | 软件触发 |
| Overrun behaviour | 默认 | |
| End Of Conversion Selection | 默认 | |

Rank（Number Of Conversion = **1**）：

| Rank | Channel | Sampling Time |
|------|---------|---------------|
| 1 | **Channel 2 (PA1)** | **2.5 cycles** |

### 5.3 ADC2 DMA

> **不添加 DMA。** ADC2 只用 `HAL_ADC_Start()` + `HAL_ADC_PollForConversion()` 软件读取。

---

## ═══ 第6步：TIM2 — 触发源 ═══

> TIM2 TRGO @2.0MHz 触发 ADC1。ADC2 不依赖 TIM2。

Pinout & Configuration → Timers → **TIM2**:

| 参数 | 值 | 计算 |
|------|-----|------|
| Clock Source | **Internal Clock** | |
| Channel1~4 | 全部 **Disable** | |
| Prescaler (PSC) | **0** | TIM2_CLK = 170MHz |
| Counter Mode | **Up** | |
| Counter Period (ARR) | **84** | Fs = 170M/(0+1)/(84+1) = **2.0 MHz** |
| Auto-reload preload | **Enable** | |
| **Trigger Output (TRGO)** | **Update Event** | 每次溢出触发 ADC1 |

---

## ═══ 第7步：模拟开关 GPIO（PA4）═══

Pinout & Configuration → System Core → **GPIO**:
- PA4 → **GPIO_Output**

| 参数 | 值 |
|------|-----|
| GPIO output level | **Low**（默认断开 u_J） |
| GPIO mode | **Output Push Pull** |
| GPIO Pull-up/Pull-down | **No pull-up and no pull-down** |
| Maximum output speed | **Low** |
| User Label | `ANALOG_SWITCH`（可选） |

---

## ═══ 第8步：USART2 — 串口屏通信 ═══

> ⚠️ USART2 已从调试串口改为 **TJC 串口屏**（淘晶驰 7" USART HMI），不再用于 printf 调试。

Pinout & Configuration → Connectivity → **USART2**:

| 参数 | 值 |
|------|-----|
| Mode | **Asynchronous** |
| Baud Rate | **115200** Bits/s |
| Word Length | 8 Bits |
| Parity | None |
| Stop Bits | 1 |

| 引脚 | 信号 | 连接 |
|------|------|------|
| PA2 | USART2_TX | → 串口屏 **RX** |
| PA3 | USART2_RX | ← 串口屏 **TX**（触摸事件，必须接） |

---

## ═══ 第9步：NVIC — 中断 ═══

> ⚠️ USART2 必须开 RX 中断接收屏幕触摸事件，否则按键无效。

System Core → **NVIC**:

| 中断 | 状态 | 说明 |
|------|------|------|
| DMA1 Channel1 | ✅ 自动勾选 | ADC1 DMA 传输完成中断 |
| **USART2 global interrupt** | **✅ 手动勾选** | 接收屏幕触摸事件（printh 帧） |

其他保持默认。

---

## ═══ 第10步：CMSIS-DSP（FFT 依赖）═══

Pinout & Configuration → **Software Packs** → **Select Components**:
- CMSIS Pack → 展开 → 勾选 **CMSIS DSP Library**

验证：
```c
#include "arm_math.h"   // Ctrl+Click 能跳转 = 成功
```

---

## ═══ 第11步：Project Manager ═══

**Project Manager → Project**:
| 参数 | 值 |
|------|-----|
| Application Structure | **Basic** |

**Project Manager → Code Generator**:
- ✅ **Copy only the necessary library files**
- ✅ **Generate peripheral initialization as a pair of '.c/.h' files per peripheral**
- ✅ **Keep User Code when re-generating** ⚠️ 必须勾！

---

## ═══ 第12步：保存 → 生成 → 验证 ═══

1. **Ctrl+S** 保存 .ioc
2. **截图**：Clock Configuration + Pinout 视图
3. **Project → Generate Code**

### 生成后验证

| 检查项 | 位置 | 期望值 |
|--------|------|--------|
| ADC1 Scan=Disable | `MX_ADC1_Init()` | `ScanConvMode = ADC_SCAN_DISABLE` |
| ADC1 NbrOfConversion=1 | `MX_ADC1_Init()` | `NbrOfConversion = 1` |
| ADC1 DMA Circular | `MX_DMA_Init()` | `hdma_adc1.Init.Mode = DMA_CIRCULAR` |
| ADC2 ExtTrig=Software | `MX_ADC2_Init()` | 无 ExternalTrigger 或 = software |
| TIM2 ARR=84 | `MX_TIM2_Init()` | `htim2.Init.Period = 84` |
| TIM2 TRGO=Update | `MX_TIM2_Init()` | `TIM_TRGO_UPDATE` |
| SWD 开启 | SYS 初始化 | Debug = Serial Wire |
| SYSCLK=170M | Clock Configuration | HSI → PLL → 170MHz |

---

## ═══ 第13步：代码调用顺序 ═══

```c
// 1. 初始化
ScopeFFT_Init();
ScopeADC_Init();        // 校准 ADC1+ADC2, 启动 TIM2, DMA, 开关默认断开
ScopeDisplay_Init();    // 串口屏: 等待500ms → page 0 → 启动RX中断

// 2. 主循环 (详见 main-integration-reference.c §④)
while (1) {
    ScopeDisplay_ProcessTouch();   // 处理屏幕按键

    ScopeADC_SwitchOpen();         // 断开干扰
    HAL_Delay(10);
    do_measurement();              // 4次采集→平均→更新屏幕

    for (int t = 0; t < 200; t++) {  // 2秒等待, 持续响应触摸
        HAL_Delay(10);
        ScopeDisplay_ProcessTouch();
    }
}
```

---

## ═══ 引脚总表 ═══

| Pin | Signal | Function |
|-----|--------|----------|
| PA0 | ADC1_IN1 | 信号输入（经 AGC + 偏置，0~3.2V） |
| PA1 | ADC2_IN2 | 检波器直流（跟随器后） |
| PA2 | USART2_TX | 串口屏通信 (TJC HMI, 115200bps) |
| PA3 | USART2_RX | 串口屏触摸事件 |
| PA4 | GPIO_Output | 模拟开关 (LOW=断开, HIGH=闭合) |
| PA13 | SYS_SWDIO | ST-LINK 调试 |
| PA14 | SYS_SWCLK | ST-LINK 调试 |
