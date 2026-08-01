/**
 * scope_display.h — TJC 串口屏显示模块 (USART HMI)
 *
 * 通信: USART2, 115200 bps, 8N1, TTL 电平
 * 屏幕: TJC8048X570_011C (7" 480×800 竖屏)
 * 协议: 指令以 0xFF 0xFF 0xFF 结尾, 触摸通过 printh 0x55 帧
 *
 * 页面:
 *   Page 0 — 时域波形 + Vpp/Vrms/f₁ (赛题 1-(2))
 *   Page 1 — 频谱定性 + 频率分量幅值 (赛题 1-(3)(4))
 *
 * 用法:
 *   ScopeDisplay_Init();                          // 启动时调用一次
 *   ScopeDisplay_Update(&result, signal_buf);     // 每次测量完成后调用
 *   ScopeDisplay_ProcessTouch();                  // 主循环中轮询
 *   // USART2 RX 中断回调中: ScopeDisplay_IRQHandler(byte);
 *
 * 依赖: scope_fft.h (ScopeResult, ScopeFFT_GetMagBuffer), scope_adc.h
 */

#ifndef __SCOPE_DISPLAY_H
#define __SCOPE_DISPLAY_H

#include "scope_fft.h"
#include "stm32g4xx_hal.h"

/* ============================================================
 * 编译开关: 1 = TJC 串口屏模式, 0 = 传统 UART 调试模式
 * ============================================================ */
#define SCOPE_USE_DISPLAY  1

/* ============================================================
 * 显示配置
 * ============================================================ */

/* 波形控件尺寸 (像素, 在 USART HMI 软件中设定)
 * 横屏 800×480: 波形宽 700 高 280, 频谱宽 550 高 300 */
#define SCOPE_DISP_WAVE_WIDTH     700    /* s0 控件宽度 */
#define SCOPE_DISP_WAVE_HEIGHT    280    /* s0 控件高度 */
#define SCOPE_DISP_SPEC_WIDTH     600    /* s1 控件宽度 (横屏, 右侧留 180px 给谐波列表) */
#define SCOPE_DISP_SPEC_HEIGHT    320    /* s1 控件高度 */

/* 波形显示 Y 轴范围 (像素, 控件坐标系) */
#define SCOPE_DISP_WAVE_Y_MIN      20    /* 顶部留白 */
#define SCOPE_DISP_WAVE_Y_MAX     260    /* 底部留白 */
#define SCOPE_DISP_WAVE_Y_CENTER  140    /* 零点 (AC 耦合) */

/* UART 配置 */
#define SCOPE_DISP_BAUD          115200  /* TJC 默认波特率 */

/* 环形缓冲区 */
#define SCOPE_DISP_RX_BUF_SIZE     500   /* TJC 官方推荐 */

/* ============================================================
 * 显示模式
 * ============================================================ */
typedef enum {
    DISP_MODE_1CYCLE = 0,   /* 显示 1 个完整周期 */
    DISP_MODE_3CYCLE = 1,   /* 显示 3 个完整周期 */
} ScopeDisplayMode;

/* ============================================================
 * 触摸事件
 * ============================================================ */
typedef enum {
    TOUCH_EVT_PRESS   = 0x01,
    TOUCH_EVT_RELEASE = 0x00,
} ScopeTouchEvent;

/* ============================================================
 * API
 * ============================================================ */

/**
 * ScopeDisplay_Init — 初始化屏幕通信
 *
 * 发送 page 0 (默认波形页)。触摸通过 printh 脚本处理。
 * 在 main() 初始化阶段调用一次。
 */
void ScopeDisplay_Init(void);

/**
 * ScopeDisplay_Update — 更新屏幕全部显示内容
 *
 * 测量完成后调用一次，发送:
 *   ① 参数 n0=Vpp, n1=Vrms, n2=f₁ (平均值, mV/Hz)
 *   ② 时域波形 (1或3周期, Min-Max下采样 700px → 1400点 addt)
 *   ③ 频谱柱状图 (Max-per-group 下采样 600px)
 *   ④ 谐波列表 t9~t17 (动态检出, 含实际谐波次数)
 *
 * 内部缓存 signal_buf 用于 1/3 周期切换重绘。
 *
 * @param r          ScopeFFT_Analyze 的结果 (mV 原始域, 平均值已代入)
 * @param signal_buf ADC 原始数据副本 uint16_t[4096] (不会被 DMA 覆盖)
 */
void ScopeDisplay_Update(const ScopeResult *r, const uint16_t *signal_buf);

/**
 * ScopeDisplay_ProcessTouch — 处理收到的触摸事件
 *
 * 在主循环中轮询调用。解析环形缓冲区中的 0x55 帧 (printh),
 * 分发到: 页面切换 (b1=频谱页), 周期模式切换 (b0=1/3周期)
 */
void ScopeDisplay_ProcessTouch(void);

/**
 * ScopeDisplay_IRQHandler — USART2 RX 中断回调入口
 *
 * 在 HAL_UART_RxCpltCallback 中调用, 将收到的字节写入环形缓冲区。
 *
 * @param byte  收到的单字节数据
 */
void ScopeDisplay_IRQHandler(uint8_t byte);

/**
 * ScopeDisplay_GetMode — 获取当前显示模式
 *
 * @return DISP_MODE_1CYCLE 或 DISP_MODE_3CYCLE
 */
ScopeDisplayMode ScopeDisplay_GetMode(void);

#endif /* __SCOPE_DISPLAY_H */
