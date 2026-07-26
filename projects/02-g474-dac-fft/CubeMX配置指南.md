# STM32G474 DAC-ADC-FFT — 配置指南（STM32CubeIDE）

> 目标：用 STM32CubeIDE 生成 HAL 工程，内部 DAC 产生正弦波，ADC DMA 采集，Timer 精确控制采样率，FFT 分析后 DAC 同步输出同频信号。

---

## 0. 新建工程

打开 STM32CubeIDE → **File → New → STM32 Project**：

1. 搜索 `STM32G474RE`
2. 选中 **STM32G474RETx**（你的芯片）
3. 点 Next，填工程名 `02-g474-dac-fft`
4. Location 选到 `D:\CURSOR\Electronic Design\projects\`
5. 点 Finish

> 弹出来的 "Initialize all peripherals with their default Mode?" 选 **Yes**。

---

## 1. 系统核心 & 调试

### 1.1 System Core → SYS

| 选项 | 值 | 说明 |
|------|-----|------|
| Debug | **Serial Wire** | 必须！否则 SWD 被禁用 |
| Timebase Source | **SysTick** | HAL 时基 |

> 其余 SYS 选项全部保持默认，不用改。

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

| 参数 | 值 | 说明 |
|------|-----|------|
| Baud Rate | **115200** | |
| Word Length | 8 Bits | 默认 |
| Parity | None | 默认 |
| Stop Bits | 1 | 默认 |
| Hardware Flow Control | Disable | 默认，不流控 |
| Over Sampling | 16 Samples | 默认 |

> ⚠️ **USART2 不要开启中断！** 我们只用 printf 发送，不做接收处理。
>
> 以上6项之外的参数全部保持 CubeMX 默认值，不用改。

---

## 4. DAC1 — 双通道信号输出

### 4.1 Pinout 视图

在 Pinout 视图里，展开 Analog → **DAC1**：

| 操作 | 结果 |
|------|------|
| 勾选 OUT1 | PA4 自动变模拟模式 = DAC1_OUT1 |
| 勾选 OUT2 | PA5 自动变模拟模式 = DAC1_OUT2 |

> CubeMX 会自动把 PA4、PA5 设为 Analog mode，不需要手动配 GPIO。

### 4.2 DAC1 OUT1 配置（PA4 = 信号源）

Configuration → Analog → DAC1 → **DAC Out1 Settings** 标签：

**需要改的：**

| 参数 | 值 | 说明 |
|------|-----|------|
| Mode | **Connected to external pin and to on chip-peripherals** | PA4引脚输出 + 内部可连 |
| Output Buffer | **Enable** | 降低输出阻抗（代价：最低只能到~0.2V，不影响正弦波应用） |
| Trigger | **Timer 6 Trigger Out event** | TIM6 每次溢出触发 DAC 输出新值 |

**必须确认默认是对的：**

| 参数 | 应有的值 | 说明 |
|------|---------|------|
| Wave Generation Mode | **Disabled** ⚠️ | **必须！** 如果选 Noise/Triangle，硬件波形发生器会覆盖 DMA 数据！ |
| User Trimming | **Factory trimming** | 用出厂校准值 |
| DAC High Frequency Mode | **Automatic** | 默认 |
| DMA Double Data | **Disable** | 默认 |
| Signed Format | **Disable** | 默认 |
| Sample And Hold | **Sampleandhold Disable** | 默认 |

> ⚠️ G4系列CubeMX里叫 "Trigger"（不是 External Trigger），也没有 Trigger polarity 选项，触发极性由硬件固定。

### 4.3 DAC1 OUT2 配置（PA5 = 同频可控输出）

**DAC Out2 Settings** 标签：

**需要改的：**

| 参数 | 值 | 说明 |
|------|-----|------|
| Mode | **Connected to external pin and to on chip-peripherals** | PA5引脚输出 |
| Output Buffer | **Enable** | |
| Trigger | **Timer 6 Trigger Out event** | 与OUT1同触发源，硬件保证两路同步 |

**必须确认默认是对的：** （同OUT1，Wave Generation Mode = Disabled 等）

### 4.4 DAC1 DMA 配置

**DMA Settings** 标签 → 点 **Add** → 选 **DAC1_CH1** → OK：

| 参数 | DAC1_CH1 (PA4) | 说明 |
|------|---------------|------|
| Direction | **Memory to Peripheral** | 数据流向：内存 → DAC |
| Mode | **Circular** | 波形表循环输出，不停 |
| Priority | **Low** | |
| Increment Address (Peripheral) | ☐ **不勾** | DAC 数据寄存器是固定地址 |
| Increment Address (Memory) | ☑ **勾上** | 依次读 `dac_buf[0],[1],[2]...` |
| Data Width (Peripheral) | **Word** ⚠️ | G4 的 DAC 在 AHB 总线，必须 32-bit |
| Data Width (Memory) | **Half Word** | 对应 uint16_t 波形数组 |

配完 CH1 后，再点 **Add** → 选 **DAC1_CH2** → 同样配置。

> ⚠️ **为什么 Peripheral 必须用 Word？** STM32G4 的 DAC 挂在 AHB 总线上，
> AHB 不支持 16-bit 传输。用 Half Word 会导致 DMA 传输错误，
> DAC 输出始终为 0V。这是 G4 和 F1/F4 系列的重要区别！

> 📌 其余未提到的 DMA 选项（FIFO、Burst等）全部保持默认。

### 4.5 DAC1 NVIC

**NVIC Settings** 标签：

| 中断 | 使能 |
|------|------|
| DAC1 global interrupt | ☐ 不打开 |

> DAC 用 DMA 循环模式，不需要中断干预。

---

## 5. ADC1 — 信号采集

### 5.1 Pinout 视图

右键 PA0 → **ADC1_IN1**（接 DAC1_OUT1，杜邦线短接 PA4 ↔ PA0）

> 选 Single-ended 模式。ADC1_IN0 (PA0) 和 ADC1_IN1 (PA1) 物理上不同。

### 5.2 ADC1 参数

Configuration → Analog → ADC1：

**需要改的：**

| 参数 | 值 | 说明 |
|------|-----|------|
| Clock Prescaler | **Synchronous clock mode divided by 4** | 同步模式，160/4=40MHz |
| Resolution | **12 bits** | |
| Data Alignment | **Right alignment** | 读出来就是 uint16 的 0~4095 |
| Continuous Conversion Mode | **Disable** | TIM2 TRGO 每次触发才转换一次 |
| DMA Continuous Requests | **Enable** | DMA 持续请求 |
| End of Conversion Selection | **End of sequence of conversion** | 所有通道采完才置 EOC |
| Number Of Conversion | **1** | 只采 1 个通道 |

> ⚠️ 时钟模式选**同步**而不是异步：TIM2触发ADC时，同步模式避免跨时钟域抖动。
> /1 和 /2 选项灰色不可选 = 正常，因为 160MHz 和 80MHz 超过 ADC 限速（~52MHz）。

**必须确认默认是对的：**

| 参数 | 应有的值 | 说明 |
|------|---------|------|
| Scan Conversion Mode | 灰着/自动 | G4 系列 ≥2 通道才自动启用，1 通道不用管 |
| Discontinuous Conversion Mode | **Disable** | |
| Overrun Behaviour | **Overrun data overwritten** | 新数据覆盖旧数据 |
| Low Power Auto Wait | **Disable** | |

### 5.3 External Trigger

| 选项 | 值 |
|------|-----|
| Trigger Source | **Timer 2 Trigger Out event** |
| Trigger Edge | **Rising edge** |

### 5.4 Regular Conversion → Rank

| Rank | Channel | Sampling Time |
|------|---------|---------------|
| 1 | Channel 1 (IN1) | **2.5 cycles** |

### 5.5 ADC1 DMA 配置

**DMA Settings** 标签 → 点 **Add** → 选 **ADC1** → OK：

| 参数 | 值 | 说明 |
|------|-----|------|
| Direction | **Peripheral to Memory** | ADC → 内存 |
| Mode | **Circular** | 循环采集 |
| Priority | **Low** | |
| Increment Address (Peripheral) | ☐ **不勾** | ADC 数据寄存器是固定地址 |
| Increment Address (Memory) | ☑ **勾上** | adc_buf[0],[1],[2]... |
| Data Width (Peripheral) | **Half Word** | |
| Data Width (Memory) | **Half Word** | |

> 📌 其余 DMA 选项保持默认。ADC DMA 方向是 Peripheral→Memory，和 DAC DMA 相反。

### 5.6 ADC1 NVIC

| 中断 | 使能 | 优先级 |
|------|------|--------|
| DMA1 channel 1 interrupt | ☑ | 3 |

---

## 6. 定时器配置

### 6.1 TIM6 — DAC 触发定时器

Configuration → Timers → TIM6：

**上方 Mode 区：**

| 选项 | 值 | 说明 |
|------|-----|------|
| Clock Source | **Internal Clock** | 用内部时钟 |

> TIM6 是基本定时器，没有 Channel 选项，比 TIM2 简单。

**下方 Configuration 区（参数）：**

| 参数 | 值 | 说明 |
|------|-----|------|
| Prescaler | **160-1** = 159 | TIM6_CLK = 160MHz / 160 = 1MHz |
| Counter Period | **10-1** = 9 | 1MHz / 10 = **100kHz DAC更新率** |
| Auto-reload preload | **Enable** | |
| Trigger Output (TRGO) | **Update Event** | 每次溢出输出 TRGO 脉冲 |

**确认默认：**

| 参数 | 应有的值 | 说明 |
|------|---------|------|
| Counter Mode | **Up** | 默认 |
| One Pulse Mode | **Disable** | 默认 |

> **DAC更新率 = 100kHz**，即每秒更新100k个点。
> 例：输出 10kHz 正弦 → 100kHz / 10kHz = 10个点/周期
> 例：输出 1kHz 正弦 → 100kHz / 1kHz = 100个点/周期

### 6.2 TIM2 — ADC 触发定时器

Configuration → Timers → TIM2：

**上方 Mode 区（通道模式）：**

| 选项 | 值 | 说明 |
|------|-----|------|
| Clock Source | **Internal Clock** | 用内部时钟 |
| Channel1 ~ Channel4 | 全部 **Disable** | 不需要 PWM/输入捕获，只要 TRGO |

**下方 Configuration 区（参数）：**

**需要改的：**

| 参数 | 值 | 说明 |
|------|-----|------|
| Prescaler | **16-1** = 15 | TIM2_CLK = 160MHz / 16 = 10MHz |
| Counter Period | **40-1** = 39 | 10MHz / 40 = **250kHz 采样率** |
| Auto-reload preload | **Enable** | |
| Trigger Output (TRGO) | **Update Event** | |

**确认默认：** Counter Mode = Up，One Pulse Mode = Disable。

> **采样率 250kHz**：对于 10kHz 输入信号，每周期 25 个采样点。
>
> ⚠️ TIM2 **不开中断**！只输出 TRGO 触发 ADC。

### 6.3 采样率速查

以后改采样率只改 TIM2 的 ARR：

| 采样率 | TIM2 ARR | 能分析的信号频率（奈奎斯特÷2） |
|--------|----------|------------------------------|
| 500kHz | 19 | 0 ~ 250kHz |
| 250kHz | 39 | 0 ~ 125kHz ← 当前 |
| 100kHz | 99 | 0 ~ 50kHz |
| 50kHz | 199 | 0 ~ 25kHz |

---

## 7. NVIC 中断一览

System Core → NVIC：

| 中断 | 使能 | 优先级 | 用途 |
|------|------|--------|------|
| DMA1 channel 1 | ☑ | 3 | ADC DMA 全满 → 通知 CPU 做 FFT |

> 所有未列出的中断保持默认（通常=不使能）。

---

## 8. 引脚总览

| 引脚 | 功能 | 用途 |
|------|------|------|
| PA4 | DAC1_OUT1 | 信号源正弦波 → 示波器CH1 + 杜邦线→PA0 |
| PA5 | DAC1_OUT2 | 同频可控输出 → 示波器CH2 |
| PA0 | ADC1_IN1 | 采集PA4信号（杜邦线短接 PA4↔PA0） |
| PA2 | USART2_TX | 串口 printf 调试 |
| PA3 | USART2_RX | 串口接收（暂不用） |

> 物理接线：**一根杜邦线把 PA4 和 PA0 短接**，DAC 输出就能被 ADC 采到。

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
- 然后进入第5步：写 DAC 波形生成代码
