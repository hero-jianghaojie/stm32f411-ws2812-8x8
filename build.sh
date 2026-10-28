#!/usr/bin/env bash
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
GCC="$ROOT/../stm32_ws2812/tools/gcc/bin/arm-none-eabi-gcc.exe"
OBJCOPY="$ROOT/../stm32_ws2812/tools/gcc/bin/arm-none-eabi-objcopy.exe"
SIZE="$ROOT/../stm32_ws2812/tools/gcc/bin/arm-none-eabi-size.exe"
mkdir -p "$ROOT/build" "$ROOT/out"

"$GCC" -mcpu=cortex-m4 -mthumb -O2 -ffreestanding -fno-builtin -std=c99 -Wall \
  -T "$ROOT/f411.ld" -nostdlib -Wl,--gc-sections \
  "$ROOT/src/main.c" "$ROOT/src/startup.c" \
  -o "$ROOT/build/f411_uart_test.elf" -lgcc

"$OBJCOPY" -O ihex "$ROOT/build/f411_uart_test.elf" "$ROOT/out/f411_uart_test.hex"
"$OBJCOPY" -O binary "$ROOT/build/f411_uart_test.elf" "$ROOT/out/f411_uart_test.bin"
"$SIZE" "$ROOT/build/f411_uart_test.elf"
