/* WS2812 数据脚可选版（运行时切换 + 掉电记忆）
 *
 * 用法：
 *   短按 KEY(PA0)  -> 切到下一个候选脚，并立刻发一帧全白让你看清是哪个
 *   长按 KEY(2 s)  -> 把当前选择写入 Flash (sector 7 @0x08060000)，掉电不丢
 *   开机           -> 从 Flash 读回上次选择；若从未保存过，默认"全部广播"
 *
 * 串口(PA9/PA10, 115200) 会打印当前状态：
 *   SEL BROADCAST / SEL PA5  / SEL PB12 ...
 * 候选：PA1..PA8, PA15, PB0..PB15, 以及第 0 项"BROADCAST"（全部脚）
 * 排除：PA0(按键) PA9/PA10(串口) PA13/PA14(SWD)
 */
#include <stdint.h>

#define REG32(a)            (*(volatile uint32_t *)(uint32_t)(a))

#define RCC_CR          REG32(0x40023800u)
#define RCC_PLLCFGR     REG32(0x40023804u)
#define RCC_CFGR        REG32(0x40023808u)
#define RCC_AHB1ENR     REG32(0x40023830u)
#define RCC_APB2ENR     REG32(0x40023844u)
#define FLASH_ACR       REG32(0x40023C00u)
#define FLASH_KEYR      REG32(0x40023C04u)
#define FLASH_SR        REG32(0x40023C0Cu)
#define FLASH_CR        REG32(0x40023C10u)

#define GPIOA_MODER     REG32(0x40020000u)
#define GPIOA_OSPEEDR   REG32(0x40020008u)
#define GPIOA_PUPDR     REG32(0x4002000Cu)
#define GPIOA_IDR       REG32(0x40020010u)
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
#define FB_ADDR         0x08060000u          /* sector 7 起始 */
#define FB_MAGIC        0x57533238u          /* "WS28" */

/* 候选表: port(0=A,1=B), pin; 第 0 项为广播 */
typedef struct { uint8_t port; uint8_t pin; } pin_t;
static const pin_t g_cand[] = {
    {0u, 0xFFu},                                   /* 0: BROADCAST */
    {0u, 1u}, {0u, 2u}, {0u, 3u}, {0u, 4u},
    {0u, 5u}, {0u, 6u}, {0u, 7u}, {0u, 8u},
    {0u, 15u},
    {1u, 0u}, {1u, 1u}, {1u, 2u}, {1u, 3u}, {1u, 4u}, {1u, 5u},
    {1u, 6u}, {1u, 7u}, {1u, 8u}, {1u, 9u}, {1u, 10u}, {1u, 11u},
    {1u, 12u}, {1u, 13u}, {1u, 14u}, {1u, 15u}
};
#define CAND_N ((uint32_t)(sizeof(g_cand) / sizeof(g_cand[0])))

static uint8_t  g_grb[LED_COUNT * 3u];
static uint32_t g_sel = 0u;
static uint32_t g_mask_a = 0u, g_mask_b = 0u;

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

static void dwt_init(void)
{
    DEMCR |= (1u << 24);
    DWT_CYCCNT = 0u;
    DWT_CTRL |= 1u;
}

/* ---------------- Flash 保存 ---------------- */
static void flash_wait(void)
{
    while (FLASH_SR & (1u << 16)) { }          /* BSY */
}

static uint32_t flash_read_sel(void)
{
    uint32_t v = *(volatile uint32_t *)FB_ADDR;
    if ((v & 0xFFFF0000u) != (FB_MAGIC & 0xFFFF0000u)) { return 0u; }
    v &= 0xFFu;
    return (v < CAND_N) ? v : 0u;
}

static int flash_save_sel(uint32_t sel)
{
    uint32_t word;

    FLASH_KEYR = 0x45670123u;
    FLASH_KEYR = 0xCDEF89ABu;

    flash_wait();
    FLASH_CR = (1u << 1) | (7u << 3) | (2u << 8);   /* SER, SNB=7, PSIZE=x32 */
    flash_wait();
    FLASH_CR |= (1u << 16);                          /* STRT */
    flash_wait();
    if (!(FLASH_SR & 1u)) { FLASH_CR |= (1u << 31); return 0; }
    FLASH_SR = 1u;                                   /* 清 EOP */

    word = (FB_MAGIC & 0xFFFF0000u) | (sel & 0xFFu);   /* 0x575300SS */
    FLASH_CR = (1u << 0) | (2u << 8);                /* PG, PSIZE=x32 */
    flash_wait();
    *(volatile uint32_t *)FB_ADDR = word;
    flash_wait();
    FLASH_CR |= (1u << 31);                          /* LOCK */
    return 1;
}

/* ---------------- 引脚配置 ---------------- */
static void apply_sel(void)
{
    uint32_t i;
    uint8_t port = g_cand[g_sel].port;
    uint8_t pin = g_cand[g_sel].pin;

    /* 先全部复位为输入、关上拉，避免残留驱动 */
    for (i = 0u; i < 16u; i++) {
        if (i == 9u || i == 10u) { continue; }
        GPIOA_MODER &= ~(3u << (i * 2u));
        GPIOB_MODER &= ~(3u << (i * 2u));
    }
    GPIOA_PUPDR = 0u;
    GPIOB_PUPDR = 0u;
    g_mask_a = 0u;
    g_mask_b = 0u;

    if (pin == 0xFFu) {                              /* 广播 */
        for (i = 1u; i <= 8u; i++) {
            GPIOA_MODER = (GPIOA_MODER & ~(3u << (i * 2u))) | (1u << (i * 2u));
            GPIOA_OSPEEDR |= (3u << (i * 2u));
            g_mask_a |= (1u << i);
        }
        GPIOA_MODER = (GPIOA_MODER & ~(3u << 30)) | (1u << 30);
        GPIOA_OSPEEDR |= (3u << 30);
        g_mask_a |= (1u << 15);
        GPIOB_MODER = 0x55555555u;
        GPIOB_OSPEEDR = 0xFFFFFFFFu;
        g_mask_b = 0xFFFFu;
    } else if (port == 0u) {
        GPIOA_MODER = (GPIOA_MODER & ~(3u << (pin * 2u))) | (1u << (pin * 2u));
        GPIOA_OSPEEDR |= (3u << (pin * 2u));
        g_mask_a = (1u << pin);
    } else {
        GPIOB_MODER = (GPIOB_MODER & ~(3u << (pin * 2u))) | (1u << (pin * 2u));
        GPIOB_OSPEEDR |= (3u << (pin * 2u));
        g_mask_b = (1u << pin);
    }

    /* 按键脚保持输入+上拉 */
    GPIOA_MODER &= ~(3u << 0);
    GPIOA_PUPDR = (GPIOA_PUPDR & ~(3u << 0)) | (1u << 0);
}

/* ---------------- WS2812 ---------------- */
static uint32_t ns2cyc(uint32_t ns) { return ((SYS_HZ / 1000u) * ns + 500000u) / 1000000u; }

static inline void wait_until(uint32_t d)
{
    while ((int32_t)(DWT_CYCCNT - d) < 0) { }
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

static void wheel(uint8_t p, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (p < 85u)       { *r = (uint8_t)(255u - p * 3u); *g = (uint8_t)(p * 3u);             *b = 0; }
    else if (p < 170u) { p = (uint8_t)(p - 85u);        *r = 0;                             *g = (uint8_t)(255u - p * 3u); *b = (uint8_t)(p * 3u); }
    else               { p = (uint8_t)(p - 170u);       *r = (uint8_t)(p * 3u);             *g = 0;                        *b = (uint8_t)(255u - p * 3u); }
}

static void show_sel(void)
{
    uart_puts("SEL ");
    if (g_cand[g_sel].pin == 0xFFu) {
        uart_puts("BROADCAST\r\n");
    } else {
        uart_putc(g_cand[g_sel].port ? 'B' : 'A');
        uart_dec(g_cand[g_sel].pin);
        uart_puts("\r\n");
    }
}

/* 切脚后点亮全白，方便肉眼确认 */
static void preview(void)
{
    fill(70u, 70u, 70u);
    ws_send();
    delay_ms(900u);
    fill(0u, 0u, 0u);
    ws_send();
    delay_ms(150u);
}

int main(void)
{
    uint32_t tick = 0u, base = 0u, cnt = 0u, long_done = 0u, key_prev = 1u;

    clock_init();
    dwt_init();
    uart_init();

    g_sel = flash_read_sel();
    apply_sel();
    delay_ms(30u);

    uart_puts("\r\nWS2812 PIN SELECT\r\n");
    show_sel();
    if (g_sel == 0u) {
        uart_puts("(no saved config: BROADCAST)\r\n");
    } else {
        uart_puts("(loaded from flash)\r\n");
    }
    uart_puts("short press = next pin, long press (2s) = save\r\n");
    preview();

    for (;;) {
        uint32_t key = GPIOA_IDR & 1u;

        if (key == 0u) {
            if (cnt < 200u) { cnt++; }
            if (cnt == 80u && !long_done) {          /* 2 s */
                long_done = 1u;
                if (flash_save_sel(g_sel)) {
                    uart_puts("SAVED\r\n");
                } else {
                    uart_puts("SAVE FAILED\r\n");
                }
            }
        } else {
            if (key_prev == 0u && cnt >= 2u && cnt < 80u && !long_done) {
                g_sel = (g_sel + 1u) % CAND_N;       /* 短按: 下一个候选 */
                apply_sel();
                show_sel();
                preview();
            }
            cnt = 0u;
            long_done = 0u;
        }
        key_prev = key;

        /* 正常显示: 彩虹流动 */
        {
            uint32_t i;
            for (i = 0u; i < LED_COUNT; i++) {
                uint8_t r, g, b;
                wheel((uint8_t)((i * 4u + base) & 0xFFu), &r, &g, &b);
                g_grb[i * 3u + 0u] = g;
                g_grb[i * 3u + 1u] = r;
                g_grb[i * 3u + 2u] = b;
            }
        }
        ws_send();
        if ((tick % 40u) == 0u) { uart_puts("RUN\r\n"); }
        tick++;
        base = (base + 4u) & 0xFFu;
        delay_ms(25u);
    }
}
