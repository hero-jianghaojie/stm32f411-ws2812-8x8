/* 彩色 SPI TFT 测试固件（软件 SPI，不占硬件 SPI 引脚）
 *
 * 编译时用 -DTFT_CTRL=x 选择控制器：
 *   1 = ST7735   (128x160，常见 1.8 寸)
 *   2 = ST7789   (240x240，常见 1.14/1.3 寸)
 *   3 = ILI9341  (240x320，常见 2.4/2.8 寸)
 *
 * 接线（可改下面的 PIN_* 宏）：
 *   TFT 引脚      Black Pill
 *   VCC           3.3V
 *   GND           GND
 *   SCL / SCK     PB13
 *   SDA / MOSI    PB15
 *   CS            PB12
 *   DC / RS       PB14
 *   RES / RST     PB10
 *   BL / LED      3.3V（或经 100R 接 3.3V）
 *
 * 串口 USART1 PA9/PA10 115200：打印控制器名、分辨率与 RUN 心跳。
 *
 * 测试画面（循环）：
 *   1) 全屏 红/绿/蓝/白/黑，各 400 ms      —— 验证通信与颜色顺序
 *   2) 8 条彩条                            —— 验证数据顺序与像素排列
 *   3) 水平 RGB 渐变                       —— 验证 16 位色深
 *   4) 边框 + 方块移动                     —— 验证坐标/刷新
 */
#include <stdint.h>

#ifndef TFT_CTRL
#define TFT_CTRL 1
#endif

#if TFT_CTRL == 1
#define CTRL_NAME   "ST7735"
#define TFT_W       128
#define TFT_H       160
#define TFT_XOFF    0
#define TFT_YOFF    0
#elif TFT_CTRL == 2
#define CTRL_NAME   "ST7789"
#define TFT_W       240
#define TFT_H       240
#define TFT_XOFF    0
#define TFT_YOFF    0
#elif TFT_CTRL == 3
#define CTRL_NAME   "ILI9341"
#define TFT_W       240
#define TFT_H       320
#define TFT_XOFF    0
#define TFT_YOFF    0
#else
#error "TFT_CTRL must be 1(ST7735), 2(ST7789) or 3(ILI9341)"
#endif

/* ---------------- 引脚（默认 PB 组，避开 PA0 按键 / PA9 PA10 串口 / PB6 PB7 I2C） ---- */
#define PIN_SCK   13u   /* PB13 */
#define PIN_MOSI  15u   /* PB15 */
#define PIN_CS    12u   /* PB12 */
#define PIN_DC    14u   /* PB14 */
#define PIN_RST   10u   /* PB10 */

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
    const uint32_t pins[5] = { PIN_SCK, PIN_MOSI, PIN_CS, PIN_DC, PIN_RST };
    RCC_AHB1ENR |= (1u << 1);                         /* GPIOB */
    for (i = 0u; i < 5u; i++) {
        uint32_t p = pins[i];
        GPIOB_MODER = (GPIOB_MODER & ~(3u << (p * 2u))) | (1u << (p * 2u));
        GPIOB_OSPEEDR |= (3u << (p * 2u));
        GPIOB_PUPDR = (GPIOB_PUPDR & ~(3u << (p * 2u)));
    }
    CS_H();
    DC_DATA();
    SCLK_L();
    RST_H();
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

static void tft_cmd(uint8_t c)
{
    DC_CMD();
    CS_L();
    spi_byte(c);
    CS_H();
}

static void tft_data(uint8_t d)
{
    DC_DATA();
    CS_L();
    spi_byte(d);
    CS_H();
}

static void tft_cmd_data(uint8_t c, const uint8_t *d, uint32_t n)
{
    uint32_t i;
    tft_cmd(c);
    DC_DATA();
    CS_L();
    for (i = 0u; i < n; i++) { spi_byte(d[i]); }
    CS_H();
}

/* ---------------- 初始化序列 ---------------- */
static void tft_init(void)
{
    RST_L();
    delay_ms(20u);
    RST_H();
    delay_ms(120u);

#if TFT_CTRL == 1        /* ST7735 */
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

        tft_cmd(0x01);                      /* SWRESET */
        delay_ms(150u);
        tft_cmd(0x11);                      /* SLPOUT */
        delay_ms(120u);
        tft_cmd_data(0xB1, frm, 3u);
        tft_cmd_data(0xB2, frm, 3u);
        tft_cmd_data(0xB3, frm3, 6u);
        tft_cmd(0xB4); tft_data(0x07);      /* INVCTR */
        tft_cmd_data(0xC0, pw1, 3u);
        tft_cmd(0xC1); tft_data(0xC5);
        tft_cmd_data(0xC2, pw3, 2u);
        tft_cmd_data(0xC3, pw4, 2u);
        tft_cmd_data(0xC4, pw5, 2u);
        tft_cmd(0xC5); tft_data(0x0E);
        tft_cmd(0x20);                      /* INVOFF */
        tft_cmd(0x36); tft_data(0xC8);      /* MADCTL */
        tft_cmd(0x3A); tft_data(0x05);      /* 16bit */
        tft_cmd_data(0xE0, pgam, 16u);
        tft_cmd_data(0xE1, ngam, 16u);
        tft_cmd(0x13); delay_ms(10u);
        tft_cmd(0x29); delay_ms(100u);
    }
#elif TFT_CTRL == 2      /* ST7789 */
    {
        static const uint8_t porctrl[] = { 0x0C, 0x0C, 0x00, 0x33, 0x33 };
        static const uint8_t pwctrl1[] = { 0xA4, 0xA1 };
        static const uint8_t pgam[] = { 0xD0, 0x04, 0x0D, 0x11, 0x13, 0x2B, 0x3F,
                                        0x54, 0x4C, 0x18, 0x0D, 0x0B, 0x1F, 0x23 };
        static const uint8_t ngam[] = { 0xD0, 0x04, 0x0C, 0x11, 0x13, 0x2C, 0x3F,
                                        0x44, 0x51, 0x2F, 0x1F, 0x1F, 0x20, 0x23 };

        tft_cmd(0x01); delay_ms(150u);
        tft_cmd(0x11); delay_ms(120u);
        tft_cmd(0x36); tft_data(0x00);      /* MADCTL */
        tft_cmd(0x3A); tft_data(0x55);      /* 16bit */
        tft_cmd_data(0xB2, porctrl, 5u);
        tft_cmd(0xB7); tft_data(0x35);
        tft_cmd(0xBB); tft_data(0x19);
        tft_cmd(0xC0); tft_data(0x2C);
        tft_cmd(0xC2); tft_data(0x01);
        tft_cmd(0xC3); tft_data(0x12);
        tft_cmd(0xC4); tft_data(0x20);
        tft_cmd(0xC6); tft_data(0x0F);
        tft_cmd_data(0xD0, pwctrl1, 2u);
        tft_cmd_data(0xE0, pgam, 14u);
        tft_cmd_data(0xE1, ngam, 14u);
        tft_cmd(0x21);                      /* INVON */
        tft_cmd(0x13); delay_ms(10u);
        tft_cmd(0x29); delay_ms(100u);
    }
#else                    /* ILI9341 */
    {
        static const uint8_t pwr1[] = { 0x23 };
        static const uint8_t pwr2[] = { 0x10 };
        static const uint8_t vm[]   = { 0x3E, 0x28 };
        static const uint8_t gvdd[] = { 0x86 };
        static const uint8_t vcm[]  = { 0x00, 0xC1, 0x30 };
        static const uint8_t pwr_a[] = { 0x64, 0x03, 0x12, 0x81 };
        static const uint8_t dtca[] = { 0x85, 0x00, 0x78 };
        static const uint8_t pwr_b[] = { 0x39, 0x2C, 0x00, 0x34, 0x02 };
        static const uint8_t pmpr[] = { 0x20 };
        static const uint8_t pgam[] = { 0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E,
                                        0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00 };
        static const uint8_t ngam[] = { 0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31,
                                        0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F };

        tft_cmd(0x01); delay_ms(50u);
        tft_cmd(0x28);
        tft_cmd_data(0xCF, vcm, 3u);
        tft_cmd_data(0xED, pwr_a, 4u);
        tft_cmd_data(0xE8, dtca, 3u);
        tft_cmd_data(0xCB, pwr_b, 5u);
        tft_cmd_data(0xF7, pmpr, 1u);
        tft_cmd_data(0xC0, pwr1, 1u);
        tft_cmd_data(0xC1, pwr2, 1u);
        tft_cmd_data(0xC5, vm, 2u);
        tft_cmd_data(0xC7, gvdd, 1u);
        tft_cmd(0x36); tft_data(0x48);      /* MADCTL: MX+BGR */
        tft_cmd(0x3A); tft_data(0x55);      /* 16bit */
        tft_cmd(0x21);
        tft_cmd_data(0xE0, pgam, 15u);
        tft_cmd_data(0xE1, ngam, 15u);
        tft_cmd(0x11); delay_ms(120u);
        tft_cmd(0x29); delay_ms(50u);
    }
#endif
}

/* ---------------- 绘图 ---------------- */
static void tft_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t buf[4];
    x0 += TFT_XOFF; x1 += TFT_XOFF;
    y0 += TFT_YOFF; y1 += TFT_YOFF;

    tft_cmd(0x2A);                      /* CASET */
    buf[0] = (uint8_t)(x0 >> 8); buf[1] = (uint8_t)x0;
    buf[2] = (uint8_t)(x1 >> 8); buf[3] = (uint8_t)x1;
    tft_cmd_data(0x2A, buf, 4u);

    buf[0] = (uint8_t)(y0 >> 8); buf[1] = (uint8_t)y0;
    buf[2] = (uint8_t)(y1 >> 8); buf[3] = (uint8_t)y1;
    tft_cmd_data(0x2B, buf, 4u);

    tft_cmd(0x2C);                      /* RAMWR */
}

static void tft_fill(uint16_t c)
{
    uint32_t i;
    uint32_t n = (uint32_t)TFT_W * TFT_H;
    uint8_t hi = (uint8_t)(c >> 8), lo = (uint8_t)c;

    tft_window(0u, 0u, TFT_W - 1u, TFT_H - 1u);
    DC_DATA();
    CS_L();
    for (i = 0u; i < n; i++) {
        spi_byte(hi);
        spi_byte(lo);
    }
    CS_H();
}

/* RGB565 */
#define RGB(r, g, b)  (uint16_t)((((r) & 0xF8u) << 8) | (((g) & 0xFCu) << 3) | (((b) & 0xF8u) >> 3))

static void tft_bars(void)
{
    static const uint16_t col[8] = {
        0xFFFF, 0xFFE0, 0x07FF, 0x07E0, 0xF81F, 0xF800, 0x001F, 0x0000
    };
    uint32_t b, x, y;
    uint32_t bw = TFT_W / 8u;

    for (b = 0u; b < 8u; b++) {
        uint16_t x0 = (uint16_t)(b * bw);
        uint16_t x1 = (uint16_t)((b == 7u) ? (TFT_W - 1u) : (x0 + bw - 1u));
        tft_window(x0, 0u, x1, TFT_H - 1u);
        DC_DATA(); CS_L();
        for (y = 0u; y < TFT_H; y++) {
            for (x = 0u; x < (uint32_t)(x1 - x0 + 1u); x++) {
                spi_byte((uint8_t)(col[b] >> 8));
                spi_byte((uint8_t)(col[b] & 0xFFu));
            }
        }
        CS_H();
    }
}

static void tft_gradient(void)
{
    uint32_t x, y;
    tft_window(0u, 0u, TFT_W - 1u, TFT_H - 1u);
    DC_DATA(); CS_L();
    for (y = 0u; y < TFT_H; y++) {
        for (x = 0u; x < TFT_W; x++) {
            uint16_t c = RGB((uint8_t)(x * 255u / (TFT_W - 1u)),
                             (uint8_t)(y * 255u / (TFT_H - 1u)),
                             (uint8_t)(255u - x * 255u / (TFT_W - 1u)));
            spi_byte((uint8_t)(c >> 8));
            spi_byte((uint8_t)(c & 0xFFu));
        }
    }
    CS_H();
}

static void tft_rect_fill(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t c)
{
    uint32_t i, n = (uint32_t)w * h;
    tft_window(x, y, (uint16_t)(x + w - 1u), (uint16_t)(y + h - 1u));
    DC_DATA(); CS_L();
    for (i = 0u; i < n; i++) {
        spi_byte((uint8_t)(c >> 8));
        spi_byte((uint8_t)(c & 0xFFu));
    }
    CS_H();
}

static void tft_border(uint16_t c)
{
    tft_rect_fill(0u, 0u, TFT_W, 3u, c);
    tft_rect_fill(0u, (uint16_t)(TFT_H - 3u), TFT_W, 3u, c);
    tft_rect_fill(0u, 0u, 3u, TFT_H, c);
    tft_rect_fill((uint16_t)(TFT_W - 3u), 0u, 3u, TFT_H, c);
}

static void tft_square(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t c)
{
    uint32_t i, n = (uint32_t)w * h;
    tft_window(x, y, (uint16_t)(x + w - 1u), (uint16_t)(y + h - 1u));
    DC_DATA(); CS_L();
    for (i = 0u; i < n; i++) {
        spi_byte((uint8_t)(c >> 8));
        spi_byte((uint8_t)(c & 0xFFu));
    }
    CS_H();
}

int main(void)
{
    uint32_t t = 0u, hb = 0u;

    clock_init();
    uart_init();
    pins_init();
    delay_ms(50u);

    uart_puts("\r\nTFT TEST  ctrl=");
    uart_puts(CTRL_NAME);
    uart_puts("  ");
    uart_dec(TFT_W);
    uart_puts("x");
    uart_dec(TFT_H);
    uart_puts("\r\n");

    tft_init();
    uart_puts("init done\r\n");

    for (;;) {
        /* 1) 纯色 */
        tft_fill(0xF800u); uart_puts("RED\r\n");    delay_ms(400u);
        tft_fill(0x07E0u); uart_puts("GREEN\r\n");  delay_ms(400u);
        tft_fill(0x001Fu); uart_puts("BLUE\r\n");   delay_ms(400u);
        tft_fill(0xFFFFu);                          delay_ms(400u);
        tft_fill(0x0000u);                          delay_ms(300u);

        /* 2) 彩条 */
        tft_bars(); uart_puts("BARS\r\n"); delay_ms(1200u);

        /* 3) 渐变 */
        tft_gradient(); uart_puts("GRAD\r\n"); delay_ms(1500u);

        /* 4) 方块移动 */
        tft_fill(0x0000u);
        tft_border(0xFFFFu);
        {
            uint32_t i;
            uint32_t n = (TFT_W > TFT_H) ? TFT_H : TFT_W;
            for (i = 0u; i < n; i++) {
                uint16_t x = (uint16_t)(i * (TFT_W - 20u) / (n - 1u));
                uint16_t y = (uint16_t)(i * (TFT_H - 20u) / (n - 1u));
                tft_square(x, y, 20u, 20u, 0x07FFu);
                if ((i % 20u) == 0u) { tft_fill(0x0000u); tft_border(0xFFFFu); }
                delay_ms(8u);
            }
        }
        uart_puts("MOVE\r\n");

        if (++hb >= 1u) { hb = 0u; }
        t++;
    }
}
