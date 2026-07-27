# LCR 网络频率响应分析仪（扫频仪）

> 任务二 第2题：利用 DDS 扫频 + ADC 双通道采集 + FFT → 绘制幅频/相频曲线
> 芯片：STM32G474RET6 | IDE：STM32CubeIDE

---

## 目标

- AD9833 DDS 输出扫频正弦信号（如 100Hz ~ 100kHz）
- 信号经过 LCR 被测网络
- ADC1 双通道同步采集：CH1=输入（参考），CH2=输出（响应）
- CMSIS-DSP FFT 分析 → 计算幅度比 + 相位差 → H(jω)
- ST7789 2.4" TFT 屏幕绘制：幅频曲线 + 相频曲线（Bode 图）

## 系统框图

```
                    ┌──────────────┐
  SPI1 ────▶  AD9833 ──▶  LCR 网络 ──▶ ADC1_CH2 (输出)
                    │                    │
                    └──────────────────▶ ADC1_CH1 (输入参考)
                                               │
                                          DMA 同步采集
                                               │
                                    CMSIS-DSP FFT × 2
                                               │
                                H(jω) = Vout/Vin (幅度比+相位差)
                                               │
                              SPI2 ──▶  TFT 屏幕画 Bode 图
```

## 硬件

| 组件 | 型号 | 连接 |
|------|------|------|
| MCU | STM32G474RET6 (NUCLEO-G474RE) | — |
| DDS | AD9833 模块 (25MHz 晶振) | SPI1 |
| 屏幕 | 2.4" TFT LCD (ST7789) | SPI2 |
| 被测网络 | LCR 电路（外接） | 面包板搭建 |
| 调试 | ST-LINK/V3 + VCP 串口 | USART2 |

## 引脚分配

| 引脚 | 功能 | 连接 |
|------|------|------|
| **SPI1 — AD9833** | | |
| PA5 | SPI1_SCK | AD9833 SCLK |
| PA7 | SPI1_MOSI | AD9833 SDATA |
| PA4 | GPIO OUT | AD9833 FSYNC (片选) |
| **SPI2 — TFT LCD** | | |
| PB13 | SPI2_SCK | TFT SC (时钟) |
| PB15 | SPI2_MOSI | TFT ADS (数据) |
| PB14 | SPI2_MISO | TFT ODS (读回, 可选) |
| PB1 | GPIO OUT | TFT LCS (片选) |
| PB0 | GPIO OUT | TFT CD (命令/数据) |
| PA8 | GPIO OUT | TFT TSR (复位) |
| — | — | TFT A (背光) → 3.3V 或 PWM |
| **ADC1 双通道** | | |
| PA0 | ADC1_IN1 | LCR 输入参考信号 |
| PA1 | ADC1_IN2 | LCR 输出响应信号 |
| **TIM2** | | ADC 触发源 (TRGO) |
| **USART2** | | |
| PA2/PA3 | USART2 TX/RX | 串口调试 |

## 测量原理

```
每个频率点 f:
  1. AD9833 输出 sin(2πft)
  2. TIM2 触发 ADC 双通道同步采样
  3. DMA 搬运 2048 点到缓冲区
  4. 对 CH1(输入) 和 CH2(输出) 分别做 FFT
  5. 提取基频分量:
     |Vin| = CH1 在 f 处的幅度
     |Vout| = CH2 在 f 处的幅度
     ∠Vin = CH1 在 f 处的相位
     ∠Vout = CH2 在 f 处的相位
  6. 传输函数:
     增益(dB) = 20 × log10(|Vout| / |Vin|)
     相位差(°) = ∠Vout - ∠Vin
  7. 在屏幕上画一个点 (f, 增益) 和 (f, 相位差)
  8. 换下一个频率 f + Δf，重复
```

## 目录结构

```
projects/04-g474-lcr-sweep-fft/
├── README.md                       ← 本文件
├── CubeMX配置指南.md                ← 一步一步配置截图
├── Core/
│   ├── Inc/
│   │   ├── ad9833.h               ← DDS 驱动 (从 03 项目复制)
│   │   ├── adc_fft.h              ← ADC 双通道 + FFT (改自 02 项目)
│   │   ├── lcd.h                  ← TFT LCD 驱动 (新建)
│   │   └── lcr_sweep.h            ← 扫频主控 (新建)
│   └── Src/
│       ├── ad9833.c
│       ├── adc_fft.c
│       ├── lcd.c
│       └── lcr_sweep.c
├── docs/
│   ├── ST7789屏幕参考资料.md        ← 屏幕芯片手册提炼
│   └── LCR网络基础.md              ← LCR 基本概念
└── screenshots/                    ← CubeMX 配置截图
```

## 实施步骤

| 步骤 | 内容 | 状态 |
|------|------|------|
| 1 | 确认硬件 + 整理参考资料 | ✅ |
| 2 | CubeMX 新建工程 + 外设配置 | 🔲 |
| 3 | AD9833 驱动移植 + 基本输出验证 | 🔲 |
| 4 | TFT LCD 驱动编写 + 显示验证 | 🔲 |
| 5 | ADC 双通道 DMA 采集 + FFT 验证 | 🔲 |
| 6 | 扫频逻辑 + 数据存储 | 🔲 |
| 7 | 屏幕绘制坐标系 + Bode 曲线 | 🔲 |
| 8 | 完整联调 | 🔲 |

## 扫频参数 (初步)

| 参数 | 值 |
|------|-----|
| 起始频率 | 100 Hz |
| 终止频率 | 100 kHz |
| 扫描点数 | 50~100 点 |
| 步进方式 | 对数 (log) 步进 |
| 每点稳定时间 | ~10ms (等 DDS+ADC 稳定) |
| 预计总时间 | ~1~2 秒 |

## 与已有项目的关系

| | 02-g474-dac-fft | 03-g474-dual-ad9833 | **04-g474-lcr-sweep-fft** |
|---|---|---|---|
| 信号源 | 内部 DAC | 外部 AD9833 × 2 | 外部 AD9833 × 1 |
| 采集 | ADC 单通道 | ADC 双通道 | **ADC 双通道同步** |
| 分析 | FFT 幅度+相位 | FFT 幅度 | **FFT 幅度比+相位差** |
| 显示 | 串口打印 | 串口打印 | **TFT 屏幕曲线** |
| 特色 | 同频跟踪输出 | SPI 双模块 | **扫频 Bode 图** |
