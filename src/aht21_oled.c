/* AHT21B 温湿度 -> OLED(SSD1306/SH1106 128x64) 显示合并版
 *
 * 接线（两者共用同一条软件 I2C 总线）：
 *   AHT21B          Black Pill
 *   VCC       ->    3.3V
 *   GND       ->    GND
 *   SCL       ->    PB7
 *   SDA       ->    PB6
 *   OLED VCC/GND/SCL/SDA 同上（地址 0x3C 或 0x3D，与 0x38 不冲突）
 *
 * 显示内容：
 *   TEMP   25.31 C
 *   HUM    48.72 %
 *   + 一条随湿度变化的横条
 *
 * 串口同样打印 T=..C H=..%（便于对照）
 */
#include <stdint.h>
#include "font5x7.h"

#define AHT_SCL   7u
#define AHT_SDA   6u
#define AHT_ADDR  0x38u

#define OLED_W    128u
#define OLED_H    64u

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

#define USART1_SR       REG32(0x40011000u)
#define USART1_DR       REG32(0x40011004u)
#define USART1_BRR      REG32(0x40011008u)
#define USART1_CR1      REG32(0x4001100Cu)

static uint8_t  fb[OLED_W * 8u];
static uint8_t  oled_addr = 0x3Cu;

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

/* ---------------- 软件 I2C ---------------- */
static void i2c_delay(void) { volatile uint32_t i; for (i = 0u; i < 20u; i++) { } }

static void scl_hi(void) { GPIOB_ODR |= (1u << AHT_SCL); }
static void scl_lo(void) { GPIOB_ODR &= ~(1u << AHT_SCL); }
static void sda_hi(void) { GPIOB_ODR |= (1u << AHT_SDA); }
static void sda_lo(void) { GPIOB_ODR &= ~(1u << AHT_SDA); }
static uint32_t sda_read(void) { return (GPIOB_IDR >> AHT_SDA) & 1u; }

static void i2c_init(void)
{
    RCC_AHB1ENR |= (1u << 1);
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
    for (i = 0u; i < n; i++) {
        if (!i2c_write(p[i])) { i2c_stop(); return 0u; }
    }
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

/* ---------------- OLED ---------------- */
static void oled_cmds(const uint8_t *c, uint32_t n)
{
    uint8_t tmp[17];
    uint32_t i = 0u, k;
    while (i < n) {
        uint32_t m = n - i;
        if (m > 16u) { m = 16u; }
        tmp[0] = 0x00u;
        for (k = 0u; k < m; k++) { tmp[1u + k] = c[i + k]; }
        i2c_send(oled_addr, tmp, m + 1u);
        i += m;
    }
}

static void oled_init(void)
{
    static const uint8_t seq[] = {
        0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
        0x8D, 0x14, 0xA1, 0xC8, 0xDA, 0x12, 0x81, 0xCF,
        0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF
    };
    oled_cmds(seq, sizeof(seq));
}

static void oled_flush(void)
{
    uint8_t page, col;
    uint8_t hdr[3];
    uint8_t line[1u + OLED_W];

    for (page = 0u; page < 8u; page++) {
        hdr[0] = 0x00u;
        hdr[1] = (uint8_t)(0xB0u | page);
        hdr[2] = 0x00u;
        i2c_send(oled_addr, hdr, 3u);
        hdr[1] = 0x10u;
        i2c_send(oled_addr, hdr, 3u);

        line[0] = 0x40u;
        for (col = 0u; col < OLED_W; col++) {
            line[1u + col] = fb[page * OLED_W + col];
        }
        i2c_send(oled_addr, line, 1u + OLED_W);
    }
}

static void fb_clear(void)
{
    uint32_t i;
    for (i = 0u; i < sizeof(fb); i++) { fb[i] = 0u; }
}

static void draw_pixel(uint32_t x, uint32_t y, uint32_t on)
{
    if (x >= OLED_W || y >= OLED_H) { return; }
    if (on) { fb[(y / 8u) * OLED_W + x] |= (uint8_t)(1u << (y & 7u)); }
    else    { fb[(y / 8u) * OLED_W + x] &= (uint8_t)~(1u << (y & 7u)); }
}

/* 5x7 字符，字间 1 px */
static void draw_char(uint32_t x, uint32_t y, char c)
{
    const unsigned char *g = font_glyph(c);
    uint32_t col, row;
    for (col = 0u; col < 5u; col++) {
        uint8_t bits = g[col];
        for (row = 0u; row < 7u; row++) {
            if (bits & (1u << row)) { draw_pixel(x + col, y + row, 1u); }
        }
    }
}

static void draw_str(uint32_t x, uint32_t y, const char *s)
{
    while (*s) {
        draw_char(x, y, *s++);
        x += 6u;
    }
}

static void draw_bar(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t fill_pct)
{
    uint32_t i;
    for (i = 0u; i < w; i++) {
        uint32_t on = (i * 100u / w) < fill_pct;
        uint32_t r;
        if (!on) { continue; }
        for (r = 0u; r < h; r++) { draw_pixel(x + i, y + r, 1u); }
    }
}

static void draw_frame(void)
{
    uint32_t i;
    for (i = 0u; i < OLED_W; i++) { draw_pixel(i, 0u, 1u); draw_pixel(i, OLED_H - 1u, 1u); }
}

/* ---------------- AHT21B ---------------- */
static uint32_t aht_write3(const uint8_t *p) { return i2c_send(AHT_ADDR, p, 3u); }

static uint32_t aht_read_status(uint8_t *st)
{
    return i2c_recv(AHT_ADDR, st, 1u);
}

static uint32_t aht_begin(void)
{
    static const uint8_t init_cmd[3] = { 0xBE, 0x08, 0x00 };
    uint8_t st = 0u;
    uint32_t tries;

    delay_ms(120u);
    if (!aht_read_status(&st)) { return 1u; }
    if ((st & 0x08u) == 0u) {
        if (!aht_write3(init_cmd)) { return 1u; }
        delay_ms(10u);
    }
    for (tries = 0u; tries < 10u; tries++) {
        if (!aht_read_status(&st)) { return 1u; }
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

    if (!aht_read_status(&b[0])) { return 1u; }
    for (tries = 0u; (b[0] & 0x80u) && tries < 10u; tries++) {
        delay_ms(10u);
        if (!aht_read_status(&b[0])) { return 1u; }
    }
    if (b[0] & 0x80u) { return 2u; }

    if (!i2c_recv(AHT_ADDR, b, 7u)) { return 1u; }

    raw_h = ((uint32_t)b[1] << 12) | ((uint32_t)b[2] << 4) | ((uint32_t)b[3] >> 4);
    raw_t = (((uint32_t)b[3] & 0x0Fu) << 16) | ((uint32_t)b[4] << 8) | (uint32_t)b[5];

    out->h_x100 = (raw_h * 10000u) >> 20;
    {
        int32_t t = (int32_t)((raw_t * 20000u) >> 20) - 5000;
        out->t_x100 = (t < 0) ? 0u : (uint32_t)t;
    }
    return 0u;
}

/* 无符号定点（放大 100 倍）转字符串，写入 buf，返回长度 */
static uint32_t fixed2_str(char *buf, uint32_t v)
{
    uint32_t n = 0u, ip = v / 100u, fp = v % 100u;
    char tmp[10];
    int i = 0;

    if (!ip) { buf[n++] = '0'; }
    while (ip) { tmp[i++] = (char)('0' + ip % 10u); ip /= 10u; }
    while (i--) { buf[n++] = tmp[i]; }
    buf[n++] = '.';
    buf[n++] = (char)('0' + (fp / 10u) % 10u);
    buf[n++] = (char)('0' + fp % 10u);
    buf[n] = 0;
    return n;
}

int main(void)
{
    uint32_t ret;
    char num[12];

    clock_init();
    uart_init();
    i2c_init();
    delay_ms(120u);

    /* 探测 OLED 地址 */
    if (i2c_send(0x3Cu, (const uint8_t *)"\x00\xAE", 2u)) { oled_addr = 0x3Cu; }
    else if (i2c_send(0x3Du, (const uint8_t *)"\x00\xAE", 2u)) { oled_addr = 0x3Du; }
    else {
        uart_puts("OLED NOT FOUND\r\n");
        for (;;) { delay_ms(1000u); uart_puts("NO OLED\r\n"); }
    }
    oled_init();
    delay_ms(50u);

    uart_puts("\r\nAHT21 + OLED merge\r\n");

    ret = aht_begin();
    if (ret) {
        fb_clear();
        draw_frame();
        draw_str(8u, 12u, "AHT21 ERROR");
        draw_str(8u, 30u, "CHECK WIRING");
        oled_flush();
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

        fb_clear();
        draw_frame();

        if (ok) {
            draw_str(4u, 6u, "TEMP");
            fixed2_str(num, v.t_x100);
            draw_str(40u, 6u, num);
            draw_str(88u, 6u, "C");

            draw_str(4u, 22u, "HUM");
            fixed2_str(num, v.h_x100);
            draw_str(40u, 22u, num);
            draw_str(88u, 22u, "%");

            draw_bar(4u, 40u, 120u, 6u, (v.h_x100 > 10000u) ? 100u : (v.h_x100 / 100u));

            if (v.t_x100 > 3000u) { draw_str(4u, 52u, "HOT"); }
            else if (v.t_x100 < 1800u) { draw_str(4u, 52u, "COLD"); }
            else { draw_str(4u, 52u, "COMFORT"); }

            uart_puts("T=");
            uart_fixed2(v.t_x100);
            uart_puts("C H=");
            uart_fixed2(v.h_x100);
            uart_puts("%\r\n");
        } else {
            draw_str(4u, 12u, "SENSOR ERR");
            draw_str(4u, 28u, "RETRY...");
            uart_puts("MEAS FAIL\r\n");
        }

        oled_flush();
        delay_ms(1000u);
    }
}
