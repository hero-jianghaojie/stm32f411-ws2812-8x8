"""只读回 0x08000000 起 1KB, 判断 Flash 里到底是什么。

用法: python read_flash.py --port COM6 --wait 30
输出: 每 64 字节一行的十六进制 + 保存 readback.bin
"""
import argparse
import sys
import time

import serial

ACK = 0x79


def xor_all(bs):
    x = 0
    for b in bs:
        x ^= b
    return x


class Boot:
    def __init__(self, port, baud):
        self.s = serial.Serial(port, baud, bytesize=8, parity=serial.PARITY_EVEN,
                               stopbits=1, timeout=0.6)

    def ack(self, t=1.0):
        end = time.time() + t
        while time.time() < end:
            b = self.s.read(1)
            if b:
                return b[0]
        return None

    def sync(self):
        for _ in range(6):
            self.s.reset_input_buffer()
            self.s.write(b"\x7f")
            if self.ack(0.5) == ACK:
                return True
        return False

    def read(self, addr, n):
        self.s.reset_input_buffer()
        self.s.write(bytes([0x11, 0xEE]))
        if self.ack() != ACK:
            return None
        a = addr.to_bytes(4, "big")
        self.s.write(a + bytes([xor_all(a)]))
        if self.ack() != ACK:
            return None
        blk = bytes([n - 1, (n - 1) ^ 0xFF])
        self.s.write(blk)
        if self.ack() != ACK:
            return None
        data = self.s.read(n)
        self.ack(1.0)
        return data


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM6")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--wait", type=float, default=30.0)
    ap.add_argument("--size", type=int, default=1024)
    args = ap.parse_args()

    b = Boot(args.port, args.baud)
    print("等待进入 bootloader (%.0f 秒内按 BOOT0+NRST) ..." % args.wait)
    t0 = time.time()
    ok = False
    while time.time() - t0 < args.wait:
        if b.sync():
            ok = True
            break
    if not ok:
        print("没有同步成功")
        return 2
    print("已同步, 开始读回 0x08000000 起 %d 字节" % args.size)

    out = bytearray()
    addr = 0x08000000
    while len(out) < args.size:
        n = min(256, args.size - len(out))
        d = b.read(addr, n)
        if d is None or len(d) != n:
            print("读失败 @0x%08X (得到 %s 字节)" % (addr, "None" if d is None else len(d)))
            break
        out += d
        addr += n
    b.s.close()

    open("readback.bin", "wb").write(bytes(out))
    print("已保存 readback.bin (%d 字节)" % len(out))

    nz = sum(1 for x in out if x != 0xFF)
    print("非 0xFF 字节: %d / %d" % (nz, len(out)))
    for off in range(0, min(len(out), 320), 64):
        chunk = out[off:off + 64]
        print("%08X  %s" % (0x08000000 + off, chunk.hex(" ")))
    return 0


if __name__ == "__main__":
    sys.exit(main())
