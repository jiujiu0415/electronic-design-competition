# STM32G474 双通道 AD9833 + ADC FFT — 配置指南（STM32CubeIDE）

> 目标：用 STM32CubeIDE 生成 HAL 工程，SPI 控制两个 AD9833 出信号，ADC 双通道 DMA 采集，Timer 精确控制采样率。

---

## 0. 新建工程

打开 STM32CubeIDE → **File → New → STM32 Project**：

1. 搜索 `STM32G474RE`
2. 选中 **STM32G474RETx**（你的芯片）
3. 点 Next，填工程名 `dual_ad9833_fft`，点 Finish

> 弹出来的 "Initialize all peripherals with their default Mode?" 选 **Yes**。

CubeIDE 会自动打开 `.ioc` 文件，进入 Pinout & Configuration 界面（集成的 CubeMX）。

---

## 1. 系统核心 & 调试

### 1.1 System Core → SYS

| 选项 | 值 | 说明 |
|------|-----|------|
| Debug | **Serial Wire** | 必须！否则 SWD 会被禁用，烧一次就锁死 |
| Timebase Source | **SysTick** | HAL 时基，默认即可 |

### 1.2 System Core → RCC

| 选项 | 值 |
|------|-----|
| High Speed Clock (HSE) | **Disable**（你的板子没有外部晶振） |
| Low Speed Clock (LSE) | **Disable** |

> 全部用内部 RC 振荡器 HSI（16MHz），简单可靠。

---

## 2. 时钟树配置（Clock Configuration）

目标：**SYSCLK = 170MHz**，AHB/APB 全速。

切到 **Clock Configuration** 标签页，按下面顺序配：

```
① PLL Source Mux      → HSI (16MHz)
② PLLM                → /4       → PLL输入 = 4MHz
③ PLLN                → ×85      → VCO = 340MHz ✓ (96~344 范围内)
④ PLLR                → /2       → SYSCLK = 170MHz ✓

⑤ System Clock Mux    → PLLCLK   → 170MHz
⑥ AHB Prescaler       → /1       → HCLK = 170MHz
⑦ APB1 Prescaler      → /1       → APB1 = 170MHz
⑧ APB2 Prescaler      → /1       → APB2 = 170MHz
```

> PLLP 和 PLLQ 灰着不用管，CubeMX 自动分配。

### 验证

最终数字：

```
SYSCLK:  170 MHz
HCLK:    170 MHz
APB1:    170 MHz
APB2:    170 MHz
```

---

## 3. SPI1 — 控制两个 AD9833

### 3.1 连接思路

```
STM32G474            AD9833 #1          AD9833 #2
PA5 (SPI1_SCK)  ───▶ SCLK      ───▶    SCLK       ← SCK 并联
PA7 (SPI1_MOSI) ───▶ SDATA     ───▶    SDATA      ← MOSI 并联
PA4 (GPIO)      ───▶ FSYNC                        ← 独立片选
PA6 (GPIO)      ──────────────────────▶ FSYNC     ← 独立片选
```

### 3.2 Pinout 视图

在 Pinout 视图里做三件事：

| 操作 | 结果 |
|------|------|
| 选 SPI1 → Mode: **Transmit Only Master** | PA5=SCK, PA7=MOSI 自动分配 |
| SPI1 → Hardware NSS: **Disable** | FSYNC 用 GPIO 软件控制 |
| 右键 PA4 → **GPIO_Output** | FSYNC1 |
| 右键 PA6 → **GPIO_Output** | FSYNC2 |

### 3.3 SPI1 Parameter Settings

切到 Configuration 视图 → Connectivity → SPI1：

| 参数 | 值 | 为什么 |
|------|-----|--------|
| Frame Format | **Motorola** | 标准 SPI |
| Data Size | **16 Bits** | AD9833 命令是 16-bit |
| First Bit | **MSB First** | AD9833 先收高位 |
| Clock Prescaler | **256** | 170MHz ÷ 256 ≈ **664 kHz** |
| CPOL | **High** | 空闲 SCK=1 |
| CPHA | **1 Edge** | 第 1 边沿 = 下降沿 = 采样沿 |
| CRC Calculation | **Disable** | |
| NSSP Mode | **Disable** | |

> **CPOL=High + CPHA=1 Edge = SPI Mode 2。** 推导：CPOL=High 表示空闲高，CPHA=1 Edge 表示第 1 个边沿采样。从高到低的第 1 个边沿是下降沿——正是 AD9833 抓数据的边沿。
>
> 详见 [经验总结-犯错误记录.md](../ad9833-signal-gen/经验总结-犯错误记录.md) Bug #1。

> **SPI 时钟为什么选 664kHz？** 两个 AD9833 并联后 SCK 容性负载变大，保守一点。后续可调到 128 分频（~1.3MHz）。

### 3.4 FSYNC GPIO 配置

System Core → GPIO → 找到 PA4 和 PA6：

| 参数 | PA4 (FSYNC1) | PA6 (FSYNC2) |
|------|-------------|-------------|
| GPIO output level | **High** | **High** |
| GPIO mode | Output Push Pull | Output Push Pull |
| GPIO Pull-up/Pull-down | No pull-up and no pull-down | No pull-up and no pull-down |
| Maximum output speed | **Low** | **Low** |
| User Label | `FSYNC1` | `FSYNC2` |

---

## 4. ADC1 — 双通道采集

### 4.1 Pinout 视图

右键 PA0 → **ADC1_IN1**（接 AD9833 #1 VOUT）
右键 PA1 → **ADC1_IN2**（接 AD9833 #2 VOUT）

两个通道都选 **Single-ended**。AD9833 是单端输出。

### 4.2 ADC1 参数

Configuration 视图 → Analog → ADC1：

| 参数 | 值 | 说明 |
|------|-----|------|
| Clock Prescaler | **PLLP divided by 4** | ADC 时钟 ~5MHz |
| Resolution | **12 bits** | |
| Data Alignment | **Right alignment** | 直接当 uint16 用 |
| Scan Conversion Mode | **灰着不管** | STM32G4 自动根据通道数启用 |
| Continuous Conversion Mode | **Disable** | 由 Timer 触发，不连续跑 |
| Discontinuous Conversion Mode | **Disable** | |
| DMA Continuous Requests | **Enable** | 必须先开！DMA 标签页才能操作 |
| End of Conversion Selection | **End of sequence of conversion** | 两个通道采完才发 DMA |
| Number Of Conversion | **2** | |

### 4.3 Regular Conversion → Rank

配两个通道：

| Rank | Channel | Sampling Time |
|------|---------|---------------|
| 1 | Channel 1 (IN1) | **2.5 cycles** |
| 2 | Channel 2 (IN2) | **2.5 cycles** |

> 5MHz ADC 时钟下 2.5 cycles = 0.5μs。如果 FFT 有杂散，加到 6.5 或 12.5 cycles。

### 4.4 External Trigger

| 选项 | 值 |
|------|-----|
| Trigger Source | **Timer 2 Trigger Out event** |
| Trigger Edge | **Rising edge** |

---

## 5. DMA — ADC 数据自动搬到内存

> ⚠️ **CubeIDE 的关键操作：DMA 必须在 ADC1 自己的 DMA Settings 标签页里添加，不能去 System Core → DMA 那边建！**

### 操作步骤

1. ADC1 面板 → 切到 **DMA Settings** 标签
2. 点 **Add** 按钮
3. 弹出框选 **ADC1** → OK
4. 在出现的行里配置：

| 参数 | 值 |
|------|-----|
| DMA Request | ADC1 |
| Direction | **Peripheral to Memory** |
| Priority | **High** |
| Mode | **Circular** |
| Increment Address (Peripheral) | ☐ 不勾（ADC 数据寄存器地址固定） |
| Increment Address (Memory) | ☑ 勾（buffer 地址递增） |
| Data Width (Peripheral) | **Half Word** |
| Data Width (Memory) | **Half Word** |

> 配好后，System Core → DMA 里会自动出现 ADC1 的条目，不用手动去那边加。

### DMA 中断

切到 ADC1 → **NVIC Settings** 标签：

| 中断 | 使能 |
|------|------|
| DMA1 channel 1 interrupt | ☑ |

用于半满/全满中断通知 CPU 做 FFT。

---

## 6. TIM2 — 精确触发 ADC

### 6.1 为什么用 Timer？

ADC 自由跑的话采样间隔不确定，FFT 算频率会偏。Timer 的 TRGO 每溢出一次触发一次 ADC 扫描，采样率精确到 Hz。

### 6.2 TIM2 配置

Configuration → Timers → TIM2：

| 参数 | 值 | 说明 |
|------|-----|------|
| Prescaler | **170-1** = 169 | TIM2_CLK = 170MHz / 170 = 1MHz |
| Counter Mode | **Up** | |
| Counter Period | **4-1** = 3 | 1MHz / 4 = **250kHz 采样率** |
| Auto-reload preload | **Enable** | |
| Trigger Output (TRGO) | **Update Event** | 每次溢出输出触发脉冲 |

> **TIM2 不用开中断！** 它只负责输出 TRGO 触发 ADC，CPU 不需要响应 Timer 事件。

### 6.3 改采样率速查

以后改采样率只改 ARR（Counter Period）：

| 采样率 | ARR | 能分析的频率范围 |
|--------|-----|-----------------|
| 500kHz | 1 | 0 ~ 250kHz |
| 250kHz | 3 | 0 ~ 125kHz（推荐） |
| 100kHz | 9 | 0 ~ 50kHz |
| 50kHz | 19 | 0 ~ 25kHz |

### 6.4 FFT 频率分辨率

```
分辨率 = 采样率 / FFT点数
例: 250kHz / 2048点 = 122Hz
```

---

## 7. USART2 — 串口输出结果

### 7.1 Pinout

Pinout 视图 → 选 USART2 → Mode: **Asynchronous**。

| 引脚 | 功能 | 去向 |
|------|------|------|
| PA2 | USART2_TX | → ST-LINK VCP → PC |
| PA3 | USART2_RX | ← ST-LINK VCP ← PC |

> 你的 NUCLEO 板上如果 TX/RX 跑到了 PD5/PD6，CubeIDE 会自动分配，不用纠结。

### 7.2 参数

Configuration → Connectivity → USART2：

| 参数 | 值 |
|------|-----|
| Baud Rate | **115200** |
| Word Length | 8 Bits |
| Parity | None |
| Stop Bits | 1 |
| Oversampling | 16 Samples |

---

## 8. NVIC 中断一览

System Core → NVIC：

| 中断 | 使能 | 优先级 | 用途 |
|------|------|--------|------|
| DMA1 channel 1 | ☑ | 3 | DMA 半满/全满 → 通知 CPU 做 FFT |
| USART2 | ☐ | — | 用串口收命令时才开 |
| TIM2 | ☐ | — | 只出 TRGO，不进中断 |
| SysTick | 自动 | 0 | HAL 时基 |

---

## 9. 工程设置

### Project Manager → Project

| 选项 | 值 |
|------|-----|
| Project Name | `dual_ad9833_fft` |
| Project Location | 选你的工作目录 |
| Toolchain / IDE | **STM32CubeIDE**（自动） |

### Project Manager → Code Generator

| 选项 | 值 |
|------|-----|
| Copy only the necessary library files | ☑ |
| Generate peripheral initialization as a pair of .c/.h | ☑ |
| Set all free pins as analog | ☑ |

---

## 10. 生成代码

**Ctrl+S** 保存 `.ioc` → 弹出 "Generate Code?" → **Yes**。

CubeIDE 会自动生成 HAL 工程，包含：
- `Core/Src/main.c` — 主程序
- `Core/Src/stm32g4xx_hal_msp.c` — 外设 MSP 初始化
- `Core/Src/stm32g4xx_it.c` — 中断服务函数
- `Core/Inc/` — 头文件目录

---

## 11. 验证清单

生成代码后，在 CubeIDE 里做以下检查（不用 Keil）：

### 11.1 编译

- [ ] **Project → Build All**（或按 **Ctrl+B**） → **0 Error, 0 Warning**

### 11.2 检查生成的代码

- [ ] `main.c` 里 `SystemClock_Config()` → SYSCLK = 170MHz
- [ ] `MX_SPI1_Init()` → CPOL=High, CPHA=1 Edge, DataSize=16bit
- [ ] `MX_ADC1_Init()` → Scan + DMA + Timer2 Trigger
- [ ] `MX_DMA_Init()` → 包含 ADC1 的 Circular DMA
- [ ] `MX_TIM2_Init()` → TRGO = Update Event, PSC=169, ARR=3
- [ ] PA4、PA6 → GPIO Output, High
- [ ] 没有 `#warning` 占位符

### 11.3 烧录测试

- [ ] USB 线连 NUCLEO 板 → CubeIDE 识别到 ST-LINK
- [ ] **Run → Debug**（或 F11）→ 能烧进去、能 halt 在 main() 第一行
- [ ] **Run → Resume**（F8）→ 程序跑起来不 crash

---

## 12. 下一步

编译通过、烧录正常后：

| 步骤 | 内容 | 产出 |
|------|------|------|
| ① | 移植 AD9833 驱动（双 FSYNC 版本） | 两个模块同时出波形 |
| ② | ADC DMA 双缓冲 + 串口打印原始数据 | 验证波形采集正确 |
| ③ | CMSIS-DSP FFT + 峰值检测 | 算出频率和幅值 |
| ④ | 联调标定 | 完整的测量系统 |

---

> **建议**：`Ctrl+S` 保存 `.ioc` 之前截图 Clock Configuration 页和 Pinout 页。电赛现场重配能省很多时间。
>
> 截图保存路径：`projects/stm32g4-dual-ad9833-fft/screenshots/`
