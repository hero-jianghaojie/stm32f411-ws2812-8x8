/* 按键扫描固件: 找出开发板上"用户按键"接在哪个引脚
 * 把 PA0-8/PA15、PB0-15、PC13 全部配置为带上拉的输入, 每 5ms 采样,
 * 有变化就通过 UART1(PA9/PA10, 115200) 打印 "PIN A0 -> 0" 之类。
 * 用法: 烧入后打开串口, 按一下按键, 看打印的引脚名。
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
#define GPIOA_PUPDR     REG32(0x4002000Cu)
#define GPIOA_IDR       REG32(0x40020010u)
#define GPIOA_AFRH      REG32(0x40020024u)
#define GPIOA_OSPEEDR   REG32(0x40020008u)

#define GPIOB_MODER     REG32(0x40020400u)
#define GPIOB_PUPDR     REG32(0x4002040Cu)
#define GPIOB_IDR       REG32(0x40020410u)

#define GPIOC_MODER     REG32(0x40020800u)
#define GPIOC_PUPDR     REG32(0x4002080Cu)
#define GPIOC_IDR       REG32(0x40020810u)

#define USART1_SR       REG32(0x40011000u)
#define USART1_DR       REG32(0x40011004u)
#define USART1_BRR      REG32(0x40011008u)
#define USART1_CR1      REG32(0x4001100Cu)

#define DEMCR           REG32(0xE000EDFCu)
#define DWT_CTRL        REG32(0xE0001000u)
#define DWT_CYCCNT      REG32(0xE0001004u)

#define SYS_HZ          100000000u

#define MASK_A  ((1u << 0) | (1u << 1) | (1u << 2) | (1u << 3) | (1u << 4) | \
                 (1u << 5) | (1u << 6) | (1u << 7) | (1u << 8) | (1u << 15))

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
    RCC_AHB1ENR |= (1u << 0) | (1u << 1) | (1u << 2);
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

static void uart_putu(uint32_t v)
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
    while (ms--) {
        for (i = 0u; i < 13333u; i++) { __asm volatile ("nop"); }
    }
}

static void dwt_init(void)
{
    DEMCR |= (1u << 24);
    DWT_CYCCNT = 0u;
    DWT_CTRL |= 1u;
}

static uint32_t ns2cyc(uint32_t ns) { return ((SYS_HZ / 1000u) * ns + 500000u) / 1000000u; }

/* 把所有候选脚设为输入 + 上拉 (按下对地 -> 读到 0) */
static void inputs_init(void)
{
    uint32_t i;
    for (i = 0u; i <= 8u; i++) {
        GPIOA_MODER &= ~(3u << (i * 2u));
        GPIOA_PUPDR = (GPIOA_PUPDR & ~(3u << (i * 2u))) | (1u << (i * 2u));
    }
    GPIOA_MODER &= ~(3u << 30);
    GPIOA_PUPDR = (GPIOA_PUPDR & ~(3u << 30)) | (1u << 30);

    GPIOB_MODER = 0u;
    GPIOB_PUPDR = 0x55555555u;

    GPIOC_MODER = (GPIOC_MODER & ~(3u << 26));
    GPIOC_PUPDR = (GPIOC_PUPDR & ~(3u << 26)) | (1u << 26);
}

static void report(char port, uint32_t pin, uint32_t level)
{
    uart_puts("PIN ");
    uart_putc(port);
    uart_putu(pin);
    uart_puts(level ? " -> 1" : " -> 0");
    uart_putc(13);
    uart_putc(10);
}

int main(void)
{
    uint32_t lastA, lastB, lastC, nowA, nowB, nowC, i, hb = 0u;

    clock_init();
    dwt_init();
    uart_init();
    inputs_init();
    delay_ms(30u);

    uart_puts("SCAN READY  press the button\r\n");

    lastA = GPIOA_IDR & MASK_A;
    lastB = GPIOB_IDR & 0xFFFFu;
    lastC = GPIOC_IDR & (1u << 13);

    for (;;) {
        delay_ms(5u);
        nowA = GPIOA_IDR & MASK_A;
        nowB = GPIOB_IDR & 0xFFFFu;
        nowC = GPIOC_IDR & (1u << 13);

        for (i = 0u; i < 16u; i++) {
            uint32_t m = 1u << i;
            if ((nowA ^ lastA) & m) { report('A', i, nowA & m); }
            if ((nowB ^ lastB) & m) { report('B', i, nowB & m); }
        }
        if ((nowC ^ lastC) & (1u << 13)) { report('C', 13u, nowC & (1u << 13)); }

        lastA = nowA; lastB = nowB; lastC = nowC;

        if (++hb >= 200u) { hb = 0u; uart_puts("SCAN\r\n"); }
    }
}
