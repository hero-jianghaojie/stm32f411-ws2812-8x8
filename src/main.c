/* STM32F411 串口 ISP 验证固件
 * - 不依赖外部晶振: 直接用 HSI 16 MHz, 任何 F411 板子都能跑
 * - USART1 (PA9=TX, PA10=RX) 115200 8N1 周期性打印
 * - PA5 每 500 ms 翻转一次 (Nucleo-F411RE 的 LD2)
 * 用途: 用 USB-TTL 走 ROM bootloader 烧进去, 复位后串口能收到 "F411-RUN-OK"
 *       即证明整条链路(串口 ISP -> Flash -> 运行)打通。
 */
#include <stdint.h>

#define REG32(a)        (*(volatile uint32_t *)(uint32_t)(a))

#define RCC_AHB1ENR     REG32(0x40023830u)
#define RCC_APB2ENR     REG32(0x40023844u)

#define GPIOA_MODER     REG32(0x40020000u)
#define GPIOA_OSPEEDR   REG32(0x40020008u)
#define GPIOA_PUPDR     REG32(0x4002000Cu)
#define GPIOA_BSRR      REG32(0x40020018u)
#define GPIOA_AFRH      REG32(0x40020024u)

#define USART1_SR       REG32(0x40011000u)
#define USART1_DR       REG32(0x40011004u)
#define USART1_BRR      REG32(0x40011008u)
#define USART1_CR1      REG32(0x4001100Cu)

#define DWT_CTRL        REG32(0xE0001000u)
#define DWT_CYCCNT      REG32(0xE0001004u)
#define DEMCR           REG32(0xE000EDFCu)

static void dwt_init(void)
{
    DEMCR |= (1u << 24);
    DWT_CYCCNT = 0u;
    DWT_CTRL |= 1u;
}

static void delay_ms(uint32_t ms)
{
    /* HSI 16 MHz: 1 ms = 16000 周期 */
    while (ms--) {
        uint32_t t0 = DWT_CYCCNT;
        while ((DWT_CYCCNT - t0) < 16000u) { }
    }
}

static void uart_init(void)
{
    RCC_AHB1ENR |= (1u << 0);          /* GPIOA */
    RCC_APB2ENR |= (1u << 4);          /* USART1 */

    /* PA9/PA10 -> AF7, 高速 */
    GPIOA_MODER = (GPIOA_MODER & ~((3u << 18) | (3u << 20))) | (2u << 18) | (2u << 20);
    GPIOA_OSPEEDR |= (3u << 18) | (3u << 20);
    GPIOA_PUPDR = (GPIOA_PUPDR & ~((3u << 18) | (3u << 20))) | (1u << 18);
    GPIOA_AFRH = (GPIOA_AFRH & ~((0xFu << 4) | (0xFu << 8)))
               | (7u << 4) | (7u << 8);

    /* PA5 推挽输出, 做 LED 指示 */
    GPIOA_MODER = (GPIOA_MODER & ~(3u << 10)) | (1u << 10);

    USART1_BRR = 139u;                 /* 16e6 / 115200 = 138.9 */
    USART1_CR1 = (1u << 13) | (1u << 3) | (1u << 2);   /* UE | TE | RE */
}

static void uart_putc(char c)
{
    while (!(USART1_SR & (1u << 7))) { }   /* TXE */
    USART1_DR = (uint32_t)(uint8_t)c;
}

static void uart_puts(const char *s)
{
    while (*s) { uart_putc(*s++); }
}

static void uart_putu(uint32_t v)
{
    char buf[12];
    int i = 0;
    if (!v) { uart_putc('0'); return; }
    while (v) { buf[i++] = (char)('0' + (v % 10u)); v /= 10u; }
    while (i--) { uart_putc(buf[i]); }
}

int main(void)
{
    uint32_t n = 0u;

    dwt_init();
    uart_init();
    delay_ms(20u);

    uart_puts("\r\nF411-RUN-OK\r\n");
    uart_puts("USART1 PA9/PA10 115200 8N1, HSI 16MHz\r\n");

    for (;;) {
        GPIOA_BSRR = (1u << 5);
        uart_puts("tick ");
        uart_putu(n++);
        uart_puts("\r\n");
        delay_ms(500u);
        GPIOA_BSRR = (1u << (5 + 16));
        delay_ms(500u);
    }
}
