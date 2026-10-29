/* OLED 播放"鲸鱼娘"8 帧动画 (128x64, 单色)
 * 总线: 软件 I2C, SCL=PB7, SDA=PB6 (实测接法), 开漏 + 上拉
 * 显示: 页寻址, 起始地址自动探测 0x3C / 0x3D
 * 串口: USART1 PA9/PA10 115200, 每秒 RUN 心跳
 */
#include <stdint.h>
#include "anim_whale.h"

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

#define SCL_PIN 7u
#define SDA_PIN 6u

static uint8_t fb[128 * 8];
static uint8_t oled_addr = 0x3Cu;

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
    RCC_AHB1ENR |= (1u << 0);
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

static void delay_ms(uint32_t ms)
{
    uint32_t i;
    while (ms--) { for (i = 0u; i < 13333u; i++) { __asm volatile ("nop"); } }
}

static void i2c_delay(void) { volatile uint32_t i; for (i = 0u; i < 20u; i++) { } }

static void scl_hi(void) { GPIOB_ODR |= (1u << SCL_PIN); }
static void scl_lo(void) { GPIOB_ODR &= ~(1u << SCL_PIN); }
static void sda_hi(void) { GPIOB_ODR |= (1u << SDA_PIN); }
static void sda_lo(void) { GPIOB_ODR &= ~(1u << SDA_PIN); }
static uint32_t sda_read(void) { return (GPIOB_IDR >> SDA_PIN) & 1u; }

static void i2c_init(void)
{
    RCC_AHB1ENR |= (1u << 1);
    GPIOB_MODER = (GPIOB_MODER & ~((3u << (SCL_PIN * 2u)) | (3u << (SDA_PIN * 2u))))
                | (1u << (SCL_PIN * 2u)) | (1u << (SDA_PIN * 2u));
    GPIOB_OTYPER |= (1u << SCL_PIN) | (1u << SDA_PIN);
    GPIOB_OSPEEDR |= (1u << (SCL_PIN * 2u)) | (1u << (SDA_PIN * 2u));
    GPIOB_PUPDR = (GPIOB_PUPDR & ~((3u << (SCL_PIN * 2u)) | (3u << (SDA_PIN * 2u))))
                | (1u << (SCL_PIN * 2u)) | (1u << (SDA_PIN * 2u));
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

static uint32_t i2c_send(uint8_t addr, const uint8_t *buf, uint32_t n)
{
    uint32_t i;
    i2c_start();
    if (!i2c_write((uint8_t)(addr << 1))) { i2c_stop(); return 0u; }
    for (i = 0u; i < n; i++) {
        if (!i2c_write(buf[i])) { i2c_stop(); return 0u; }
    }
    i2c_stop();
    return 1u;
}

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
    uint8_t line[129];

    for (page = 0u; page < 8u; page++) {
        hdr[0] = 0x00u;
        hdr[1] = (uint8_t)(0xB0u | page);
        hdr[2] = 0x00u;
        i2c_send(oled_addr, hdr, 3u);
        hdr[1] = 0x10u;
        i2c_send(oled_addr, hdr, 3u);

        line[0] = 0x40u;
        for (col = 0u; col < 128u; col++) {
            line[1u + col] = fb[page * 128u + col];
        }
        i2c_send(oled_addr, line, 129u);
    }
}

int main(void)
{
    uint32_t f = 0u, i, hb = 0u;

    clock_init();
    uart_init();
    i2c_init();
    delay_ms(120u);

    if (i2c_send(0x3Cu, (const uint8_t *)"\x00\xAE", 2u)) { oled_addr = 0x3Cu; }
    else if (i2c_send(0x3Du, (const uint8_t *)"\x00\xAE", 2u)) { oled_addr = 0x3Du; }
    else {
        uart_puts("OLED NOT FOUND\r\n");
        for (;;) { delay_ms(1000u); uart_puts("NO OLED\r\n"); }
    }

    uart_puts("WHALE ANIM  OLED OK\r\n");
    oled_init();
    delay_ms(50u);

    for (;;) {
        /* 取一帧到显存并刷新 */
        for (i = 0u; i < sizeof(fb); i++) { fb[i] = anim_whale[f][i]; }
        oled_flush();

        if (++hb >= 8u) { hb = 0u; uart_puts("RUN\r\n"); }
        f = (f + 1u) % ANIM_FRAMES;
        delay_ms(120u);
    }
}
