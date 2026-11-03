/* AHT21B 温湿度 -> 1.44 寸彩色 TFT (ST7735 128x128) 显示
 *
 * 两块设备各自独立，互不冲突：
 *   AHT21B (I2C)   SCL=PB7  SDA=PB6   VCC=3.3V
 *   TFT    (SPI)   SC=PB13  DI=PB15  RS=PB14  CS=PB12  RST=PB10  BLK=3.3V
 *
 * 屏幕布局（128x128）：
 *   ┌──────────────────┐
 *   │  ENV MONITOR     │  小字标题
 *   │  25.3 C          │  大字（3 倍），橙色=暖 青色=冷
 *   │  48.7 %          │  大字（3 倍），青色
 *   │  ███████████░░░  │  湿度条
 *   │  COMFORT         │  状态
 *   └──────────────────┘
 *
 * 编译开关：
 *   -DTFT_XOFF=2 -DTFT_YOFF=1   面板偏移（1.44 默认）
 *   -DTFT_INVERT=1             颜色反相
 */
#include <stdint.h>
#include "font5x7.h"

#ifndef TFT_XOFF
#define TFT_XOFF 2
#endif
#ifndef TFT_YOFF
#define TFT_YOFF 1
#endif
#ifndef TFT_INVERT
#define TFT_INVERT 0
#endif

#define TFT_W 128u
#define TFT_H 128u

#define AHT_SCL   7u
#define AHT_SDA   6u
#define AHT_ADDR  0x38u

/* TFT 引脚 */
#define PIN_SCK   13u
#define PIN_MOSI  15u
#define PIN_CS    12u
#define PIN_DC    14u
#define PIN_RST   10u

#define REG32(a)            (*(volatile uint32_t *)(uint32_t)(a))

#define RCC_CR          REG32(0x40023800u)
#define RCC_PLLCFGR     REG32(0x40023804u)
#define RCC_CFGR        REG32(0x40023808u)
#define RCC_AHB1ENR     REG32(0x40023830u)
#define RCC_APB2ENR     REG32(0x40023844u)
#define FLASH_ACR       REG32(0x40023C00u)

#define GPIOA_MODER     REG32(0x40020000u)
#define GPIOA_OSPEEDR   REG32(0x40020008u)
#define GPIOA_AFRH      REG32(0x40020024u)

#define GPIOB_MODER     REG32(0x40020400u)
#define GPIOB_OTYPER    REG32(0x40020404u)
#define GPIOB_OSPEEDR   REG32(0x40020408u)
#define GPIOB_PUPDR     REG32(0x4002040Cu)
#define GPIOB_IDR       REG32(0x40020410u)
#define GPIOB_ODR       REG32(0x40020414u)
#define GPIOB_BSRR      REG32(0x40020418u)

#define USART1_SR       REG32(0x40011000u)
#define USART1_DR       REG32(0x40011004u)
#define USART1_BRR      REG32(0x40011008u)
#define USART1_CR1      REG32(0x4001100Cu)

/* RGB565 */
#define C_BG      0x0000u
#define C_TITLE   0xFFFFu
#define C_WARM    0xFD20u   /* 橙 */
#define C_COOL    0x051Fu   /* 青 */
#define C_BLUE    0x2C7Fu   /* 浅蓝 */
#define C_GREY    0x7BEFu
#define C_GREEN   0x07E0u
#define C_RED     0xF800u

/* ---------------- 基础 ---------------- */
static void clock_init(void)
{
    FLASH_ACR = (3u << 0) | (1u << 8) | (1u << 9);
    RCC_PLLCFGR = (8u << 0) | (200u << 6) | (1u << 16) | (7u << 24);
    RCC_CR |= (1u << 24);
    while (!(RCC_CR & (1u << 25))) { }
    RCC_CFGR = (RCC_CFGR & ~((0xFu << 4) | (7u << 10) | (7u << 11) | 3u)) | (4u << 10) | 2u;
    while (((RCC_CFGR >> 2) & 3u) != 2u) { }
}

static void uart_init(void)
{
    RCC_AHB1ENR |= (1u << 0) | (1u << 1);
    RCC_APB2ENR |= (1u << 4);
    GPIOA_MODER = (GPIOA_MODER & ~((3u << 18) | (3u << 20))) | (2u << 18) | (2u << 20);
    GPIOA_OSPEEDR |= (3u << 18) | (3u << 20);
    GPIOA_AFRH = (GPIOA_AFRH & ~((0xFu << 4) | (0xFu << 8))) | (7u << 4) | (7u << 8);
    USART1_BRR = 868u;
    USART1_CR1 = (1u << 13) | (1u << 3) | (1u << 2);
}

static void uart_putc(char c)
{
    while (!(USART1_SR & (1u << 7))) { }
    USART1_DR = (uint32_t)(uint8_t)c;
}

static void uart_puts(const char *s) { while (*s) { uart_putc(*s++); } }

static void uart_udec(uint32_t v)
{
    char b[12];
    int i = 0;
    if (!v) { uart_putc('0'); return; }
    while (v) { b[i++] = (char)('0' + v % 10u); v /= 10u; }
    while (i--) { uart_putc(b[i]); }
}

static void uart_fixed2(uint32_t v)
{
    uart_udec(v / 100u);
    uart_putc('.');
    uart_putc((char)('0' + (v / 10u) % 10u));
    uart_putc((char)('0' + v % 10u));
}

static void delay_ms(uint32_t ms)
{
    uint32_t i;
    while (ms--) { for (i = 0u; i < 13333u; i++) { __asm volatile ("nop"); } }
}

/* ---------------- 软件 I2C（AHT21B） ---------------- */
static void i2c_delay(void) { volatile uint32_t i; for (i = 0u; i < 20u; i++) { } }

static void scl_hi(void) { GPIOB_ODR |= (1u << AHT_SCL); }
static void scl_lo(void) { GPIOB_ODR &= ~(1u << AHT_SCL); }
static void sda_hi(void) { GPIOB_ODR |= (1u << AHT_SDA); }
static void sda_lo(void) { GPIOB_ODR &= ~(1u << AHT_SDA); }
static uint32_t sda_read(void) { return (GPIOB_IDR >> AHT_SDA) & 1u; }

static void i2c_init(void)
{
    GPIOB_MODER = (GPIOB_MODER & ~((3u << (AHT_SCL * 2u)) | (3u << (AHT_SDA * 2u))))
                | (1u << (AHT_SCL * 2u)) | (1u << (AHT_SDA * 2u));
    GPIOB_OTYPER |= (1u << AHT_SCL) | (1u << AHT_SDA);
    GPIOB_OSPEEDR |= (1u << (AHT_SCL * 2u)) | (1u << (AHT_SDA * 2u));
    GPIOB_PUPDR = (GPIOB_PUPDR & ~((3u << (AHT_SCL * 2u)) | (3u << (AHT_SDA * 2u))))
                | (1u << (AHT_SCL * 2u)) | (1u << (AHT_SDA * 2u));
    scl_hi(); sda_hi();
}

static void i2c_start(void)
{
    sda_hi(); i2c_delay(); scl_hi(); i2c_delay();
    sda_lo(); i2c_delay(); scl_lo(); i2c_delay();
}

static void i2c_stop(void)
{
    sda_lo(); i2c_delay(); scl_hi(); i2c_delay(); sda_hi(); i2c_delay();
}

static uint32_t i2c_write(uint8_t b)
{
    uint32_t i, ack;
    for (i = 0u; i < 8u; i++) {
        if (b & 0x80u) { sda_hi(); } else { sda_lo(); }
        i2c_delay(); scl_hi(); i2c_delay(); scl_lo(); i2c_delay();
        b = (uint8_t)(b << 1);
    }
    sda_hi(); i2c_delay(); scl_hi(); i2c_delay();
    ack = (sda_read() == 0u) ? 1u : 0u;
    scl_lo(); i2c_delay();
    return ack;
}

static uint8_t i2c_read(uint32_t ack_after)
{
    uint8_t v = 0u;
    uint32_t i;
    sda_hi();
    for (i = 0u; i < 8u; i++) {
        v = (uint8_t)(v << 1);
        scl_hi(); i2c_delay();
        if (sda_read()) { v |= 1u; }
        scl_lo(); i2c_delay();
    }
    if (ack_after) { sda_lo(); } else { sda_hi(); }
    i2c_delay(); scl_hi(); i2c_delay(); scl_lo(); i2c_delay();
    sda_hi();
    return v;
}

static uint32_t i2c_send(uint8_t addr, const uint8_t *p, uint32_t n)
{
    uint32_t i;
    i2c_start();
    if (!i2c_write((uint8_t)(addr << 1))) { i2c_stop(); return 0u; }
    for (i = 0u; i < n; i++) { if (!i2c_write(p[i])) { i2c_stop(); return 0u; } }
    i2c_stop();
    return 1u;
}

static uint32_t i2c_recv(uint8_t addr, uint8_t *p, uint32_t n)
{
    uint32_t i;
    i2c_start();
    if (!i2c_write((uint8_t)((addr << 1) | 1u))) { i2c_stop(); return 0u; }
    for (i = 0u; i < n; i++) { p[i] = i2c_read((i + 1u) < n); }
    i2c_stop();
    return 1u;
}

/* ---------------- 软件 SPI（TFT） ---------------- */
#define SCLK_H()  (GPIOB_BSRR = (1u << PIN_SCK))
#define SCLK_L()  (GPIOB_BSRR = (1u << (PIN_SCK + 16u)))
#define MOSI_H()  (GPIOB_BSRR = (1u << PIN_MOSI))
#define MOSI_L()  (GPIOB_BSRR = (1u << (PIN_MOSI + 16u)))
#define CS_L()    (GPIOB_BSRR = (1u << (PIN_CS + 16u)))
#define CS_H()    (GPIOB_BSRR = (1u << PIN_CS))
#define DC_CMD()  (GPIOB_BSRR = (1u << (PIN_DC + 16u)))
#define DC_DATA() (GPIOB_BSRR = (1u << PIN_DC))
#define RST_L()   (GPIOB_BSRR = (1u << (PIN_RST + 16u)))
#define RST_H()   (GPIOB_BSRR = (1u << PIN_RST))

static void tft_pins_init(void)
{
    uint32_t i;
    const uint32_t p[5] = { PIN_SCK, PIN_MOSI, PIN_CS, PIN_DC, PIN_RST };
    for (i = 0u; i < 5u; i++) {
        GPIOB_MODER = (GPIOB_MODER & ~(3u << (p[i] * 2u))) | (1u << (p[i] * 2u));
        GPIOB_OSPEEDR |= (3u << (p[i] * 2u));
        GPIOB_PUPDR = (GPIOB_PUPDR & ~(3u << (p[i] * 2u)));
    }
    CS_H(); DC_DATA(); SCLK_L(); RST_H();
}

static void spi_byte(uint8_t b)
{
    int i;
    for (i = 7; i >= 0; i--) {
        if (b & (1u << i)) { MOSI_H(); } else { MOSI_L(); }
        SCLK_H(); SCLK_L();
    }
}

static void tft_cmd(uint8_t c)  { DC_CMD();  CS_L(); spi_byte(c); CS_H(); }
static void tft_data(uint8_t d) { DC_DATA(); CS_L(); spi_byte(d); CS_H(); }

static void tft_cmd_data(uint8_t c, const uint8_t *d, uint32_t n)
{
    uint32_t i;
    tft_cmd(c);
    DC_DATA(); CS_L();
    for (i = 0u; i < n; i++) { spi_byte(d[i]); }
    CS_H();
}

static void tft_init(void)
{
    static const uint8_t frm[] = { 0x01, 0x2C, 0x2D };
    static const uint8_t frm3[] = { 0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D };
    static const uint8_t pw1[] = { 0xA2, 0x02, 0x84 };
    static const uint8_t pw3[] = { 0x0A, 0x00 };
    static const uint8_t pw4[] = { 0x8A, 0x2A };
    static const uint8_t pw5[] = { 0x8A, 0xEE };
    static const uint8_t pgam[] = { 0x02, 0x1C, 0x07, 0x12, 0x37, 0x32, 0x29, 0x2D,
                                    0x29, 0x25, 0x2B, 0x39, 0x00, 0x01, 0x03, 0x10 };
    static const uint8_t ngam[] = { 0x03, 0x1D, 0x07, 0x06, 0x2E, 0x2C, 0x29, 0x2D,
                                    0x2E, 0x2E, 0x37, 0x3F, 0x00, 0x00, 0x02, 0x10 };

    RST_L(); delay_ms(20u); RST_H(); delay_ms(120u);

    tft_cmd(0x01); delay_ms(150u);
    tft_cmd(0x11); delay_ms(120u);
    tft_cmd_data(0xB1, frm, 3u);
    tft_cmd_data(0xB2, frm, 3u);
    tft_cmd_data(0xB3, frm3, 6u);
    tft_cmd(0xB4); tft_data(0x07);
    tft_cmd_data(0xC0, pw1, 3u);
    tft_cmd(0xC1); tft_data(0xC5);
    tft_cmd_data(0xC2, pw3, 2u);
    tft_cmd_data(0xC3, pw4, 2u);
    tft_cmd_data(0xC4, pw5, 2u);
    tft_cmd(0xC5); tft_data(0x0E);
#if TFT_INVERT
    tft_cmd(0x21);
#else
    tft_cmd(0x20);
#endif
    tft_cmd(0x36); tft_data(0xC8);
    tft_cmd(0x3A); tft_data(0x05);
    tft_cmd_data(0xE0, pgam, 16u);
    tft_cmd_data(0xE1, ngam, 16u);
    tft_cmd(0x13); delay_ms(10u);
    tft_cmd(0x29); delay_ms(100u);
}

static void tft_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t buf[4];
    x0 += TFT_XOFF; x1 += TFT_XOFF;
    y0 += TFT_YOFF; y1 += TFT_YOFF;

    buf[0] = (uint8_t)(x0 >> 8); buf[1] = (uint8_t)x0;
    buf[2] = (uint8_t)(x1 >> 8); buf[3] = (uint8_t)x1;
    tft_cmd_data(0x2A, buf, 4u);

    buf[0] = (uint8_t)(y0 >> 8); buf[1] = (uint8_t)y0;
    buf[2] = (uint8_t)(y1 >> 8); buf[3] = (uint8_t)y1;
    tft_cmd_data(0x2B, buf, 4u);

    tft_cmd(0x2C);
}

/* ---------------- 帧缓冲 + 文字（5x7，可缩放） ----------------
 * 直接用 tft_rect 画每个像素会非常慢（每像素一条命令），
 * 所以在 RAM 里建 128x128 RGB565 帧缓冲（32 KB），画完一次性推屏。
 */
static uint16_t lcd[TFT_W * TFT_H];

static void lcd_fill(uint16_t c)
{
    uint32_t i, n = TFT_W * TFT_H;
    for (i = 0u; i < n; i++) { lcd[i] = c; }
}

static void lcd_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t c)
{
    uint32_t i, j;
    for (j = 0u; j < h; j++) {
        uint32_t yy = (uint32_t)y + j;
        if (yy >= TFT_H) { break; }
        for (i = 0u; i < w; i++) {
            uint32_t xx = (uint32_t)x + i;
            if (xx >= TFT_W) { break; }
            lcd[yy * TFT_W + xx] = c;
        }
    }
}

static void lcd_px(uint16_t x, uint16_t y, uint16_t c)
{
    if (x < TFT_W && y < TFT_H) { lcd[(uint32_t)y * TFT_W + x] = c; }
}

static void draw_char(uint16_t x, uint16_t y, char ch, uint8_t scale, uint16_t fg, uint16_t bg)
{
    const unsigned char *g = font_glyph(ch);
    uint32_t col, row, dx, dy;

    for (col = 0u; col < 5u; col++) {
        for (row = 0u; row < 7u; row++) {
            uint16_t c = (g[col] & (1u << row)) ? fg : bg;
            for (dx = 0u; dx < scale; dx++) {
                for (dy = 0u; dy < scale; dy++) {
                    lcd_px((uint16_t)(x + col * scale + dx),
                           (uint16_t)(y + row * scale + dy), c);
                }
            }
        }
    }
}

static void draw_str(uint16_t x, uint16_t y, const char *s, uint8_t scale, uint16_t fg, uint16_t bg)
{
    while (*s) {
        draw_char(x, y, *s++, scale, fg, bg);
        x = (uint16_t)(x + 6u * scale);
    }
}

/* 把整块帧缓冲推到屏上 */
static void lcd_flush(void)
{
    uint32_t i, n = TFT_W * TFT_H;
    tft_window(0u, 0u, TFT_W - 1u, TFT_H - 1u);
    DC_DATA(); CS_L();
    for (i = 0u; i < n; i++) {
        uint16_t c = lcd[i];
        spi_byte((uint8_t)(c >> 8));
        spi_byte((uint8_t)(c & 0xFFu));
    }
    CS_H();
}

/* 无符号定点（×100）转 "12.34" */
static void fixed2_str(char *buf, uint32_t v)
{
    uint32_t ip = v / 100u, fp = v % 100u;
    char tmp[10];
    int i = 0, n = 0;

    if (!ip) { buf[n++] = '0'; }
    while (ip) { tmp[i++] = (char)('0' + ip % 10u); ip /= 10u; }
    while (i--) { buf[n++] = tmp[i]; }
    buf[n++] = '.';
    buf[n++] = (char)('0' + (fp / 10u) % 10u);
    buf[n++] = (char)('0' + fp % 10u);
    buf[n] = 0;
}

/* ---------------- AHT21B ---------------- */
static uint32_t aht_write3(const uint8_t *p) { return i2c_send(AHT_ADDR, p, 3u); }

static uint32_t aht_begin(void)
{
    static const uint8_t init_cmd[3] = { 0xBE, 0x08, 0x00 };
    uint8_t st = 0u;
    uint32_t tries;

    delay_ms(120u);
    if (!i2c_recv(AHT_ADDR, &st, 1u)) { return 1u; }
    if ((st & 0x08u) == 0u) {
        if (!aht_write3(init_cmd)) { return 1u; }
        delay_ms(10u);
    }
    for (tries = 0u; tries < 10u; tries++) {
        if (!i2c_recv(AHT_ADDR, &st, 1u)) { return 1u; }
        if ((st & 0x80u) == 0u) { return 0u; }
        delay_ms(10u);
    }
    return 2u;
}

typedef struct { uint32_t t_x100; uint32_t h_x100; } aht_val_t;

static uint32_t aht_measure(aht_val_t *out)
{
    static const uint8_t trig[3] = { 0xAC, 0x33, 0x00 };
    uint8_t b[7];
    uint32_t raw_h, raw_t, tries;

    if (!aht_write3(trig)) { return 1u; }
    delay_ms(80u);

    if (!i2c_recv(AHT_ADDR, b, 1u)) { return 1u; }
    for (tries = 0u; (b[0] & 0x80u) && tries < 10u; tries++) {
        delay_ms(10u);
        if (!i2c_recv(AHT_ADDR, b, 1u)) { return 1u; }
    }
    if (b[0] & 0x80u) { return 2u; }

    if (!i2c_recv(AHT_ADDR, b, 7u)) { return 1u; }

    raw_h = ((uint32_t)b[1] << 12) | ((uint32_t)b[2] << 4) | ((uint32_t)b[3] >> 4);
    raw_t = (((uint32_t)b[3] & 0x0Fu) << 16) | ((uint32_t)b[4] << 8) | (uint32_t)b[5];

    /* 注意：raw_h 最大 2^20，raw_h*10000 会超过 32 位（约 1.05e10），
       所以先右移 4 位再乘，等价于 (raw*10000)>>20，误差 < 0.01%RH */
    out->h_x100 = (((uint32_t)raw_h >> 4) * 10000u) >> 16;
    {
{
        int32_t t = (int32_t)((((uint32_t)raw_t >> 4) * 20000u) >> 16) - 5000;
        out->t_x100 = (t < 0) ? 0u : (uint32_t)t;
    }
    }
    return 0u;
}

/* ---------------- 界面 ---------------- */
static void draw_ui(const aht_val_t *v, uint32_t ok)
{
    char num[12];
    uint16_t tcol;

    lcd_fill(C_BG);

    /* 顶部标题条 */
    lcd_rect(0u, 0u, TFT_W, 14u, 0x1082u);
    draw_str(6u, 4u, "ENV MONITOR", 1u, C_TITLE, 0x1082u);

    if (!ok) {
        draw_str(10u, 40u, "SENSOR", 2u, C_RED, C_BG);
        draw_str(10u, 62u, "ERROR", 2u, C_RED, C_BG);
        draw_str(10u, 96u, "CHECK I2C", 1u, C_GREY, C_BG);
        lcd_flush();
        return;
    }

    /* 温度：大字，按冷热换色 */
    tcol = (v->t_x100 > 3000u) ? C_WARM : ((v->t_x100 < 1800u) ? C_COOL : C_GREEN);
    fixed2_str(num, v->t_x100);
    draw_str(8u, 22u, num, 3u, tcol, C_BG);
    draw_str(8u, 46u, "CELSIUS", 1u, C_GREY, C_BG);

    /* 湿度：大字 */
    fixed2_str(num, v->h_x100);
    draw_str(8u, 60u, num, 3u, C_BLUE, C_BG);
    draw_str(8u, 84u, "HUMIDITY %", 1u, C_GREY, C_BG);

    /* 湿度条 */
    {
        uint16_t bar_w = 108u;
        uint16_t fill = (uint16_t)((v->h_x100 > 10000u) ? bar_w : (v->h_x100 * bar_w / 10000u));
        lcd_rect(10u, 100u, bar_w, 8u, 0x2104u);
        if (fill) { lcd_rect(10u, 100u, fill, 8u, C_BLUE); }
    }

    /* 状态 */
    if (v->t_x100 > 3000u)      { draw_str(10u, 114u, "HOT", 1u, C_WARM, C_BG); }
    else if (v->t_x100 < 1800u) { draw_str(10u, 114u, "COLD", 1u, C_COOL, C_BG); }
    else                        { draw_str(10u, 114u, "COMFORT", 1u, C_GREEN, C_BG); }

    lcd_flush();
}

int main(void)
{
    uint32_t ret;

    clock_init();
    uart_init();
    i2c_init();
    tft_pins_init();
    delay_ms(60u);

    uart_puts("\r\nAHT21 -> TFT 128x128\r\n");

    tft_init();
    uart_puts("TFT OK\r\n");

    ret = aht_begin();
    if (ret) {
        uart_puts("AHT INIT ERR ");
        uart_udec(ret);
        uart_puts("\r\n");
    } else {
        uart_puts("AHT OK\r\n");
    }

    for (;;) {
        aht_val_t v;
        uint32_t ok;
        v.t_x100 = 0u; v.h_x100 = 0u;
        ok = (ret == 0u) && (aht_measure(&v) == 0u);

        draw_ui(&v, ok);

        if (ok) {
            uart_puts("T=");
            uart_fixed2(v.t_x100);
            uart_puts("C H=");
            uart_fixed2(v.h_x100);
            uart_puts("%\r\n");
        } else {
            uart_puts("MEAS FAIL\r\n");
        }
        delay_ms(1000u);
    }
}
