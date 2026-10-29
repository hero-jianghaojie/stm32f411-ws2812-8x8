/* OLED (SSD1306 / SH1106) 测试固件 —— 只测屏, 不驱动 WS2812
 * 总线: 软件 I2C, PB6=SCL, PB7=SDA, 开漏 + 上拉
 * 地址: 先试 0x3C, 不通再试 0x3D
 * 显示: 页寻址(0xB0|page, 0x00|col低4位, 0x10|col高4位), 128x64 缓冲
 * 内容: 边框 + 棋盘 + 斜线 + 一条横向移动的竖条(动画)
 * 串口: USART1 PA9/PA10 115200, 打印检测结果与心跳
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

#define SCL_PIN 7u      /* 实测: 屏的 SCL 在 PB7 */
#define SDA_PIN 6u      /* 实测: 屏的 SDA 在 PB6 */

static uint8_t fb[128 * 8];          /* 128 列 x 8 页 */
static uint8_t oled_addr = 0x3Cu;

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

static void uart_hex8(uint32_t v)
{
    const char *h = "0123456789ABCDEF";
    uart_putc('0'); uart_putc('x');
    uart_putc(h[(v >> 4) & 0xFu]); uart_putc(h[v & 0xFu]);
}

static void delay_ms(uint32_t ms)
{
    uint32_t i;
    while (ms--) { for (i = 0u; i < 13333u; i++) { __asm volatile ("nop"); } }
}

/* ---------------- 软件 I2C ---------------- */
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

/* 一次事务: 地址 + 若干字节 */
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

/* ---------------- OLED ---------------- */
static void oled_cmds(const uint8_t *c, uint32_t n)
{
    uint8_t tmp[17];
    uint32_t i = 0u, k;
    /* 每条 I2C 事务最多带 16 个命令字节, 超出部分自动分批,
       否则像 0xAF(开显示) 这种排在序列末尾的命令会被丢掉 */
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
        0xAE,        /* display off */
        0xD5, 0x80,  /* clock div */
        0xA8, 0x3F,  /* multiplex 64 */
        0xD3, 0x00,  /* display offset */
        0x40,        /* start line 0 */
        0x8D, 0x14,  /* charge pump on */
        0xA1,        /* segment remap */
        0xC8,        /* COM scan dec */
        0xDA, 0x12,  /* COM pins */
        0x81, 0xCF,  /* contrast */
        0xD9, 0xF1,  /* precharge */
        0xDB, 0x40,  /* VCOMH */
        0xA4,        /* display on RAM */
        0xA6,        /* normal (not inverted) */
        0xAF         /* display on */
    };
    oled_cmds(seq, sizeof(seq));
}

static void oled_flush(void)
{
    uint8_t page, col;
    uint8_t hdr[3];
    uint8_t line[129];

    for (page = 0u; page < 8u; page++) {
        hdr[0] = 0x00u;                       /* 命令流 */
        hdr[1] = (uint8_t)(0xB0u | page);
        hdr[2] = 0x00u;                       /* 列低 4 位 */
        i2c_send(oled_addr, hdr, 3u);
        hdr[1] = 0x10u;                       /* 列高 4 位 */
        i2c_send(oled_addr, hdr, 3u);

        line[0] = 0x40u;                      /* Co=0, D/C#=1 -> 数据流 */
        for (col = 0u; col < 128u; col++) {
            line[1u + col] = fb[page * 128u + col];
        }
        i2c_send(oled_addr, line, 129u);
    }
}

static void draw_pixel(uint32_t x, uint32_t y, uint32_t on)
{
    if (x >= 128u || y >= 64u) return;
    if (on) { fb[(y / 8u) * 128u + x] |= (uint8_t)(1u << (y & 7u)); }
    else    { fb[(y / 8u) * 128u + x] &= (uint8_t)~(1u << (y & 7u)); }
}

static void clear_fb(void)
{
    uint32_t i;
    for (i = 0u; i < sizeof(fb); i++) { fb[i] = 0u; }
}

int main(void)
{
    uint32_t x, y, t = 0u;

    clock_init();
    uart_init();
    i2c_init();
    delay_ms(120u);                       /* OLED 上电稳定 */

    uart_puts("\r\nOLED TEST  SCL=PB6 SDA=PB7\r\n");

    /* 探测地址 */
    if (i2c_send(0x3Cu, (const uint8_t *)"\x00\xAE", 2u)) { oled_addr = 0x3Cu; }
    else if (i2c_send(0x3Du, (const uint8_t *)"\x00\xAE", 2u)) { oled_addr = 0x3Du; }
    else {
        uart_puts("OLED NOT FOUND (try 0x3C/0x3D)\r\n");
        for (;;) { delay_ms(1000u); uart_puts("NO OLED\r\n"); }
    }

    uart_puts("OLED AT ");
    uart_hex8(oled_addr);
    uart_puts("\r\n");

    oled_init();
    delay_ms(50u);

    /* 静态图案: 边框 + 棋盘 + 斜线 */
    clear_fb();
    for (x = 0u; x < 128u; x++) {
        draw_pixel(x, 0u, 1u); draw_pixel(x, 63u, 1u);
        draw_pixel(x, 1u, 1u); draw_pixel(x, 62u, 1u);
    }
    for (y = 0u; y < 64u; y++) {
        draw_pixel(0u, y, 1u); draw_pixel(127u, y, 1u);
        draw_pixel(1u, y, 1u); draw_pixel(126u, y, 1u);
    }
    for (y = 8u; y < 32u; y++) {
        for (x = 8u; x < 64u; x++) {
            if (((x / 8u) + (y / 8u)) & 1u) { draw_pixel(x, y, 1u); }
        }
    }
    for (x = 8u; x < 120u; x++) { draw_pixel(x, (x * 48u) / 120u + 40u, 1u); }
    oled_flush();
    delay_ms(800u);

    /* 动画: 竖条横向移动 */
    for (;;) {
        clear_fb();
        for (x = 0u; x < 128u; x++) {
            draw_pixel(x, 0u, 1u); draw_pixel(x, 63u, 1u);
        }
        for (y = 2u; y < 62u; y++) {
            uint32_t bx = (t * 2u) % 128u;
            draw_pixel(bx, y, 1u);
            draw_pixel((bx + 1u) % 128u, y, 1u);
        }
        oled_flush();
        if ((t % 8u) == 0u) { uart_puts("RUN\r\n"); }
        t++;
        delay_ms(60u);
    }
}
