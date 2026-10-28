/* STM32F411 (Black Pill) 驱动 WS2812B x64
 * 时钟: HSI 16MHz -> PLL 100MHz (不依赖外部晶振)
 * 数据脚: PA7 (DIN), 串 330R
 * 调试口: USART1 PA9/PA10 115200 8N1 (与 USB-TTL 复用, 烧完可直接看打印)
 * 上电: 红->绿->蓝->灭 自检, 然后彩虹流动
 */
#include <stdint.h>

#define REG32(a)            (*(volatile uint32_t *)(uint32_t)(a))

#ifndef WS_PIN
#define WS_PIN      7u
#endif
#define WS_BSRR     REG32(0x40020018u)
#define WS_SET()    (WS_BSRR = (1u << WS_PIN))
#define WS_CLR()    (WS_BSRR = (1u << (WS_PIN + 16u)))

#define RCC_CR          REG32(0x40023800u)
#define RCC_PLLCFGR     REG32(0x40023804u)
#define RCC_CFGR        REG32(0x40023808u)
#define RCC_AHB1ENR     REG32(0x40023830u)
#define RCC_APB2ENR     REG32(0x40023844u)
#define FLASH_ACR       REG32(0x40023C00u)

#define GPIOA_MODER     REG32(0x40020000u)
#define GPIOA_OSPEEDR   REG32(0x40020008u)
#define GPIOA_PUPDR     REG32(0x4002000Cu)
#define GPIOA_AFRH      REG32(0x40020024u)

#define USART1_SR       REG32(0x40011000u)
#define USART1_DR       REG32(0x40011004u)
#define USART1_BRR      REG32(0x40011008u)
#define USART1_CR1      REG32(0x4001100Cu)

#define DEMCR           REG32(0xE000EDFCu)
#define DWT_CTRL        REG32(0xE0001000u)
#define DWT_CYCCNT      REG32(0xE0001004u)

#define LED_COUNT       64u
#define SYS_HZ          100000000u

static uint8_t g_grb[LED_COUNT * 3u];

/* ---------- 时钟: HSI 16M / M8 * N200 / P4 = 100MHz ---------- */
static void clock_init(void)
{
    FLASH_ACR = (3u << 0) | (1u << 8) | (1u << 9);   /* 3WS + prefetch + ICEN */       /* 3WS + prefetch + I/D cache */

    RCC_PLLCFGR = (8u << 0) | (200u << 6) | (1u << 16) | (7u << 24);
    RCC_CR |= (1u << 24);                             /* PLLON */
    while (!(RCC_CR & (1u << 25))) { }

    RCC_CFGR = (RCC_CFGR & ~((0xFu << 4) | (7u << 10) | (7u << 11) | 3u))
             | (4u << 10)                             /* APB1 /2 = 50MHz */
             | 2u;                                    /* SW = PLL */
    while (((RCC_CFGR >> 2) & 3u) != 2u) { }
}

/* ---------- 串口打印 ---------- */
static void uart_init(void)
{
    RCC_AHB1ENR |= (1u << 0);
    RCC_APB2ENR |= (1u << 4);

    GPIOA_MODER = (GPIOA_MODER & ~((3u << 18) | (3u << 20))) | (2u << 18) | (2u << 20);
    GPIOA_OSPEEDR |= (3u << 18) | (3u << 20);
    GPIOA_AFRH = (GPIOA_AFRH & ~((0xFu << 4) | (0xFu << 8))) | (7u << 4) | (7u << 8);

    USART1_BRR = 868u;                                /* 100MHz / 115200 = 868.1 */
    USART1_CR1 = (1u << 13) | (1u << 3) | (1u << 2);
}

static void uart_putc(char c)
{
    while (!(USART1_SR & (1u << 7))) { }
    USART1_DR = (uint32_t)(uint8_t)c;
}

static void uart_puts(const char *s)
{
    while (*s) { uart_putc(*s++); }
}

static void ws_gpio_init(void)
{
    GPIOA_MODER = (GPIOA_MODER & ~(3u << (WS_PIN * 2u))) | (1u << (WS_PIN * 2u));
    GPIOA_OSPEEDR |= (3u << (WS_PIN * 2u));
    GPIOA_PUPDR = (GPIOA_PUPDR & ~(3u << (WS_PIN * 2u)));
    WS_CLR();
}

static void dwt_init(void)
{
    DEMCR |= (1u << 24);
    DWT_CYCCNT = 0u;
    DWT_CTRL |= 1u;
}

static uint32_t ns2cyc(uint32_t ns)
{
    return ((SYS_HZ / 1000u) * ns + 500000u) / 1000000u;
}

static inline void wait_until(uint32_t deadline)
{
    while ((int32_t)(DWT_CYCCNT - deadline) < 0) { }
}

static void delay_ms(uint32_t ms)
{
    uint32_t i;
    while (ms--) {
        for (i = 0u; i < 13333u; i++) {      /* 约 1ms @100MHz (粗略) */
            __asm volatile ("nop");
        }
    }
}

static void ws_send(void)
{
    const uint32_t t0h = ns2cyc(400u), t0l = ns2cyc(850u);
    const uint32_t t1h = ns2cyc(800u), t1l = ns2cyc(450u);
    uint32_t i, base;
    uint32_t n = sizeof(g_grb);

    __asm volatile ("cpsid i" ::: "memory");
    for (i = 0u; i < n; i++) {
        uint8_t b = g_grb[i];
        int32_t k;
        for (k = 7; k >= 0; k--) {
            base = DWT_CYCCNT;
            if (b & (1u << k)) {
                WS_SET(); wait_until(base + t1h);
                WS_CLR(); wait_until(base + t1h + t1l);
            } else {
                WS_SET(); wait_until(base + t0h);
                WS_CLR(); wait_until(base + t0h + t0l);
            }
        }
    }
    __asm volatile ("cpsie i" ::: "memory");
    delay_ms(1u);
}

static void set_all(uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t i;
    for (i = 0u; i < LED_COUNT; i++) {
        g_grb[i * 3u + 0u] = g;
        g_grb[i * 3u + 1u] = r;
        g_grb[i * 3u + 2u] = b;
    }
}

static void wheel(uint8_t p, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (p < 85u)       { *r = (uint8_t)(255u - p * 3u); *g = (uint8_t)(p * 3u);             *b = 0; }
    else if (p < 170u) { p = (uint8_t)(p - 85u);        *r = 0;                             *g = (uint8_t)(255u - p * 3u); *b = (uint8_t)(p * 3u); }
    else               { p = (uint8_t)(p - 170u);       *r = (uint8_t)(p * 3u);             *g = 0;                        *b = (uint8_t)(255u - p * 3u); }
}

int main(void)
{
    uint32_t i, base = 0u;

    clock_init();
    dwt_init();
    ws_gpio_init();
    uart_init();
    delay_ms(50u);

    uart_puts("\r\nF411-WS2812-OK  DIN=PA");
    uart_putc((char)('0' + WS_PIN));
    uart_puts("  64 LEDs  100MHz\r\n");

    set_all(40, 0, 0); ws_send(); delay_ms(400u);
    set_all(0, 40, 0); ws_send(); delay_ms(400u);
    set_all(0, 0, 40); ws_send(); delay_ms(400u);
    set_all(0, 0, 0);  ws_send(); delay_ms(200u);

    uint32_t hb = 0u;
    for (;;) {
        if (++hb >= 40u) {          /* 约每秒打印一次, 便于串口判断程序在跑 */
            hb = 0u;
            uart_puts("RUN");
            uart_putc(13);
            uart_putc(10);
        }
        for (i = 0u; i < LED_COUNT; i++) {
            uint8_t r, g, b;
            wheel((uint8_t)((i * 4u + base) & 0xFFu), &r, &g, &b);
            g_grb[i * 3u + 0u] = (uint8_t)(g >> 2);
            g_grb[i * 3u + 1u] = (uint8_t)(r >> 2);
            g_grb[i * 3u + 2u] = (uint8_t)(b >> 2);
        }
        ws_send();
        delay_ms(25u);
        base = (base + 4u) & 0xFFu;
    }
}
