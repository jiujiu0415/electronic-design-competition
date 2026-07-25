# STM32G4 DAC-ADC-FFT 同频信号输出

> 任务二 第1题：内部DAC自闭环方案

## 目标

- DAC通道1产生正弦波（模拟"外部信号源"）
- ADC采集 → FFT分析得到频率、幅度、相位
- DAC通道2输出同频正弦波，幅度和相位可调
- 两路信号在示波器上同频稳定显示

## 硬件

- 芯片：STM32G474RET6
- 时钟：内部HSI → PLL → 160MHz
- 引脚：
  - PA4 = DAC1_OUT1（信号源输出）
  - PA5 = DAC1_OUT2（同频可控输出）
  - PA0 = ADC1_IN1（采集PA4信号）
  - PA2/PA3 = USART2（串口调试）

## 目录结构

```
stm32g4-dac-adc-fft/
├── README.md               ← 本文件
├── docs/                   ← 参考资料、笔记
│   └── STM32数字信号处理_v1.1.pdf
├── Core/
│   ├── Inc/                ← 用户头文件
│   └── Src/                ← 用户源文件
└── CubeMX配置指南.md        ← CubeMX配置步骤
```

## 实施步骤

1. ✅ 引脚确认
2. 🔲 CubeMX 新建工程 + 基础配置
3. 🔲 配置 DAC（TIM+DMA 波形输出）
4. 🔲 配置 ADC（TIM+DMA 采集）
5. 🔲 写代码：DAC波形生成
6. 🔲 写代码：ADC采集 + FFT分析
7. 🔲 写代码：根据FFT结果控制DAC2
8. 🔲 烧录验证

## 与 stm32g4-dual-ad9833-fft 的区别

| | stm32g4-dual-ad9833-fft | stm32g4-dac-adc-fft |
|---|---|---|
| 信号源 | 外部AD9833 DDS芯片 | 内部DAC |
| 信号采集 | ADC双通道 | ADC单/双通道 |
| 核心外设 | SPI + ADC | DAC + ADC |
| 用途 | DDS信号发生器 + 频谱分析 | 同频信号跟踪输出 |
