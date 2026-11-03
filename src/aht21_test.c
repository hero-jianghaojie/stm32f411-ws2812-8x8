/* AHT21B 温湿度传感器 —— 只测读数，串口输出
 *
 * 接线（与 OLED 共用同一条软件 I2C 总线）：
 *   AHT21B        Black Pill
 *   VCC     ->    3.3V        (不要接 5V)
 *   GND     ->    GND
 *   SCL     ->    PB7         (本项目实测：OLED 的 SCL 在 PB7)
 *   SDA     ->    PB6
 *
 * 若你想接到别的脚，改下面的 AHT_SCL / AHT_SDA 两个宏即可（必须同组 GPIO，
 * 当前实现假定是 GPIOB；换到 GPIOA 需同时改 GPIOB_* 寄存器与实际端口）。
 *
 * 串口 USART1 PA9/PA10 115200 每 1 s 打印：
 *   T=25.31C H=48.72%
 * 读不到时打印：
 *   ERR n   (1=无 ACK, 2=忙未就绪, 3=CRC 错)
 *
 * AHT20/21 协议：
 *   上电等待 >=100ms
 *   发 0x71 读状态；bit3=校准使能
 *   触发: 0xAC 0x33 0x00
 *   等 80ms，读 6 字节: [状态][湿度19:12][湿度11:4][湿度3:0|温度19:16][温度15:8][温度7:0]
 *   RH  = raw_h / 2^20 * 100 %
 *   T   = raw_t / 2^20 * 200 - 50 ℃
 */
#include <stdint.h>

#define AHT_SCL   7u        /* PB7 */
#define AHT_SDA   6u        /* PB6 */
#define AHT_ADDR  0x38u

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

/* 打印定点数: v/100 保留两位（v 为放大 100 倍的整数） */
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
    uint8_t v = 0;
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

/* 写一串字节；返回 1 成功（全部 ACK） */
static uint32_t aht_write(const uint8_t *p, uint32_t n)
{
    uint32_t i;
    i2c_start();
    if (!i2c_write((uint8_t)(AHT_ADDR << 1))) { i2c_stop(); return 0u; }
    for (i = 0u; i < n; i++) {
        if (!i2c_write(p[i])) { i2c_stop(); return 0u; }
    }
    i2c_stop();
    return 1u;
}

/* 读 n 字节；返回 1 成功 */
static uint32_t aht_read(uint8_t *buf, uint32_t n)
{
    uint32_t i;
    i2c_start();
    if (!i2c_write((uint8_t)((AHT_ADDR << 1) | 1u))) { i2c_stop(); return 0u; }
    for (i = 0u; i < n; i++) {
        buf[i] = i2c_read((i + 1u) < n);
    }
    i2c_stop();
    return 1u;
}

/* ---------------- AHT21B ---------------- */
/* 返回 0 成功；1 无 ACK；2 忙超时 */
static uint32_t aht_begin(void)
{
    uint8_t st = 0u;
    uint32_t tries;

    delay_ms(120u);

    /* 若未校准（状态 bit3 == 0），发初始化命令 0xBE 0x08 0x00 */
    i2c_start();
    if (!i2c_write((uint8_t)(AHT_ADDR << 1))) { i2c_stop(); return 1u; }
    i2c_stop();
    delay_ms(5u);

    if (!aht_read(&st, 1u)) { return 1u; }
    if ((st & 0x08u) == 0u) {
        static const uint8_t init_cmd[3] = { 0xBE, 0x08, 0x00 };
        if (!aht_write(init_cmd, 3u)) { return 1u; }
        delay_ms(10u);
    }

    /* 等空闲（状态 bit7 == 0） */
    for (tries = 0u; tries < 10u; tries++) {
        if (!aht_read(&st, 1u)) { return 1u; }
        if ((st & 0x80u) == 0u) { return 0u; }
        delay_ms(10u);
    }
    return 2u;
}

typedef struct { uint32_t t_x100; uint32_t h_x100; } aht_val_t;

/* 读一次温湿度；返回 0 成功 */
static uint32_t aht_measure(aht_val_t *out)
{
    static const uint8_t trig[3] = { 0xAC, 0x33, 0x00 };
    uint8_t b[7];
    uint32_t raw_h, raw_t;
    uint32_t tries;

    if (!aht_write(trig, 3u)) { return 1u; }

    /* 等测量完成（状态 bit7 == 0），最多 100 ms */
    for (tries = 0u; tries < 12u; tries++) {
        delay_ms(10u);
        if (!aht_read(b, 1u)) { return 1u; }
        if ((b[0] & 0x80u) == 0u) { break; }
    }

    if (!aht_read(b, 7u)) { return 1u; }

    raw_h = ((uint32_t)b[1] << 12) | ((uint32_t)b[2] << 4) | ((uint32_t)b[3] >> 4);
    raw_t = (((uint32_t)b[3] & 0x0Fu) << 16) | ((uint32_t)b[4] << 8) | (uint32_t)b[5];

    /* RH% = raw_h / 2^20 * 100  →  放大 100 倍 = raw_h * 10000 / 2^20 */
    /* 注意：raw_h 最大 2^20，raw_h*10000 会超过 32 位（约 1.05e10），
       所以先右移 4 位再乘，等价于 (raw*10000)>>20，误差 < 0.01%RH */
    out->h_x100 = (((uint32_t)raw_h >> 4) * 10000u) >> 16;

    /* T = raw_t / 2^20 * 200 - 50  →  放大 100 倍 = raw_t * 20000/2^20 - 5000 */
    {
{
        int32_t t = (int32_t)((((uint32_t)raw_t >> 4) * 20000u) >> 16) - 5000;
        out->t_x100 = (t < 0) ? 0u : (uint32_t)t;
    }
    }
    return 0u;
}

int main(void)
{
    uint32_t ret, hb = 0u;

    clock_init();
    uart_init();
    i2c_init();

    uart_puts("\r\nAHT21B TEST  SCL=PB");
    uart_udec(AHT_SCL);
    uart_puts(" SDA=PB");
    uart_udec(AHT_SDA);
    uart_puts("\r\n");

    ret = aht_begin();
    if (ret) {
        uart_puts("INIT ERR ");
        uart_udec(ret);
        uart_puts("\r\n(check wiring / 3.3V / pull-ups)\r\n");
        for (;;) { delay_ms(1000u); uart_puts("NO AHT\r\n"); }
    }
    uart_puts("AHT OK\r\n");

    for (;;) {
        aht_val_t v;
        ret = aht_measure(&v);
        if (ret) {
            uart_puts("ERR ");
            uart_udec(ret);
            uart_puts("\r\n");
        } else {
            uart_puts("T=");
            uart_fixed2(v.t_x100);
            uart_puts("C H=");
            uart_fixed2(v.h_x100);
            uart_puts("%\r\n");
        }
        if (++hb >= 20u) { hb = 0u; }
        delay_ms(900u);
    }
}
