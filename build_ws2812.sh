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
build_one ws2812_sel.c     ws2812_sel

# 彩色 SPI TFT（-DTFT_CTRL: 1=ST7735, 2=ST7789, 3=ILI9341）
build_one tft_test.c       tft_st7735  -DTFT_CTRL=1
build_one tft_test.c       tft_st7789  -DTFT_CTRL=2
build_one tft_test.c       tft_ili9341 -DTFT_CTRL=3
# ST7735 面板偏移/颜色反相的备用版（1.8 寸蓝板常见 offset 2/1）
build_one tft_test.c       tft_st7735o -DTFT_CTRL=1 -DTFT_XOFF=2 -DTFT_YOFF=1
build_one tft_test.c       tft_st7735i -DTFT_CTRL=1 -DTFT_XOFF=2 -DTFT_YOFF=1 -DTFT_INVERT=1
# 1.44 寸 128x128 版（ST7735，默认偏移 2/1）
build_one tft_test.c       tft_144     -DTFT_CTRL=4
build_one tft_test.c       tft_144o    -DTFT_CTRL=4 -DTFT_XOFF=0 -DTFT_YOFF=0
build_one tft_test.c       tft_144i    -DTFT_CTRL=4 -DTFT_INVERT=1

# 壁纸播放（需要先跑 tools/gen_tft_video.py 生成 src/anim_tft.h）
build_one tft_wallpaper.c  tft_wall

# 单脚版本（DIN 接 PA7 / PA6 / PA1 / PA0）

# 工具类固件
build_one btn_scan.c       btn_scan
build_one aht21_test.c     aht21_test
build_one aht21_oled.c     aht21_oled
build_one ws_pin_scan.c    ws_pin_scan

echo "-- done --"
ls -l "$ROOT/out"
