/* AHT21B 原始数据诊断：把 7 字节原样打印，判断是"数据为 0"还是"读偏移"
 *
 * 串口每秒一组：
 *   RAW1 xx xx xx xx xx xx xx     <- 触发后直接读 7 字节（标准时序）
 *   RAW2 xx xx xx xx xx xx xx     <- 先读 1 字节状态再读 7 字节（本工程原做法）
 *   A1  T=..C H=..%               <- 按 RAW1 解出的值
 *   A2  T=..C H=..%               <- 按 RAW2 解出的值
 *
 * 判读：
 *   - 两条都全 0            -> 传感器已 ACK 但没给数据（多半没触发成功/未校准/供电弱）
 *   - RAW1 有数据、RAW2 错位 -> 说明"先读状态再读 7 字节"会吞掉首字节，应改用 RAW1 时序
 *   - 两条数据相同且合理    -> 问题在换算，不在读取
 */
#include <stdint.h>

#define AHT_SCL   7u
#define AHT_SDA   6u
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

static void uart_fixed2(uint32_t v)
{
    uart_udec(v / 100u);
    uart_putc('.');
    uart_putc((char)('0' + (v / 10u) % 10u));
    uart_putc((char)('0' + v % 10u));
}

static void uart_hex2(uint8_t v)
{
    const char *h = "0123456789ABCDEF";
    uart_putc(h[(v >> 4) & 0xFu]);
    uart_putc(h[v & 0xFu]);
}

static uint8_t aht_crc8(const uint8_t *d, uint32_t n)
{
    uint8_t crc = 0xFFu;
    uint32_t i;
    int k;
    for (i = 0u; i < n; i++) {
        crc ^= d[i];
        for (k = 0; k < 8; k++) {
            crc = (crc & 0x80u) ? (uint8_t)((crc << 1) ^ 0x31u) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

static void dump(const char *tag, const uint8_t *b, uint32_t n)
{
    uint32_t i;
    uart_puts(tag);
    for (i = 0u; i < n; i++) {
        uart_putc(' ');
        uart_hex2(b[i]);
    }
    if (n >= 7u) {
        uint8_t c = aht_crc8(b, 6u);
        uart_puts("  crc=");
        uart_hex2(c);
        uart_puts((b[6] == c) ? " OK" : " BAD");
    }
    uart_puts("\r\n");
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

/* ---------------- 解码 ---------------- */
static void decode(const char *tag, const uint8_t *b)
{
    uint32_t raw_h, raw_t, h_x100;
    int32_t t_x100;

    raw_h = ((uint32_t)b[1] << 12) | ((uint32_t)b[2] << 4) | ((uint32_t)b[3] >> 4);
    raw_t = (((uint32_t)b[3] & 0x0Fu) << 16) | ((uint32_t)b[4] << 8) | (uint32_t)b[5];

    h_x100 = (((uint32_t)raw_h >> 4) * 10000u) >> 16;
    t_x100 = (int32_t)((((uint32_t)raw_t >> 4) * 20000u) >> 16) - 5000;

    uart_puts(tag);
    uart_puts(" rawH=");
    uart_udec(raw_h);
    uart_puts(" rawT=");
    uart_udec(raw_t);
    uart_puts("  T=");
    if (t_x100 < 0) { uart_putc('-'); uart_fixed2((uint32_t)(-t_x100)); }
    else { uart_fixed2((uint32_t)t_x100); }
    uart_puts("C H=");
    uart_fixed2(h_x100);
    uart_puts("%\r\n");
}

int main(void)
{
    static const uint8_t trig[3] = { 0xAC, 0x33, 0x00 };
    static const uint8_t init_cmd[3] = { 0xBE, 0x08, 0x00 };
    uint8_t b1[7], b2[7];
    uint8_t st;
    uint32_t tries, ok1, ok2;

    clock_init();
    uart_init();
    i2c_init();
    delay_ms(150u);

    uart_puts("\r\nAHT21 RAW DIAG\r\n");

    /* 1) 读状态，看是否需要初始化 */
    if (!i2c_recv(AHT_ADDR, &st, 1u)) {
        uart_puts("NO ACK at 0x38\r\n");
        for (;;) { delay_ms(1000u); }
    }
    uart_puts("status=");
    uart_hex2(st);
    uart_puts("\r\n");

    if ((st & 0x08u) == 0u) {
        uart_puts("not calibrated -> send BE 08 00\r\n");
        i2c_send(AHT_ADDR, init_cmd, 3u);
        delay_ms(20u);
        if (i2c_recv(AHT_ADDR, &st, 1u)) {
            uart_puts("status=");
            uart_hex2(st);
            uart_puts("\r\n");
        }
    }

    for (;;) {
        /* ---- 方式 1：触发后直接读 7 字节（标准时序） ---- */
        ok1 = 0u;
        if (i2c_send(AHT_ADDR, trig, 3u)) {
            delay_ms(80u);
            for (tries = 0u; tries < 10u; tries++) {
                if (!i2c_recv(AHT_ADDR, b1, 7u)) { break; }
                if ((b1[0] & 0x80u) == 0u) { ok1 = 1u; break; }
                delay_ms(10u);
            }
        }
        dump("RAW1", b1, 7u);

        /* ---- 方式 2：先读 1 字节状态，再读 7 字节（原工程做法） ---- */
        ok2 = 0u;
        if (i2c_send(AHT_ADDR, trig, 3u)) {
            delay_ms(80u);
            for (tries = 0u; tries < 10u; tries++) {
                if (!i2c_recv(AHT_ADDR, &st, 1u)) { break; }
                if ((st & 0x80u) == 0u) { break; }
                delay_ms(10u);
            }
            if (i2c_recv(AHT_ADDR, b2, 7u)) { ok2 = 1u; }
        }
        dump("RAW2", b2, 7u);

        if (ok1) { decode("A1", b1); }
        if (ok2) { decode("A2", b2); }

        uart_puts("----\r\n");
        delay_ms(1200u);
    }
}
