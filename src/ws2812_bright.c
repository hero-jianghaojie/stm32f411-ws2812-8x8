/* STM32F411 WS2812 全引脚版
 * 同一路 WS2812 波形同时输出到 PA0~PA8、PA15、PB0~PB15，
 * 这样无论灯板 DIN 接在哪个可用引脚上都能亮（PA9/PA10 保留给调试串口）。
 * 时钟: HSI 16MHz -> PLL 100MHz, 3WS
 * 串口: USART1 PA9/PA10 115200, 每秒打印 RUN
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
#define GPIOA_PUPDR     REG32(0x4002000Cu)
#define GPIOA_BSRR      REG32(0x40020018u)
#define GPIOA_AFRH      REG32(0x40020024u)

#define GPIOB_MODER     REG32(0x40020400u)
#define GPIOB_OSPEEDR   REG32(0x40020408u)
#define GPIOB_PUPDR     REG32(0x4002040Cu)
#define GPIOB_BSRR      REG32(0x40020418u)

#define USART1_SR       REG32(0x40011000u)
#define USART1_DR       REG32(0x40011004u)
#define USART1_BRR      REG32(0x40011008u)
#define USART1_CR1      REG32(0x4001100Cu)

#define DEMCR           REG32(0xE000EDFCu)
#define DWT_CTRL        REG32(0xE0001000u)
#define DWT_CYCCNT      REG32(0xE0001004u)

#define LED_COUNT       64u
#define SYS_HZ          100000000u

/* PA0..PA8 与 PA15 */
#define MASK_A  ((1u << 0) | (1u << 1) | (1u << 2) | (1u << 3) | (1u << 4) | \
                 (1u << 5) | (1u << 6) | (1u << 7) | (1u << 8) | (1u << 15))
/* PB0..PB15 */
#define MASK_B  0xFFFFu

static uint8_t g_grb[LED_COUNT * 3u];

static void ws_set(void) { GPIOA_BSRR = MASK_A; GPIOB_BSRR = MASK_B; }
static void ws_clr(void) { GPIOA_BSRR = MASK_A << 16; GPIOB_BSRR = MASK_B << 16; }

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

static void pins_init(void)
{
    uint32_t i;
    for (i = 0u; i <= 8u; i++) {
        GPIOA_MODER = (GPIOA_MODER & ~(3u << (i * 2u))) | (1u << (i * 2u));
        GPIOA_OSPEEDR |= (3u << (i * 2u));
        GPIOA_PUPDR = (GPIOA_PUPDR & ~(3u << (i * 2u)));
    }
    GPIOA_MODER = (GPIOA_MODER & ~(3u << 30)) | (1u << 30);   /* PA15 */
    GPIOA_OSPEEDR |= (3u << 30);
    GPIOB_MODER = 0x55555555u;                                 /* PB0..15 输出 */
    GPIOB_OSPEEDR = 0xFFFFFFFFu;
    GPIOB_PUPDR = 0u;
    ws_clr();
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

static inline void wait_until(uint32_t d)
{
    while ((int32_t)(DWT_CYCCNT - d) < 0) { }
}

static void delay_ms(uint32_t ms)
{
    uint32_t i;
    while (ms--) {
        for (i = 0u; i < 13333u; i++) { __asm volatile ("nop"); }
    }
}

static void ws_send(void)
{
    const uint32_t t0h = ns2cyc(350u), t0l = ns2cyc(900u);
    const uint32_t t1h = ns2cyc(700u), t1l = ns2cyc(550u);
    uint32_t i, base;
    uint32_t n = sizeof(g_grb);

    __asm volatile ("cpsid i" ::: "memory");
    for (i = 0u; i < n; i++) {
        uint8_t b = g_grb[i];
        int32_t k;
        for (k = 7; k >= 0; k--) {
            base = DWT_CYCCNT;
            if (b & (1u << k)) {
                ws_set(); wait_until(base + t1h);
                ws_clr(); wait_until(base + t1h + t1l);
            } else {
                ws_set(); wait_until(base + t0h);
                ws_clr(); wait_until(base + t0h + t0l);
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
    uint32_t i, base = 0u, hb = 0u;

    clock_init();
    dwt_init();
    uart_init();
    pins_init();
    delay_ms(50u);

    uart_puts("F411-WS2812-BRIGHT  full brightness\r\n");

    set_all(30, 30, 30); ws_send(); delay_ms(500u);   /* 低亮白 */
    set_all(60, 60, 60); ws_send(); delay_ms(500u);
    set_all(120, 120, 120); ws_send(); delay_ms(500u);
    set_all(255, 0, 0); ws_send(); delay_ms(600u);    /* 全红 */
    set_all(0, 255, 0); ws_send(); delay_ms(600u);    /* 全绿 */
    set_all(0, 0, 255); ws_send(); delay_ms(600u);    /* 全蓝 */
    set_all(0, 0, 0);  ws_send(); delay_ms(200u);

    for (;;) {
        if (++hb >= 40u) { hb = 0u; uart_puts("RUN"); uart_putc(13); uart_putc(10); }
        for (i = 0u; i < LED_COUNT; i++) {
            uint8_t r, g, b;
            wheel((uint8_t)((i * 4u + base) & 0xFFu), &r, &g, &b);
            g_grb[i * 3u + 0u] = g;
            g_grb[i * 3u + 1u] = r;
            g_grb[i * 3u + 2u] = b;
        }
        ws_send();
        delay_ms(25u);
        base = (base + 4u) & 0xFFu;
    }
}
