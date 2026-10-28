/* WS2812 诊断固件: 走马灯逐颗点亮 + 串口报数
 * 目的: 判断"数据链能传到第几颗"以及"字节序是否正确"
 * 100MHz, DIN=PA7, USART1 PA9/PA10 115200
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

static void clock_init(void)
{
    FLASH_ACR = (3u << 0) | (1u << 8) | (1u << 9);   /* 3WS + prefetch + ICEN */
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

static void uart_puts(const char *s)
{
    while (*s) { uart_putc(*s++); }
}

static void uart_putu(uint32_t v)
{
    char b[11];
    int i = 0;
    if (!v) { uart_putc('0'); return; }
    while (v) { b[i++] = (char)('0' + v % 10u); v /= 10u; }
    while (i--) { uart_putc(b[i]); }
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

static void clear_all(void)
{
    uint32_t i;
    for (i = 0u; i < sizeof(g_grb); i++) { g_grb[i] = 0u; }
}

static void set_px(uint32_t i, uint8_t r, uint8_t g, uint8_t b)
{
    if (i >= LED_COUNT) return;
    g_grb[i * 3u + 0u] = g;
    g_grb[i * 3u + 1u] = r;
    g_grb[i * 3u + 2u] = b;
}

int main(void)
{
    uint32_t i;

    clock_init();
    dwt_init();
    ws_gpio_init();
    uart_init();
    delay_ms(30u);

    uart_puts("\r\nWS2812 DIAG\r\n");

    /* 阶段 1: 逐颗点亮 (红色, 全亮), 每颗 150ms */
    for (i = 0u; i < LED_COUNT; i++) {
        clear_all();
        set_px(i, 255, 0, 0);
        ws_send();
        uart_puts("step ");
        uart_putu(i);
        uart_puts("\r\n");
        delay_ms(150u);
    }

    /* 阶段 2: 全红 */
    uart_puts("ALL RED\r\n");
    clear_all();
    for (i = 0u; i < LED_COUNT; i++) { set_px(i, 255, 0, 0); }
    ws_send();
    delay_ms(800u);

    /* 阶段 3: 全绿 */
    uart_puts("ALL GREEN\r\n");
    clear_all();
    for (i = 0u; i < LED_COUNT; i++) { set_px(i, 0, 255, 0); }
    ws_send();
    delay_ms(800u);

    /* 阶段 4: 全蓝 */
    uart_puts("ALL BLUE\r\n");
    clear_all();
    for (i = 0u; i < LED_COUNT; i++) { set_px(i, 0, 0, 255); }
    ws_send();
    delay_ms(800u);

    /* 阶段 5: 呼吸白, 每 200ms 变一次亮度 */
    for (;;) {
        uint32_t lv;
        for (lv = 8u; lv <= 64u; lv += 8u) {
            clear_all();
            for (i = 0u; i < LED_COUNT; i++) {
                set_px(i, (uint8_t)lv, (uint8_t)lv, (uint8_t)lv);
            }
            ws_send();
            delay_ms(200u);
        }
        uart_puts("alive");
        for (lv = 64u; lv >= 8u; lv -= 8u) {
            clear_all();
            for (i = 0u; i < LED_COUNT; i++) {
                set_px(i, (uint8_t)lv, (uint8_t)lv, (uint8_t)lv);
            }
            ws_send();
            delay_ms(200u);
            if (lv <= 8u) break;
        }
    }
}
