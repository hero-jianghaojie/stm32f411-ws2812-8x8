"""看着串口输出: 烧写完成后用来确认程序真的在跑。

用法:
    python watch_serial.py --port COM6 --seconds 8
"""
import argparse
import sys
import time

import serial


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM6")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--seconds", type=float, default=8.0)
    args = ap.parse_args()

    ser = serial.Serial(args.port, args.baud, timeout=0.3)
    print("监听 %s @ %d 8N1, %.0f 秒 ..." % (args.port, args.baud, args.seconds))
    t0 = time.time()
    buf = b""
    while time.time() - t0 < args.seconds:
        chunk = ser.read(64)
        if chunk:
            buf += chunk
            sys.stdout.write(chunk.decode("utf-8", "replace"))
            sys.stdout.flush()
    ser.close()
    print("\n--- 收到 %d 字节 ---" % len(buf))
    if (b"F411-RUN-OK" in buf) or (b"F411-WS2812-OK" in buf):
        print("判定: PASS - 固件已在 F411 上运行, 串口 ISP 烧录链路完全打通")
        return 0
    print("判定: 没看到 F411-RUN-OK, 检查 BOOT0 是否已回到 0 并复位")
    return 1


if __name__ == "__main__":
    sys.exit(main())
