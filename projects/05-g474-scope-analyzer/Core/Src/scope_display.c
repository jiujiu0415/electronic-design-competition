/**
 * scope_display.c — TJC 串口屏显示模块实现
 *
 * 三层架构:
 *   Layer 1: UART 底层 — 字节发送 (直接寄存器) + 环形缓冲区接收
 *   Layer 2: TJC 协议 — 指令封装 (自动加 0xFF FF FF 结束符)
 *   Layer 3: 业务逻辑 — 下采样 + 数据格式化 + 页面管理
 *
 * 通信: USART2, 115200-8N1, TTL 电平
 * 触摸: printh 模式, 按钮脚本 0x55 帧 → MCU 解析
 */

#include "scope_display.h"
#include "scope_adc.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

/* ============================================================
 * 外部引用
 * ============================================================ */
extern UART_HandleTypeDef huart2;

/* ============================================================
 * Layer 1: 环形缓冲区 (RX) — TJC 官方参考实现
 * ============================================================ */
static uint8_t  ring_buf[SCOPE_DISP_RX_BUF_SIZE];
static uint16_t ring_len = 0;

static void ring_init(void)
{
    ring_len = 0;
}

static void ring_write(uint8_t data)
{
    if (ring_len < SCOPE_DISP_RX_BUF_SIZE)
        ring_buf[ring_len++] = data;
}

static uint8_t ring_read(uint16_t pos)
{
    return ring_buf[pos];
}

static void ring_delete(uint16_t size)
{
    if (size >= ring_len)
    {
        ring_len = 0;
        return;
    }
    memmove(ring_buf, ring_buf + size, ring_len - size);
    ring_len -= size;
}

static uint16_t ring_get_len(void)
{
    return ring_len;
}

/* 辅助宏 (兼容 TJC 官方代码风格) */
#define usize  ring_get_len()
#define u(x)   ring_read((uint16_t)(x))
#define udelete(x) ring_delete((uint16_t)(x))

/* ============================================================
 * Layer 1: UART 字节发送 (直接寄存器, 比 HAL 快)
 * ============================================================ */

static void uart_send_byte(uint8_t b)
{
    /* 等待 TXE (Transmit Data Register Empty) */
    while (!(USART2->ISR & USART_ISR_TXE));
    USART2->TDR = b;
}

static void uart_send_bytes(const uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++)
        uart_send_byte(data[i]);
}

static void uart_send_str(const char *s)
{
    while (*s)
        uart_send_byte((uint8_t)*s++);
}

/* 快速整数→字符串发送 (避免 sprintf 开销, 用于大批量波形数据) */
static void uart_send_int(int32_t val)
{
    if (val < 0)
    {
        uart_send_byte('-');
        val = -val;
    }
    if (val == 0)
    {
        uart_send_byte('0');
        return;
    }
    char tmp[12];
    uint8_t pos = 0;
    while (val > 0)
    {
        tmp[pos++] = (char)('0' + (val % 10));
        val /= 10;
    }
    while (pos > 0)
        uart_send_byte((uint8_t)tmp[--pos]);
}

/* ============================================================
 * Layer 2: TJC 协议封装
 *
 * 所有指令必须以 0xFF 0xFF 0xFF 结尾。
 * ============================================================ */

/* 发送 TJC 结束符 */
static void tjc_send_end(void)
{
    uart_send_byte(0xFF);
    uart_send_byte(0xFF);
    uart_send_byte(0xFF);
}

/**
 * tjc_send_cmd — printf 风格指令发送
 *
 * 用法: tjc_send_cmd("n0.val=%d", 10500);
 * 自动追加 FF FF FF 结束符。
 */
static void tjc_send_cmd(const char *fmt, ...)
{
    char buf[256];
    va_list ap;

    /* 注意: 在裸机 C 中, 第二个参数是最后一个具名参数 */
    /* 使用 __builtin_va_start 兼容 ARM GCC */
    __builtin_va_start(ap, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (len > 0)
        uart_send_bytes((uint8_t *)buf, (uint16_t)len);
    tjc_send_end();
}

/**
 * tjc_set_val — 设置数字组件整数值
 *
 * 等价于: n2.val=10500 + FF FF FF
 * 用于 f₁ (Hz, 500Hz 吸附后精确整数)
 */
static void tjc_set_val(const char *comp, int32_t val)
{
    uart_send_str(comp);
    uart_send_str(".val=");
    uart_send_int(val);
    tjc_send_end();
}

/**
 * tjc_set_val_float — 设置数字组件浮点值 (1位小数)
 *
 * 等价于: n0.val=150.2 + FF FF FF
 * Vpp/Vrms/谐波幅值均用此函数, 精度 ±0.1mV, 满足赛题 ≤5mV 误差要求。
 */
static void tjc_set_val_float(const char *comp, float val)
{
    uart_send_str(comp);
    uart_send_str(".val=");

    /* 格式化为 1 位小数 (如 52.8, 0.5, 120.0) */
    char buf[16];
    int len = snprintf(buf, sizeof(buf), "%.1f", (double)val);
    if (len > 0)
        uart_send_bytes((uint8_t *)buf, (uint16_t)len);

    tjc_send_end();
}

/**
 * tjc_set_txt — 设置文本组件值
 *
 * 等价于: t0.txt="Hello" + FF FF FF
 */
static void tjc_set_txt(const char *comp, const char *txt)
{
    uart_send_str(comp);
    uart_send_str(".txt=\"");
    uart_send_str(txt);
    uart_send_byte('"');
    tjc_send_end();
}

/**
 * tjc_clear_wave — 清除波形通道
 *
 * 等价于: cle 0,0 + FF FF FF
 */
static void tjc_clear_wave(uint8_t channel)
{
    tjc_send_cmd("cle %d,0", channel);
}

/**
 * tjc_switch_page — 切换页面
 *
 * 等价于: page 1 + FF FF FF
 */
static void tjc_switch_page(uint8_t page)
{
    tjc_send_cmd("page %d", page);
}

/**
 * tjc_addt_begin — 开始批量波形数据传输
 *
 * 发送 addt 指令前缀: "addt channel,N,"
 * 调用后逐点发送数据, 最后调用 tjc_addt_end() 发送结束符。
 *
 * 这避免了先构建完整字符串再发送的内存开销。
 */
static void tjc_addt_begin(uint8_t channel, uint16_t count)
{
    uart_send_str("addt ");
    uart_send_int((int32_t)channel);
    uart_send_byte(',');
    uart_send_int((int32_t)count);
    uart_send_byte(',');
}

/**
 * tjc_addt_point — 发送一个波形数据点 (Y 值)
 *
 * 在 addt_begin 和 addt_end 之间调用。
 * 自动追加逗号分隔符 (第一个点也加, 不影响解析)。
 */
static void tjc_addt_point(int32_t y)
{
    uart_send_int(y);
    uart_send_byte(',');
}

/**
 * tjc_addt_end — 结束批量波形数据并发送
 *
 * 发送 0xFF 0xFF 0xFF 结束符。
 * addt_begin → N 次 addt_point → addt_end 组成一次完整 addt。
 */
static void tjc_addt_end(void)
{
    tjc_send_end();
}

/* ============================================================
 * Layer 2: 触摸事件解析
 *
 * printh 模式, 按钮脚本发 7 字节帧:
 *   0x55 | page_id | component_id | event_type | 0xFF | 0xFF | 0xFF
 *
 * event_type: 0x01 = 按下, 0x00 = 释放
 * ============================================================ */

#define TOUCH_FRAME_LEN  7

/**
 * touch_parse — 解析并处理一个触摸帧
 *
 * @return 1 = 成功解析, 0 = 数据不足或帧头不匹配
 */
static uint8_t touch_parse(void)
{
    if (usize < TOUCH_FRAME_LEN)
        return 0;

    /* 检查帧头 + 帧尾 */
    if (u(0) == 0x55
        && u(4) == 0xFF && u(5) == 0xFF && u(6) == 0xFF)
    {
        uint8_t page_id      = u(1);
        uint8_t component_id = u(2);
        uint8_t event_type   = u(3);

        /* 只处理按下事件 (忽略释放) */
        if (event_type == TOUCH_EVT_PRESS)
        {
            /* ── Page 0 按钮 ── */
            if (page_id == 0)
            {
                if (component_id == 0)  /* b0: 切换 1/3 周期 */
                {
                    /* 由 ScopeDisplay_GetMode() 查询状态, 主循环处理 */
                }
                else if (component_id == 1)  /* b1: 切换到频谱页 */
                {
                    tjc_switch_page(1);
                }
            }
            /* ── Page 1 按钮 ── */
            else if (page_id == 1)
            {
                if (component_id == 0)  /* b0: 返回波形页 */
                {
                    tjc_switch_page(0);
                }
            }
        }

        udelete(TOUCH_FRAME_LEN);
        return 1;
    }

    /* 帧头不匹配 → 丢弃 1 字节, 重新对齐 */
    udelete(1);
    return 0;
}

/* ============================================================
 * Layer 3: 波形下采样 — Min-Max 分箱
 *
 * 4096 采样点 → 700 显示像素 (x2 = 1400 点: min+max 交替, 横屏)
 *
 * 每个像素覆盖 ≈10 个采样点, 取 bin 内 min 和 max,
 * 用 addt 交替发送, 屏幕上形成垂直细线 (DSO 标准做法)。
 *
 * Y 轴映射: ADC raw code → 波形控件像素坐标
 *   y_pixel = CENTER - (ac_code * SCALE)
 *
 * 输入: signal_buf[4096] — ADC 原始 uint16_t
 * 输出: 通过 tjc_addt_point 发送到 s0 通道
 * ============================================================ */

/* Y 轴缩放: ADC 满量程 4096 → 波形像素高度 */
#define WAVE_PIXEL_RANGE  ((SCOPE_DISP_WAVE_Y_MAX) - (SCOPE_DISP_WAVE_Y_MIN))
#define WAVE_SCALE        ((float)(WAVE_PIXEL_RANGE) / 4096.0f)

static void waveform_downsample_and_send(const uint16_t *signal_buf,
                                          float f1_hz, int n_cycles)
{
    const uint16_t N_SRC  = SCOPE_ADC_SIGNAL_BUF_SIZE;  /* 4096 */
    const uint16_t N_DST  = SCOPE_DISP_WAVE_WIDTH;      /* 横屏 700 */

    /* ── 计算周期截取: 1周期 = Fs/f₁ 个采样点 ── */
    float    fs_hz    = SCOPE_ADC_SAMPLE_RATE;  /* 2,000,000 */
    uint16_t per_samp = (uint16_t)(fs_hz / f1_hz + 0.5f);
    if (per_samp < 4)  per_samp = 4;            /* 500kHz→4点, 至少4点 */
    if (per_samp > N_SRC) per_samp = N_SRC;

    uint16_t n_extract = per_samp * n_cycles;
    if (n_extract > N_SRC) n_extract = N_SRC;
    if (n_extract < N_DST / 2) n_extract = N_DST / 2;  /* 低频补偿 */

    /* ── 计算 DC 均值 (仅截取段内, AC 耦合) ── */
    uint32_t sum = 0;
    for (uint16_t i = 0; i < n_extract; i++)
        sum += signal_buf[i];
    float dc_code = (float)sum / (float)n_extract;

    /* ── 计算每个显示像素的 min/max ── */
    static uint16_t wave_pixels[SCOPE_DISP_WAVE_WIDTH * 2];  /* [min0, max0, min1, max1, ...] */

    float bin_size = (float)n_extract / (float)N_DST;

    for (uint16_t px = 0; px < N_DST; px++)
    {
        uint16_t i_start = (uint16_t)((float)px * bin_size);
        uint16_t i_end   = (uint16_t)((float)(px + 1) * bin_size);
        if (i_end > n_extract) i_end = n_extract;

        uint16_t smin = 4095, smax = 0;
        for (uint16_t i = i_start; i < i_end; i++)
        {
            uint16_t v = signal_buf[i];
            if (v < smin) smin = v;
            if (v > smax) smax = v;
        }

        /* AC 耦合 + Y 轴映射: y = CENTER - (code - dc) * SCALE */
        float   ac_min = (float)((int32_t)smin - (int32_t)dc_code);
        float   ac_max = (float)((int32_t)smax - (int32_t)dc_code);
        int32_t y_min  = (int32_t)(SCOPE_DISP_WAVE_Y_CENTER - ac_max * WAVE_SCALE);  /* ac_max → 顶部 */
        int32_t y_max  = (int32_t)(SCOPE_DISP_WAVE_Y_CENTER - ac_min * WAVE_SCALE);  /* ac_min → 底部 */

        /* 钳位到控件范围内 */
        if (y_min < 0) y_min = 0;
        if (y_max > SCOPE_DISP_WAVE_HEIGHT) y_max = SCOPE_DISP_WAVE_HEIGHT;

        wave_pixels[px * 2]     = (uint16_t)y_min;  /* 注意: min ac → max pixel (上) */
        wave_pixels[px * 2 + 1] = (uint16_t)y_max;  /*       max ac → min pixel (下) */

        /* 交换确保 min < max */
        if (wave_pixels[px * 2] > wave_pixels[px * 2 + 1])
        {
            uint16_t t = wave_pixels[px * 2];
            wave_pixels[px * 2]     = wave_pixels[px * 2 + 1];
            wave_pixels[px * 2 + 1] = t;
        }
    }

    /* ── 清除旧波形 + 批量发送 ── */
    tjc_clear_wave(0);
    tjc_addt_begin(0, N_DST * 2);
    for (uint16_t i = 0; i < N_DST * 2; i++)
        tjc_addt_point((int32_t)wave_pixels[i]);
    tjc_addt_end();
}

/* ============================================================
 * Layer 3: 频谱下采样 — Max-Per-Group
 *
 * fft_mag[2..1024] (≈1023 bins) → 600 显示像素 (横屏)
 *
 * 每组取最大值 (保留窄谱线特征)。
 * bin 0(DC) 和 bin 1(直流泄漏区) 跳过。
 * 频谱图用 s1 通道, 柱状图效果。
 *
 * ============================================================ */

#define SPEC_BINS_SHOWN   1024   /* 显示 bin 2..1024 范围 */
#define SPEC_SKIP_BINS       2   /* 跳过 DC 区域 */

static void spectrum_downsample_and_send(const float *fft_mag, uint16_t mag_size)
{
    const uint16_t N_DST = SCOPE_DISP_SPEC_WIDTH;  /* 横屏 600 */
    (void)mag_size;  /* 固定为 2049, 参数保留用于校验 */

    /* ── 下采样: max-per-group ── */
    static uint16_t spec_pixels[SCOPE_DISP_SPEC_WIDTH];

    uint16_t usable_bins = SPEC_BINS_SHOWN - SPEC_SKIP_BINS;  /* 1022 */
    float    group_size  = (float)usable_bins / (float)N_DST;

    /* 先找最大幅值 (用于归一化到像素高度) */
    float max_mag = 0.0f;
    for (uint16_t i = SPEC_SKIP_BINS; i < SPEC_BINS_SHOWN; i++)
        if (fft_mag[i] > max_mag) max_mag = fft_mag[i];

    if (max_mag < 1e-9f) max_mag = 1.0f;  /* 防除零 */

    for (uint16_t px = 0; px < N_DST; px++)
    {
        uint16_t i_start = SPEC_SKIP_BINS + (uint16_t)((float)px * group_size);
        uint16_t i_end   = SPEC_SKIP_BINS + (uint16_t)((float)(px + 1) * group_size);
        if (i_end > SPEC_BINS_SHOWN) i_end = SPEC_BINS_SHOWN;

        float group_max = 0.0f;
        for (uint16_t i = i_start; i < i_end; i++)
            if (fft_mag[i] > group_max) group_max = fft_mag[i];

        /* 归一化 → 像素高度 (柱从底部向上) */
        int32_t bar_h = (int32_t)(group_max / max_mag * (float)SCOPE_DISP_SPEC_HEIGHT);
        if (bar_h < 0) bar_h = 0;
        if (bar_h > SCOPE_DISP_SPEC_HEIGHT) bar_h = SCOPE_DISP_SPEC_HEIGHT;

        /* Y 轴翻转: 底部=屏幕下方, 顶部=屏幕上方
         * 对于频谱柱状图: y = HEIGHT - bar_h (柱从底部向上) */
        spec_pixels[px] = (uint16_t)(SCOPE_DISP_SPEC_HEIGHT - bar_h);
    }

    /* ── 清除旧频谱 + 批量发送 ── */
    tjc_clear_wave(1);
    tjc_addt_begin(1, N_DST);
    for (uint16_t i = 0; i < N_DST; i++)
        tjc_addt_point((int32_t)spec_pixels[i]);
    tjc_addt_end();
}

/* ============================================================
 * Layer 3: 参数更新
 * ============================================================ */

static void params_update_page0(const ScopeResult *r)
{
    /* n0: Vpp (mV) — 赛题 1-(2), 1位小数 */
    tjc_set_val_float("n0", r->vpp_mV);

    /* n1: Vrms (mV) — 赛题 1-(2), 1位小数 */
    tjc_set_val_float("n1", r->vrms_mV);

    /* n2: f₁ (Hz) — 赛题 1-(2), 500Hz 吸附后精确 */
    tjc_set_val_float("n2", r->f1_hz);

    /* n3 已移除 — 赛题不要求单独显示基波 Vpeak */
}

static void params_update_page1(const ScopeResult *r)
{
    char buf[32];

    /* ── 基波 (始终显示) ── */
    tjc_set_txt("t9",  "H1 (基波)");
    snprintf(buf, sizeof(buf), "%.1f Hz", (double)r->f1_hz);
    tjc_set_txt("t10", buf);
    snprintf(buf, sizeof(buf), "%.1f mVpk", (double)r->fund_vpeak_mV);
    tjc_set_txt("t11", buf);

    /* ── 谐波1 (按检出顺序, 可能是 H2/H3/H5...) ── */
    if (r->harmonic_count >= 1)
    {
        int order = (int)(r->harmonics[0].freq_hz / r->f1_hz + 0.5f);
        snprintf(buf, sizeof(buf), "H%d (谐波%d)", order, 1);
        tjc_set_txt("t12", buf);
        snprintf(buf, sizeof(buf), "%.1f Hz", (double)r->harmonics[0].freq_hz);
        tjc_set_txt("t13", buf);
        snprintf(buf, sizeof(buf), "%.1f mVpk", (double)r->harmonics[0].vpeak_mV);
        tjc_set_txt("t14", buf);
    }
    else
    {
        tjc_set_txt("t12", "—");
        tjc_set_txt("t13", "");
        tjc_set_txt("t14", "");
    }

    /* ── 谐波2 (按检出顺序) ── */
    if (r->harmonic_count >= 2)
    {
        int order = (int)(r->harmonics[1].freq_hz / r->f1_hz + 0.5f);
        snprintf(buf, sizeof(buf), "H%d (谐波%d)", order, 2);
        tjc_set_txt("t15", buf);
        snprintf(buf, sizeof(buf), "%.1f Hz", (double)r->harmonics[1].freq_hz);
        tjc_set_txt("t16", buf);
        snprintf(buf, sizeof(buf), "%.1f mVpk", (double)r->harmonics[1].vpeak_mV);
        tjc_set_txt("t17", buf);
    }
    else
    {
        tjc_set_txt("t15", "—");
        tjc_set_txt("t16", "");
        tjc_set_txt("t17", "");
    }
}

/* ============================================================
 * Layer 3: 业务逻辑
 * ============================================================ */

/* 当前显示状态 */
static ScopeDisplayMode  disp_mode      = DISP_MODE_1CYCLE;
static uint8_t           disp_curr_page = 0;
static uint8_t           disp_inited    = 0;

/* 存储最近的测量结果 (用于页面切换时重绘) */
static ScopeResult  cached_result;
static uint8_t      cached_valid = 0;

/* 信号波形拷贝 (用于 1/3 周期切换重绘, 防止 DMA 覆盖) */
static uint16_t cached_signal[SCOPE_ADC_SIGNAL_BUF_SIZE];
static uint8_t  cached_signal_valid = 0;

/* ============================================================
 * Public API 实现
 * ============================================================ */

/**
 * ScopeDisplay_Init — 初始化屏幕通信
 */
void ScopeDisplay_Init(void)
{
    ring_init();

    /* 等待屏幕启动 (上电到可接收指令约需 500ms) */
    HAL_Delay(500);

    /* 切换到主页 (波形页) — printh 模式不需要 bkcmd */
    tjc_switch_page(0);
    HAL_Delay(100);

    disp_inited    = 1;
    disp_curr_page = 0;
    cached_valid   = 0;

    /* 启动 UART RX 中断 — 单字节接收 */
    static uint8_t rx_byte;
    HAL_UART_Receive_IT(&huart2, &rx_byte, 1);
}

/**
 * ScopeDisplay_Update — 更新屏幕全部内容
 */
void ScopeDisplay_Update(const ScopeResult *r, const uint16_t *signal_buf)
{
    if (!disp_inited || r == NULL || signal_buf == NULL)
        return;

    /* ── 缓存结果 + 信号 (1/3周期切换重绘用) ── */
    memcpy(&cached_result, r, sizeof(ScopeResult));
    cached_valid = 1;
    memcpy(cached_signal, signal_buf, SCOPE_ADC_SIGNAL_BUF_SIZE * sizeof(uint16_t));
    cached_signal_valid = 1;

    /* ── Page 0: 参数 + 波形 (按当前 1/3 周期模式) ── */
    int cycles = (disp_mode == DISP_MODE_1CYCLE) ? 1 : 3;
    params_update_page0(r);
    waveform_downsample_and_send(signal_buf, r->f1_hz, cycles);

    /* ── Page 1: 谐波参数 + 频谱 ── */
    params_update_page1(r);

    const float *mag_buf = ScopeFFT_GetMagBuffer();
    uint16_t     mag_sz  = ScopeFFT_GetMagSize();
    if (mag_buf != NULL)
        spectrum_downsample_and_send(mag_buf, mag_sz);

    disp_curr_page = 0;  /* 更新后默认停留在 Page 0 */
}

/**
 * ScopeDisplay_ProcessTouch — 处理触摸事件 (主循环轮询)
 */
void ScopeDisplay_ProcessTouch(void)
{
    /* 持续解析直到数据不足或帧头不匹配 */
    while (usize >= TOUCH_FRAME_LEN)
    {
        if (u(0) == 0x55
            && u(4) == 0xFF && u(5) == 0xFF && u(6) == 0xFF)
        {
            uint8_t page_id      = u(1);
            uint8_t component_id = u(2);
            uint8_t event_type   = u(3);

            if (event_type == TOUCH_EVT_PRESS)
            {
                if (page_id == 0)
                {
                    if (component_id == 0)  /* b0: 1/3 周期切换 */
                    {
                        if (disp_mode == DISP_MODE_1CYCLE)
                        {
                            disp_mode = DISP_MODE_3CYCLE;
                            tjc_set_txt("t4", "3周期");
                        }
                        else
                        {
                            disp_mode = DISP_MODE_1CYCLE;
                            tjc_set_txt("t4", "1周期");
                        }

                        /* 模式切换后重绘波形 (用缓存的信号+频率) */
                        if (cached_valid && cached_signal_valid)
                        {
                            int cycles = (disp_mode == DISP_MODE_1CYCLE) ? 1 : 3;
                            waveform_downsample_and_send(cached_signal,
                                                         cached_result.f1_hz, cycles);
                        }
                    }
                    else if (component_id == 1)  /* b1: 频谱页 */
                    {
                        disp_curr_page = 1;
                    }
                }
                else if (page_id == 1)
                {
                    if (component_id == 0)  /* b0: 返回波形页 */
                    {
                        disp_curr_page = 0;
                    }
                }
            }

            udelete(TOUCH_FRAME_LEN);
        }
        else
        {
            /* 帧头不对齐 → 跳过 1 字节重新同步 */
            udelete(1);
        }
    }
}

/**
 * ScopeDisplay_IRQHandler — USART2 RX 中断回调
 *
 * 在 HAL_UART_RxCpltCallback 中调用。
 */
void ScopeDisplay_IRQHandler(uint8_t byte)
{
    ring_write(byte);
}

/**
 * ScopeDisplay_GetMode — 获取当前 1/3 周期模式
 */
ScopeDisplayMode ScopeDisplay_GetMode(void)
{
    return disp_mode;
}
