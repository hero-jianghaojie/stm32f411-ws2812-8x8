/* WS2812 数据脚自动探测
 * 依次把每个候选引脚当数据脚发一帧 (全亮白 1.2 秒), 串口同步打印 "TRY PA5"。
 * 哪个脚亮, 哪个就是 DIN 接的那根 —— 看灯 + 对串口即可。
 * 候选: PA1..PA8, PA15, PB0..PB15 (PA0 是按键, PA9/PA10 是串口, 已排除)
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

static uint8_t  g_grb[LED_COUNT * 3u];
static uint32_t g_mask_a = 0u;
static uint32_t g_mask_b = 0u;

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

static void uart_dec(uint32_t v)
{
    char b[11];
    int i = 0;
    if (!v) { uart_putc('0'); return; }
    while (v) { b[i++] = (char)('0' + v % 10u); v /= 10u; }
    while (i--) { uart_putc(b[i]); }
}

static void dwt_init(void)
{
    DEMCR |= (1u << 24);
    DWT_CYCCNT = 0u;
    DWT_CTRL |= 1u;
}

static uint32_t ns2cyc(uint32_t ns) { return ((SYS_HZ / 1000u) * ns + 500000u) / 1000000u; }

static inline void wait_until(uint32_t d)
{
    while ((int32_t)(DWT_CYCCNT - d) < 0) { }
}

static void delay_ms(uint32_t ms)
{
    uint32_t i;
    while (ms--) { for (i = 0u; i < 13333u; i++) { __asm volatile ("nop"); } }
}

/* 只让当前的候选脚是推挽输出, 其余复位成高阻输入 */
static void select_pin(uint32_t port /*0=A,1=B*/, uint32_t pin)
{
    uint32_t i;

    /* 全部先设为输入 */
    for (i = 0u; i < 16u; i++) {
        if (i == 9u || i == 10u) { continue; }        /* 串口脚不动 */
        GPIOA_MODER &= ~(3u << (i * 2u));
        GPIOB_MODER &= ~(3u << (i * 2u));
    }
    GPIOA_PUPDR = 0u;                                  /* 不带上拉, 避免漏电 */
    GPIOB_PUPDR = 0u;

    g_mask_a = 0u;
    g_mask_b = 0u;

    if (port == 0u) {
        GPIOA_MODER = (GPIOA_MODER & ~(3u << (pin * 2u))) | (1u << (pin * 2u));
        GPIOA_OSPEEDR |= (3u << (pin * 2u));
        g_mask_a = (1u << pin);
    } else {
        GPIOB_MODER = (GPIOB_MODER & ~(3u << (pin * 2u))) | (1u << (pin * 2u));
        GPIOB_OSPEEDR |= (3u << (pin * 2u));
        g_mask_b = (1u << pin);
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
                GPIOA_BSRR = g_mask_a; GPIOB_BSRR = g_mask_b;
                wait_until(base + t1h);
                GPIOA_BSRR = g_mask_a << 16; GPIOB_BSRR = g_mask_b << 16;
                wait_until(base + t1h + t1l);
            } else {
                GPIOA_BSRR = g_mask_a; GPIOB_BSRR = g_mask_b;
                wait_until(base + t0h);
                GPIOA_BSRR = g_mask_a << 16; GPIOB_BSRR = g_mask_b << 16;
                wait_until(base + t0h + t0l);
            }
        }
    }
    __asm volatile ("cpsie i" ::: "memory");
    delay_ms(1u);
}

static void fill(uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t i;
    for (i = 0u; i < LED_COUNT; i++) {
        g_grb[i * 3u + 0u] = g;
        g_grb[i * 3u + 1u] = r;
        g_grb[i * 3u + 2u] = b;
    }
}

static void try_pin(uint32_t port, uint32_t pin)
{
    select_pin(port, pin);
    delay_ms(2u);

    uart_puts("TRY P");
    uart_putc(port ? 'B' : 'A');
    uart_dec(pin);
    uart_puts("\r\n");

    fill(60u, 60u, 60u);      /* 全亮白, 容易肉眼确认 */
    ws_send();
    delay_ms(1200u);

    fill(0u, 0u, 0u);
    ws_send();
    delay_ms(250u);
}

int main(void)
{
    uint32_t i;

    clock_init();
    dwt_init();
    uart_init();
    delay_ms(20u);

    uart_puts("\r\nWS2812 PIN SCAN  (watch the strip)\r\n");

    for (;;) {
        for (i = 1u; i <= 8u; i++) { try_pin(0u, i); }     /* PA1..PA8 */
        try_pin(0u, 15u);                                  /* PA15 */
        for (i = 0u; i <= 15u; i++) { try_pin(1u, i); }    /* PB0..PB15 */
        uart_puts("--- one full pass done, repeat ---\r\n");
    }
}
