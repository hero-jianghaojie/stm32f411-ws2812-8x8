/* STM32F411 + WS2812B 8x8：五模式灯效 + 数据脚运行时选择（合并版）
 *
 * 按键 KEY = PA0（内部上拉，按下为 0）
 *   短按        : 切换画面 0->1->2->3->4->5->0
 *                 0 彩虹  1 呼吸白  2 彩色流水  3 单色轮换  4 关闭  5 引脚选择
 *   在"引脚选择"画面(5)里：
 *       短按    : 换到下一个候选引脚，并全白闪一下让你看清接的是哪根
 *       长按 2s : 把当前引脚写入 Flash 并回到画面 0
 *
 * 数据脚候选：0=BROADCAST(全部脚) 1..8=PA1..PA8 9=PA15 10..25=PB0..PB15
 * 保存位置：Flash 0x08060000（sector 7），配置字 0x57530500 + N
 *          可用 set_pin.bat 或 flash_an3155.py --pin-sel N 直接改，开机自动读回
 *
 * 串口 USART1 PA9/PA10 115200：打印 MODE / PIN / RUN 状态
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
#define FB_ADDR         0x08060000u
#define FB_MAGIC        0x57530000u

#define SCR_PIN         5u
#define SCR_NUM         6u

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
static uint8_t  g_scr = 0u;
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

/* ---------------- Flash 配置 ---------------- */
static void flash_wait(void) { while (FLASH_SR & (1u << 16)) { } }

static uint32_t flash_read_sel(void)
{
    uint32_t v = *(volatile uint32_t *)FB_ADDR;
    if ((v & 0xFFFFFF00u) != FB_MAGIC) { return 0u; }
    v &= 0xFFu;
    return (v < CAND_N) ? v : 0u;
}

static int flash_save_sel(uint32_t sel)
{
    uint32_t word;

    FLASH_KEYR = 0x45670123u;
    FLASH_KEYR = 0xCDEF89ABu;

    flash_wait();
    FLASH_CR = (1u << 1) | (7u << 3) | (2u << 8);
    flash_wait();
    FLASH_CR |= (1u << 16);
    flash_wait();
    if (!(FLASH_SR & 1u)) { FLASH_CR |= (1u << 31); FLASH_SR = 1u; return 0; }
    FLASH_SR = 1u;

    word = FB_MAGIC | (sel & 0xFFu);
    FLASH_CR = (1u << 0) | (2u << 8);
    flash_wait();
    *(volatile uint32_t *)FB_ADDR = word;
    flash_wait();
    FLASH_CR |= (1u << 31);
    return 1;
}

/* ---------------- 引脚 ---------------- */
static void apply_sel(void)
{
    uint32_t i;
    uint8_t port = g_cand[g_sel].port;
    uint8_t pin = g_cand[g_sel].pin;

    for (i = 0u; i < 16u; i++) {
        if (i == 9u || i == 10u) { continue; }
        GPIOA_MODER &= ~(3u << (i * 2u));
        GPIOB_MODER &= ~(3u << (i * 2u));
    }
    GPIOA_PUPDR = 0u;
    GPIOB_PUPDR = 0u;
    g_mask_a = 0u;
    g_mask_b = 0u;

    if (pin == 0xFFu) {
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

static void set_px(uint32_t i, uint8_t r, uint8_t g, uint8_t b)
{
    if (i >= LED_COUNT) { return; }
    g_grb[i * 3u + 0u] = g;
    g_grb[i * 3u + 1u] = r;
    g_grb[i * 3u + 2u] = b;
}

static void wheel(uint8_t p, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (p < 85u)       { *r = (uint8_t)(255u - p * 3u); *g = (uint8_t)(p * 3u);             *b = 0; }
    else if (p < 170u) { p = (uint8_t)(p - 85u);        *r = 0;                             *g = (uint8_t)(255u - p * 3u); *b = (uint8_t)(p * 3u); }
    else               { p = (uint8_t)(p - 170u);       *r = (uint8_t)(p * 3u);             *g = 0;                        *b = (uint8_t)(255u - p * 3u); }
}

/* ---------------- 画面渲染 ---------------- */
static void render(uint32_t tick)
{
    uint32_t i;

    switch (g_scr) {
    case 0u: {
        for (i = 0u; i < LED_COUNT; i++) {
            uint8_t r, g, b;
            wheel((uint8_t)((i * 4u + tick * 4u) & 0xFFu), &r, &g, &b);
            set_px(i, (uint8_t)(r >> 1), (uint8_t)(g >> 1), (uint8_t)(b >> 1));
        }
        break;
    }
    case 1u: {
        uint32_t ph = tick % 80u;
        uint32_t lv = (ph < 40u) ? ph * 2u : (80u - ph) * 2u;
        fill((uint8_t)lv, (uint8_t)lv, (uint8_t)lv);
        break;
    }
    case 2u: {
        uint32_t pos = (tick / 3u) % LED_COUNT;
        uint32_t k;
        uint8_t hr, hg, hb;
        wheel((uint8_t)((tick * 4u) & 0xFFu), &hr, &hg, &hb);
        fill(0u, 0u, 0u);
        for (k = 0u; k < 4u; k++) {
            uint32_t idx = (pos + LED_COUNT - k) % LED_COUNT;
            uint32_t sc = (4u - k) * 25u;
            set_px(idx, (uint8_t)((uint32_t)hr * sc / 100u),
                       (uint8_t)((uint32_t)hg * sc / 100u),
                       (uint8_t)((uint32_t)hb * sc / 100u));
        }
        break;
    }
    case 3u: {
        static const uint8_t col[6][3] = {
            {80, 0, 0}, {0, 80, 0}, {0, 0, 80},
            {80, 80, 0}, {0, 80, 80}, {80, 0, 80}
        };
        uint32_t c = (tick / 40u) % 6u;
        fill(col[c][0], col[c][1], col[c][2]);
        break;
    }
    case 4u:
        fill(0u, 0u, 0u);
        break;
    default:
        fill(40u, 40u, 40u);
        break;
    }
}

static void show_pin(void)
{
    uart_puts("PIN ");
    if (g_cand[g_sel].pin == 0xFFu) {
        uart_puts("BROADCAST\r\n");
    } else {
        uart_putc(g_cand[g_sel].port ? 'B' : 'A');
        uart_dec(g_cand[g_sel].pin);
        uart_puts("\r\n");
    }
}

static void show_scr(void)
{
    uart_puts("MODE ");
    uart_dec(g_scr);
    uart_puts("\r\n");
    if (g_scr == SCR_PIN) {
        show_pin();
        uart_puts("short=next pin, long(2s)=save\r\n");
    }
}

int main(void)
{
    uint32_t tick = 0u, cnt = 0u, long_done = 0u, key_prev = 1u, hb = 0u;

    clock_init();
    dwt_init();
    uart_init();

    g_sel = flash_read_sel();
    apply_sel();
    delay_ms(30u);

    uart_puts("\r\nWS2812 SEL  KEY=PA0\r\n");
    show_scr();

    for (;;) {
        uint32_t key = GPIOA_IDR & 1u;

        if (key == 0u) {
            if (cnt < 200u) { cnt++; }
            if (cnt == 80u && !long_done && g_scr == SCR_PIN) {
                long_done = 1u;
                uart_puts(flash_save_sel(g_sel) ? "SAVED\r\n" : "SAVE FAILED\r\n");
                g_scr = 0u;
                show_scr();
            }
        } else {
            if (key_prev == 0u && cnt >= 2u && cnt < 80u && !long_done) {
                if (g_scr == SCR_PIN) {
                    g_sel = (g_sel + 1u) % CAND_N;
                    apply_sel();
                    show_pin();
                    fill(70u, 70u, 70u);
                    ws_send();
                    delay_ms(300u);
                } else {
                    g_scr = (uint8_t)((g_scr + 1u) % SCR_NUM);
                    if (g_scr == SCR_PIN) { apply_sel(); }
                    show_scr();
                }
            }
            cnt = 0u;
            long_done = 0u;
        }
        key_prev = key;

        render(tick);
        ws_send();

        if (++hb >= 40u) { hb = 0u; uart_puts("RUN\r\n"); }
        tick++;
        delay_ms(25u);
    }
}
