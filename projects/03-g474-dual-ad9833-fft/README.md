# 双 AD9833 信号采集 + FFT 分析

> STM32G474RET6 控制两颗 AD9833，ADC DMA 双通道采集，CMSIS-DSP FFT 实时分析频率和幅值。

## 目标

- SPI1 总线控制两颗 AD9833，独立输出两路信号
- ADC1 双通道(Timer2 触发) + DMA 循环采集
- 2048 点实数 FFT(CMSIS-DSP) → 频率 + 幅值
- 串口实时输出测量结果

## 硬件

| 组件 | 型号/规格 |
|------|----------|
| MCU | STM32G474RET6 (NUCLEO-G474RE) |
| 时钟 | HSI 16MHz → PLL → 170MHz |
| DDS | AD9833 × 2 (25MHz 晶振模块) |
| 调试 | 板载 ST-LINK/V3 + VCP 串口 |

### 关键引脚

| 引脚 | 功能 | 连接 |
|------|------|------|
| PA5 | SPI1_SCK | 两 AD9833 SCLK 并联 |
| PA7 | SPI1_MOSI | 两 AD9833 SDATA 并联 |
| PA4 | GPIO (FSYNC1) | AD9833 #1 FSYNC |
| PA6 | GPIO (FSYNC2) | AD9833 #2 FSYNC |
| PA0 | ADC1_IN1 | AD9833 #1 VOUT (信号 A) |
| PA1 | ADC1_IN2 | AD9833 #2 VOUT (信号 B) |
| PA2/PA3 | USART2 | ST-LINK VCP → PC |

## 项目文件

```
03-g474-dual-ad9833-fft/
├── README.md                    ← 本文件
├── CubeMX配置指南.md            ← CubeIDE 引脚+时钟+外设配置
├── 集成说明-双AD9833驱动.md      ← main.c 集成 step-by-step
├── Core/
│   ├── Inc/
│   │   └── ad9833.h            ← 双模块驱动头文件
│   └── Src/
│       └── ad9833.c            ← 双模块驱动实现
└── screenshots/                 ← CubeMX 配置截图(建议)
```

## 状态

- [x] CubeMX 配置完成
- [x] 双 AD9833 驱动移植完成
- [ ] 接线 + 烧录验证 (双模块独立出波形)
- [ ] ADC DMA 双缓冲采集
- [ ] CMSIS-DSP FFT + 峰值检测
- [ ] 串口输出结果

## 与相关项目的区别

| | 03-g474-dual-ad9833-fft | 01-f103-ad9833-signal-gen | 02-g474-dac-fft |
|---|---|---|---|
| MCU | STM32G474 | STM32F103 | STM32G474 |
| 信号源 | 外部 AD9833 × 2 | 外部 AD9833 × 1 | 内部 DAC |
| FSYNC | 双 GPIO 独立片选 | 单 GPIO | — |
| 采集 | ADC 双通道 + DMA | 无 | ADC 单通道 + DMA |
| 分析 | FFT (频率+幅值) | 无 | FFT (频率+幅值+相位) |
| 状态 | 🔲 进行中 | ✅ 完成 | 🔲 待开始 |
