/* 1.44 寸 ST7735 (128x128) 播放"大肥鱼"壁纸帧序列
 *
 * 素材: src/anim_tft.h (由 tools/gen_tft_video.py 从视频生成, RGB565 128x128)
 * 显示: 软件 SPI, 与 tft_144 相同的接线与初始化
 *   SC -> PB13   DI -> PB15   RS -> PB14   CS -> PB12   RST -> PB10   BLK -> 3.3V
 *
 * 编译开关（与 tft_test.c 一致）：
 *   -DTFT_XOFF=2 -DTFT_YOFF=1   面板偏移（1.44 寸默认）
 *   -DTFT_INVERT=1             颜色反相
 */
#include <stdint.h>
#include "anim_tft.h"

#ifndef TFT_XOFF
#define TFT_XOFF 2
#endif
#ifndef TFT_YOFF
#define TFT_YOFF 1
#endif
#ifndef TFT_INVERT
#define TFT_INVERT 0
#endif
#ifndef FRAME_MS
#define FRAME_MS 100        /* 每帧停留毫秒, 10 fps */
#endif

#define TFT_W 128u
#define TFT_H 128u

/* ---------------- 引脚 ---------------- */
#define PIN_SCK   13u
#define PIN_MOSI  15u
#define PIN_CS    12u
#define PIN_DC    14u
#define PIN_RST   10u

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
#define GPIOB_OSPEEDR   REG32(0x40020408u)
#define GPIOB_PUPDR     REG32(0x4002040Cu)
#define GPIOB_BSRR      REG32(0x40020418u)

#define USART1_SR       REG32(0x40011000u)
#define USART1_DR       REG32(0x40011004u)
#define USART1_BRR      REG32(0x40011008u)
#define USART1_CR1      REG32(0x4001100Cu)

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

static void uart_dec(uint32_t v)
{
    char b[12];
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

/* ---------------- 软件 SPI ---------------- */
#define SCLK_H()  (GPIOB_BSRR = (1u << PIN_SCK))
#define SCLK_L()  (GPIOB_BSRR = (1u << (PIN_SCK + 16u)))
#define MOSI_H()  (GPIOB_BSRR = (1u << PIN_MOSI))
#define MOSI_L()  (GPIOB_BSRR = (1u << (PIN_MOSI + 16u)))
#define CS_L()    (GPIOB_BSRR = (1u << (PIN_CS + 16u)))
#define CS_H()    (GPIOB_BSRR = (1u << PIN_CS))
#define DC_CMD()  (GPIOB_BSRR = (1u << (PIN_DC + 16u)))
#define DC_DATA() (GPIOB_BSRR = (1u << PIN_DC))
#define RST_L()   (GPIOB_BSRR = (1u << (PIN_RST + 16u)))
#define RST_H()   (GPIOB_BSRR = (1u << PIN_RST))

static void pins_init(void)
{
    uint32_t i;
    const uint32_t p[5] = { PIN_SCK, PIN_MOSI, PIN_CS, PIN_DC, PIN_RST };
    RCC_AHB1ENR |= (1u << 1);
    for (i = 0u; i < 5u; i++) {
        GPIOB_MODER = (GPIOB_MODER & ~(3u << (p[i] * 2u))) | (1u << (p[i] * 2u));
        GPIOB_OSPEEDR |= (3u << (p[i] * 2u));
        GPIOB_PUPDR = (GPIOB_PUPDR & ~(3u << (p[i] * 2u)));
    }
    CS_H(); DC_DATA(); SCLK_L(); RST_H();
}

static void spi_byte(uint8_t b)
{
    int i;
    for (i = 7; i >= 0; i--) {
        if (b & (1u << i)) { MOSI_H(); } else { MOSI_L(); }
        SCLK_H();
        SCLK_L();
    }
}

static void tft_cmd(uint8_t c)  { DC_CMD();  CS_L(); spi_byte(c); CS_H(); }
static void tft_data(uint8_t d) { DC_DATA(); CS_L(); spi_byte(d); CS_H(); }

static void tft_cmd_data(uint8_t c, const uint8_t *d, uint32_t n)
{
    uint32_t i;
    tft_cmd(c);
    DC_DATA(); CS_L();
    for (i = 0u; i < n; i++) { spi_byte(d[i]); }
    CS_H();
}

static void tft_init(void)
{
    static const uint8_t frm[] = { 0x01, 0x2C, 0x2D };
    static const uint8_t frm3[] = { 0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D };
    static const uint8_t pw1[] = { 0xA2, 0x02, 0x84 };
    static const uint8_t pw3[] = { 0x0A, 0x00 };
    static const uint8_t pw4[] = { 0x8A, 0x2A };
    static const uint8_t pw5[] = { 0x8A, 0xEE };
    static const uint8_t pgam[] = { 0x02, 0x1C, 0x07, 0x12, 0x37, 0x32, 0x29, 0x2D,
                                    0x29, 0x25, 0x2B, 0x39, 0x00, 0x01, 0x03, 0x10 };
    static const uint8_t ngam[] = { 0x03, 0x1D, 0x07, 0x06, 0x2E, 0x2C, 0x29, 0x2D,
                                    0x2E, 0x2E, 0x37, 0x3F, 0x00, 0x00, 0x02, 0x10 };

    RST_L(); delay_ms(20u); RST_H(); delay_ms(120u);

    tft_cmd(0x01); delay_ms(150u);
    tft_cmd(0x11); delay_ms(120u);
    tft_cmd_data(0xB1, frm, 3u);
    tft_cmd_data(0xB2, frm, 3u);
    tft_cmd_data(0xB3, frm3, 6u);
    tft_cmd(0xB4); tft_data(0x07);
    tft_cmd_data(0xC0, pw1, 3u);
    tft_cmd(0xC1); tft_data(0xC5);
    tft_cmd_data(0xC2, pw3, 2u);
    tft_cmd_data(0xC3, pw4, 2u);
    tft_cmd_data(0xC4, pw5, 2u);
    tft_cmd(0xC5); tft_data(0x0E);
#if TFT_INVERT
    tft_cmd(0x21);
#else
    tft_cmd(0x20);
#endif
    tft_cmd(0x36); tft_data(0xC8);
    tft_cmd(0x3A); tft_data(0x05);
    tft_cmd_data(0xE0, pgam, 16u);
    tft_cmd_data(0xE1, ngam, 16u);
    tft_cmd(0x13); delay_ms(10u);
    tft_cmd(0x29); delay_ms(100u);
}

static void tft_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t buf[4];
    x0 += TFT_XOFF; x1 += TFT_XOFF;
    y0 += TFT_YOFF; y1 += TFT_YOFF;

    buf[0] = (uint8_t)(x0 >> 8); buf[1] = (uint8_t)x0;
    buf[2] = (uint8_t)(x1 >> 8); buf[3] = (uint8_t)x1;
    tft_cmd_data(0x2A, buf, 4u);

    buf[0] = (uint8_t)(y0 >> 8); buf[1] = (uint8_t)y0;
    buf[2] = (uint8_t)(y1 >> 8); buf[3] = (uint8_t)y1;
    tft_cmd_data(0x2B, buf, 4u);

    tft_cmd(0x2C);
}

int main(void)
{
    uint32_t f = 0u, i, hb = 0u;

    clock_init();
    uart_init();
    pins_init();
    delay_ms(50u);

    uart_puts("\r\nTFT WALLPAPER 128x128  frames=");
    uart_dec(TFT_FRAMES);
    uart_puts("\r\n");

    tft_init();
    uart_puts("init done\r\n");

    tft_window(0u, 0u, TFT_W - 1u, TFT_H - 1u);

    for (;;) {
        const unsigned char *src = anim_tft[f];
        DC_DATA();
        CS_L();
        for (i = 0u; i < TFT_FRAME_BYTES; i++) { spi_byte(src[i]); }
        CS_H();

        f = (f + 1u) % TFT_FRAMES;
        if (++hb >= TFT_FRAMES) { hb = 0u; uart_puts("LOOP\r\n"); }
        delay_ms(FRAME_MS);
    }
}
