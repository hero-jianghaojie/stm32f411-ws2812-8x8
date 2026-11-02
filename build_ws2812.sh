#!/usr/bin/env bash
# 编译 WS2812 固件。工具链优先用环境变量，其次用本机自带的 ARM GCC，最后用 PATH 里的。
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$ROOT/build" "$ROOT/out"

BUNDLED="$ROOT/../stm32_ws2812/tools/gcc/bin"
if [ -x "$BUNDLED/arm-none-eabi-gcc.exe" ]; then
  GCC="${GCC:-$BUNDLED/arm-none-eabi-gcc.exe}"
  OBJCOPY="${OBJCOPY:-$BUNDLED/arm-none-eabi-objcopy.exe}"
  SIZE="${SIZE:-$BUNDLED/arm-none-eabi-size.exe}"
else
  GCC="${GCC:-arm-none-eabi-gcc}"
  OBJCOPY="${OBJCOPY:-arm-none-eabi-objcopy}"
  SIZE="${SIZE:-arm-none-eabi-size}"
fi

CFLAGS="-mcpu=cortex-m4 -mthumb -O2 -ffreestanding -fno-builtin -std=c99 -Wall"
LDFLAGS="-T $ROOT/f411.ld -nostdlib -Wl,--gc-sections"

build_one() {
  local src="$1" name="$2"
  shift 2
  "$GCC" $CFLAGS $LDFLAGS "$@" "$ROOT/src/$src" "$ROOT/src/startup.c" \
    -o "$ROOT/build/$name.elf" -lgcc
  "$OBJCOPY" -O ihex "$ROOT/build/$name.elf" "$ROOT/out/$name.hex"
  "$OBJCOPY" -O binary "$ROOT/build/$name.elf" "$ROOT/out/$name.bin"
  echo "== $name =="
  "$SIZE" "$ROOT/build/$name.elf"
}

# 五模式固件（全引脚广播，PA0 按键）——默认烧这个
build_one ws2812_modes.c   ws2812_modes
build_one ws2812_sel.c     ws2812_sel

# 单脚版本（DIN 接 PA7 / PA6 / PA1 / PA0）
build_one ws2812_f411.c    ws2812_PA7 -DWS_PIN=7
build_one ws2812_f411.c    ws2812_PA6 -DWS_PIN=6
build_one ws2812_f411.c    ws2812_PA1 -DWS_PIN=1
build_one ws2812_f411.c    ws2812_PA0 -DWS_PIN=0

# 工具类固件
build_one btn_scan.c       btn_scan
build_one ws_pin_scan.c    ws_pin_scan
build_one ws2812_allpins.c ws2812_allpins
build_one ws2812_bright.c  ws2812_bright

echo "-- done --"
ls -l "$ROOT/out"
