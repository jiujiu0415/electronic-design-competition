# STM32G474 DAC-ADC-FFT — 配置指南（STM32CubeIDE）

> 目标：用 STM32CubeIDE 生成 HAL 工程，内部 DAC 产生正弦波，ADC DMA 采集，Timer 精确控制采样率，FFT 分析后 DAC 同步输出同频信号。

---

## 0. 新建工程

打开 STM32CubeIDE → **File → New → STM32 Project**：

1. 搜索 `STM32G474RE`
2. 选中 **STM32G474RETx**（你的芯片）
3. 点 Next，填工程名 `02-g474-dac-fft`
4. 点 Finish

> 弹出来的 "Initialize all peripherals with their default Mode?" 选 **Yes**。

---

## 1. 系统核心 & 调试

### 1.1 System Core → SYS

| 选项 | 值 | 说明 |
|------|-----|------|
| Debug | **Serial Wire** | 必须！否则 SWD 被禁用 |
| Timebase Source | **SysTick** | HAL 时基 |

### 1.2 System Core → RCC

| 选项 | 值 |
|------|-----|
| High Speed Clock (HSE) | **Disable**（无外部晶振） |
| Low Speed Clock (LSE) | **Disable** |

---

## 2. 时钟树配置（Clock Configuration）

目标：**SYSCLK = 160MHz**

切到 **Clock Configuration** 标签页：

```
① PLL Source Mux      → HSI (16MHz)
② PLLM                → /4       → PLL输入 = 4MHz
③ PLLN                → ×80      → VCO = 320MHz
④ PLLR                → /2       → SYSCLK = 160MHz ✓

⑤ System Clock Mux    → PLLCLK   → 160MHz
⑥ AHB Prescaler       → /1       → HCLK = 160MHz
⑦ APB1 Prescaler      → /1       → APB1 = 160MHz
⑧ APB2 Prescaler      → /1       → APB2 = 160MHz
```

### 验证

```
SYSCLK:  160 MHz
HCLK:    160 MHz
APB1:    160 MHz
APB2:    160 MHz
```

> **为什么是160MHz？** 和PDF教程一致，方便验证和对比。160MHz整数分频容易凑出整采样率。

---

## 3. USART2 — 串口调试输出

### 3.1 Pinout

Pinout 视图 → 选 USART2 → Mode: **Asynchronous**

| 引脚 | 功能 | 说明 |
|------|------|------|
| PA2 | USART2_TX | 接 ST-LINK VCP（或串口模块 RX） |
| PA3 | USART2_RX | 接 ST-LINK VCP（或串口模块 TX） |

### 3.2 参数

Configuration → Connectivity → USART2：

| 参数 | 值 |
|------|-----|
| Baud Rate | **115200** |
| Word Length | 8 Bits |
| Parity | None |
| Stop Bits | 1 |

> ⚠️ **USART2 不要开启中断！** 我们只用 printf 发送，不做接收处理。

---

## 4. DAC1 — 双通道信号输出

### 4.1 Pinout 视图

| 操作 | 结果 |
|------|------|
| 选 DAC1 → OUT1 | PA4 = DAC1_OUT1（"信号源"波形） |
| 选 DAC1 → OUT2 | PA5 = DAC1_OUT2（"同频可控"波形） |

两个通道都勾选 **Connected to DAC Channel x**。

### 4.2 DAC1 OUT1 配置（PA4 = 信号源）

Configuration → Analog → DAC1 → **DAC Out1 Settings** 标签：

| 参数 | 值 | 说明 |
|------|-----|------|
| Mode | **Connected to external pin only** | 只输出到引脚 |
| Output Buffer | **Enable** | 降低输出阻抗 |
| Trigger | **Timer 6 Trigger Out event** | TIM6 触发 DAC 更新 |

> ⚠️ STM32G4 CubeMX里叫 "Trigger"（不是 External Trigger），也没有 Trigger polarity 选项，触发极性由硬件固定。

### 4.3 DAC1 OUT2 配置（PA5 = 同频可控输出）

**DAC Out2 Settings** 标签：

| 参数 | 值 | 说明 |
|------|-----|------|
| Mode | **Connected to external pin only** | 只输出到引脚 |
| Output Buffer | **Enable** | |
| Trigger | **Timer 6 Trigger Out event** | 与OUT1同触发源，硬件保证两路同步 |

### 4.4 DAC1 DMA 配置

**DMA Settings** 标签 → 点 **Add** → 选 **DAC1_CHANNEL_1** → OK：

| 参数 | DAC1_CH1 (PA4) | DAC1_CH2 (PA5) |
|------|---------------|---------------|
| Direction | **Memory to Peripheral** | **Memory to Peripheral** |
| Mode | **Circular** | **Circular** |
| Data Width | **Half Word** | **Half Word** |

> ⚠️ 方向是 Memory→Peripheral（数据从内存流向外设DAC）。先配 CH1 的 DMA，再点 Add 加 CH2。

### 4.5 DAC1 NVIC

**NVIC Settings** 标签：

| 中断 | 使能 |
|------|------|
| DAC1 global interrupt | ☐ 不打开 |

> DAC 用 DMA 循环模式，不需要中断干预。

---

## 5. ADC1 — 信号采集

### 5.1 Pinout 视图

右键 PA0 → **ADC1_IN1**（接 DAC1_OUT1，采集信号源波形）

选 Single-ended 模式。

### 5.2 ADC1 参数

Configuration → Analog → ADC1：

| 参数 | 值 | 说明 |
|------|-----|------|
| Clock Prescaler | **PLLP divided by 4** | ADC 时钟 |
| Resolution | **12 bits** | |
| Data Alignment | **Right alignment** | |
| Continuous Conversion Mode | **Disable** | TIM 触发，不连续 |
| DMA Continuous Requests | **Enable** | |
| Number Of Conversion | **1** | 单通道 |

### 5.3 Regular Conversion → Rank

| Rank | Channel | Sampling Time |
|------|---------|---------------|
| 1 | Channel 1 (IN1) | **2.5 cycles** |

### 5.4 External Trigger

| 选项 | 值 |
|------|-----|
| Trigger Source | **Timer 2 Trigger Out event** |
| Trigger Edge | **Rising edge** |

### 5.5 ADC1 DMA 配置

**DMA Settings** 标签 → 点 **Add** → 选 **ADC1** → OK：

| 参数 | 值 |
|------|-----|
| Mode | **Circular** |
| Data Width | **Half Word** |

### 5.6 ADC1 NVIC

| 中断 | 使能 | 优先级 |
|------|------|--------|
| DMA1 channel 1 interrupt | ☑ | 3 |

---

## 6. 定时器配置

### 6.1 TIM6 — DAC 触发定时器

Configuration → Timers → TIM6：

| 参数 | 值 | 说明 |
|------|-----|------|
| Prescaler | **160-1** = 159 | TIM6_CLK = 160MHz / 160 = 1MHz |
| Counter Period | **10-1** = 9 | 1MHz / 10 = **100kHz DAC更新率** |
| Auto-reload preload | **Enable** | |
| Trigger Output (TRGO) | **Update Event** | |

> **DAC更新率 = 100kHz**，意味着一个正弦波周期有多少个点取决于频率。
> 例：输出10kHz正弦波 → 100/10 = 10个点/周期

### 6.2 TIM2 — ADC 触发定时器

Configuration → Timers → TIM2：

| 参数 | 值 | 说明 |
|------|-----|------|
| Prescaler | **16-1** = 15 | TIM2_CLK = 160MHz / 16 = 10MHz |
| Counter Period | **40-1** = 39 | 10MHz / 40 = **250kHz 采样率** |
| Auto-reload preload | **Enable** | |
| Trigger Output (TRGO) | **Update Event** | |

> **采样率 250kHz**：对于 10kHz 输入信号，每周期 25 个采样点。
>
> TIM2 不开中断！只输出 TRGO 触发 ADC。

---

## 7. NVIC 中断一览

System Core → NVIC：

| 中断 | 使能 | 优先级 | 用途 |
|------|------|--------|------|
| DMA1 channel 1 | ☑ | 3 | ADC DMA 全满中断 → 通知 CPU 做 FFT |

---

## 8. 引脚总览

| 引脚 | 功能 | 用途 |
|------|------|------|
| PA4 | DAC1_OUT1 | 信号源正弦波 → 示波器CH1 + 跳线到PA0 |
| PA5 | DAC1_OUT2 | 同频可控输出 → 示波器CH2 |
| PA0 | ADC1_IN1 | 采集PA4信号（杜邦线短接PA4-PA0） |
| PA2 | USART2_TX | 串口printf调试 |
| PA3 | USART2_RX | 串口接收 |

---

## 9. 工程设置

### Project Manager → Project

| 选项 | 值 |
|------|-----|
| Project Name | `02-g474-dac-fft` |
| Toolchain / IDE | **STM32CubeIDE** |

### Project Manager → Code Generator

| 选项 | 值 |
|------|-----|
| Copy only the necessary library files | ☑ |
| Generate peripheral initialization as a pair of .c/.h | ☑ |
| Set all free pins as analog | ☑ |

---

## 10. 下一步

生成代码后：
- 先编译验证 → 0 Error
- 点灯测试烧录是否正常
- 然后进入第5步：写DAC波形生成代码
