/* STM32F411 最小启动: 向量表 + 复位 */
#include <stdint.h>

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
extern int main(void);

void Reset_Handler(void);
void Default_Handler(void);

typedef void (*vec_t)(void);

void Default_Handler(void) { for (;;) { } }

__attribute__((section(".isr_vector"), used))
vec_t const g_vectors[100] = {
    (vec_t)&_estack,
    Reset_Handler,
    [2 ... 99] = Default_Handler
};

void Reset_Handler(void)
{
    uint32_t *src = &_sidata;
    uint32_t *dst = &_sdata;
    while (dst < &_edata) { *dst++ = *src++; }
    dst = &_sbss;
    while (dst < &_ebss) { *dst++ = 0u; }
    main();
    for (;;) { }
}
