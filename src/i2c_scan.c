/* I2C 双向扫描(诊断版): 同一组引脚按两种 SCL/SDA 组合各扫一遍
 *   第 1 遍: SCL=PB6, SDA=PB7
 *   第 2 遍: SCL=PB7, SDA=PB6   (若这一遍才找到器件, 说明两根线接反了)
 * 每 3 秒重复, 输出: IDLE 电平 / FOUND 0xNN / TOTAL n
 */
#include <stdint.h>

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

static uint32_t g_scl = 6u, g_sda = 7u;

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

static void uart_hex8(uint32_t v)
{
    const char *h = "0123456789ABCDEF";
    uart_putc('0'); uart_putc('x');
    uart_putc(h[(v >> 4) & 0xFu]); uart_putc(h[v & 0xFu]);
}

static void uart_dec(uint32_t v)
{
    char b[11];
    int i = 0;
    if (!v) { uart_putc('0'); return; }
    while (v) { b[i++] = (char)('0' + v % 10u); v /= 10u; }
    while (i--) { uart_putc(b[i]); }
}

static void delay_ms(uint32_t ms)
{
    uint32_t i;
    while (ms--) { for (i = 0u; i < 13333u; i++) { __asm volatile ("nop"); } }
}

static void i2c_delay(void) { volatile uint32_t i; for (i = 0u; i < 30u; i++) { } }

static void scl_hi(void) { GPIOB_ODR |= (1u << g_scl); }
static void scl_lo(void) { GPIOB_ODR &= ~(1u << g_scl); }
static void sda_hi(void) { GPIOB_ODR |= (1u << g_sda); }
static void sda_lo(void) { GPIOB_ODR &= ~(1u << g_sda); }
static uint32_t sda_read(void) { return (GPIOB_IDR >> g_sda) & 1u; }

static void i2c_init(uint32_t scl, uint32_t sda)
{
    uint32_t mask;
    g_scl = scl; g_sda = sda;
    mask = (3u << (scl * 2u)) | (3u << (sda * 2u));

    RCC_AHB1ENR |= (1u << 1);
    GPIOB_MODER = (GPIOB_MODER & ~mask) | (1u << (scl * 2u)) | (1u << (sda * 2u));
    GPIOB_OTYPER |= (1u << scl) | (1u << sda);
    GPIOB_OSPEEDR |= (1u << (scl * 2u)) | (1u << (sda * 2u));
    GPIOB_PUPDR = (GPIOB_PUPDR & ~mask) | (1u << (scl * 2u)) | (1u << (sda * 2u));
    scl_hi(); sda_hi();
    delay_ms(1u);
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

static uint32_t i2c_write_byte(uint8_t b)
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

static uint32_t i2c_probe(uint8_t addr7)
{
    uint32_t ok;
    i2c_start();
    ok = i2c_write_byte((uint8_t)(addr7 << 1));
    i2c_stop();
    i2c_delay();
    return ok;
}

static void one_pass(uint32_t scl, uint32_t sda, const char *tag)
{
    uint32_t a, n = 0u;

    i2c_init(scl, sda);
    uart_puts("\r\n");
    uart_puts(tag);
    uart_puts(" SCL=PB");
    uart_dec(scl);
    uart_puts(" SDA=PB");
    uart_dec(sda);
    uart_puts("\r\nIDLE SCL=");
    uart_putc(((GPIOB_IDR >> scl) & 1u) ? '1' : '0');
    uart_puts(" SDA=");
    uart_putc(((GPIOB_IDR >> sda) & 1u) ? '1' : '0');
    uart_puts("\r\n");

    for (a = 0x08u; a <= 0x77u; a++) {
        if (i2c_probe((uint8_t)a)) {
            uart_puts("FOUND ");
            uart_hex8(a);
            uart_puts("\r\n");
            n++;
        }
    }
    uart_puts("TOTAL ");
    uart_dec(n);
    uart_puts(" DEVICE(S)\r\n");
    if (n == 0u) { uart_puts("NONE: wiring / power / SPI module?\r\n"); }
    if (n > 3u) { uart_puts("TOO MANY: SDA may be stuck low\r\n"); }
}

int main(void)
{
    uint32_t i;

    clock_init();
    uart_init();
    delay_ms(120u);

    uart_puts("I2C DUAL-PASS SCAN\r\n");

    for (;;) {
        one_pass(6u, 7u, "PASS1");
        one_pass(7u, 6u, "PASS2");
        for (i = 0u; i < 6u; i++) { delay_ms(500u); }
    }
}
