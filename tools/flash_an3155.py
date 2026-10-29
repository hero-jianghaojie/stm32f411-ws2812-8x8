"""STM32F411 串口 ISP 烧写器（AN3155 协议，自己实现，不依赖 FlyMcu）

为什么自己写：FlyMcu 按 DTR/RTS 自动复位进 Bootloader 时要额外的两根控制线 +
三极管电路。只有 USB-TTL 三根线（TX/RX/GND）时，那套自动时序无效--
日志里照样打印 "DTR高/RTS高"，但芯片根本没被复位，也没进 Bootloader。
本脚本只用手动 BOOT0 + 复位，协议层直接对话。

用法：
    python flash_an3155.py --port COM6 --hex out/f411_uart_test.hex
    python flash_an3155.py --port COM6 --probe          # 只看 bootloader 在不在
    python flash_an3155.py --port COM6 --hex ... --go   # 烧完直接跳转运行
"""
import argparse
import sys
import time

import serial

ACK = 0x79
NACK = 0x1F


def xor_all(bs):
    x = 0
    for b in bs:
        x ^= b
    return x


class Boot:
    def __init__(self, port, baud=115200):
        self.s = serial.Serial(port, baud, bytesize=8, parity=serial.PARITY_EVEN,
                               stopbits=1, timeout=0.6)

    def close(self):
        self.s.close()

    def ack(self, timeout=1.0):
        t = time.time()
        while time.time() - t < timeout:
            b = self.s.read(1)
            if b:
                return b[0]
        return None

    def sync(self, tries=6):
        for _ in range(tries):
            self.s.reset_input_buffer()
            self.s.write(b"\x7f")
            v = self.ack(0.5)
            if v is not None:
                return v
        return None

    def cmd(self, code):
        self.s.write(bytes([code, code ^ 0xFF]))
        return self.ack()

    def get_id(self):
        self.s.reset_input_buffer()
        self.s.write(bytes([0x02, 0xFD]))
        if self.ack() != ACK:
            return None
        nb = self.s.read(1)
        if not nb:
            return None
        n = nb[0]
        body = self.s.read(n + 1)          # PID 两字节
        self.ack(1.0)                      # 吃掉尾随 ACK, 否则后续命令错位
        pid = (body[0] << 8) | body[1] if len(body) >= 2 else None
        return pid

    def get_version(self):
        self.s.reset_input_buffer()
        self.s.write(bytes([0x00, 0xFF]))
        if self.ack() != ACK:
            return None
        nb = self.s.read(1)
        if not nb:
            return None
        n = nb[0]
        body = self.s.read(n + 1)          # 版本字节 + N 个命令字节
        self.ack()
        return body[0] / 10.0 if body else None

    def erase(self):
        """全片擦除: 先试扩展擦除 0x44, 再试标准擦除 0x43; 每种试两轮, 轮间重新同步。"""
        for attempt in range(1, 3):
            time.sleep(0.2)
            self.sync()
            self.s.reset_input_buffer()
            self.s.write(bytes([0x44, 0xBB]))
            if self.ack() == ACK:
                self.s.write(bytes([0xFF, 0xFF, 0x00]))
                if self.ack(30) == ACK:
                    return "extended-mass"
            time.sleep(0.2)
            self.sync()
            self.s.reset_input_buffer()
            self.s.write(bytes([0x43, 0xBC]))
            if self.ack() == ACK:
                self.s.write(bytes([0xFF, 0x00]))
                if self.ack(30) == ACK:
                    return "standard-mass"
        return None

    def write(self, addr, data):
        assert 0 < len(data) <= 256
        if self.cmd(0x31) != ACK:
            return "cmd"
        a = addr.to_bytes(4, "big")
        self.s.write(a + bytes([xor_all(a)]))
        r = self.ack()
        if r != ACK:
            return "addr" if r is None else "addr(0x%02X)" % r
        blk = bytes([len(data) - 1]) + data
        self.s.write(blk + bytes([xor_all(blk)]))
        r = self.ack(5.0)
        if r != ACK:
            return "data" if r is None else "data(0x%02X)" % r
        return None

    def read(self, addr, n):
        if self.cmd(0x11) != ACK:
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
        self.ack(1.0)          # 吃掉读数据后的 ACK, 否则下一条命令错位
        return data

    def go(self, addr):
        if self.cmd(0x21) != ACK:
            return False
        a = addr.to_bytes(4, "big")
        self.s.write(a + bytes([xor_all(a)]))
        return self.ack() == ACK


def parse_hex(path):
    mem = {}
    base = 0
    for line in open(path, encoding="ascii"):
        line = line.strip()
        if not line.startswith(":"):
            continue
        raw = bytes.fromhex(line[1:])
        n, addr, t = raw[0], (raw[1] << 8) | raw[2], raw[3]
        d = raw[4:4 + n]
        if t == 0:
            for i, b in enumerate(d):
                mem[base + addr + i] = b
        elif t == 4:
            base = ((d[0] << 8) | d[1]) << 16
        elif t == 1:
            break
    return mem


def blocks(mem, size=256):
    if not mem:
        return []
    lo = min(mem) & ~(size - 1)
    hi = max(mem)
    out = []
    a = lo
    while a <= hi:
        chunk = bytes(mem.get(a + i, 0xFF) for i in range(size))
        out.append((a, chunk))
        a += size
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM6")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--hex")
    ap.add_argument("--go", action="store_true")
    ap.add_argument("--probe", action="store_true")
    ap.add_argument("--no-erase", action="store_true")
    ap.add_argument("--erase-only", action="store_true")
    ap.add_argument("--no-verify", action="store_true")
    ap.add_argument("--chunk", type=int, default=64, help="每次写入字节数, 默认 64")
    ap.add_argument("--retry", type=int, default=3, help="每块重试次数")
    ap.add_argument("--loopback", action="store_true")
    ap.add_argument("--wait", type=float, default=0.0,
                    help="在这段时间内不停重试同步，方便你按 BOOT0+复位")
    args = ap.parse_args()

    if args.loopback:
        s = serial.Serial(args.port, 115200, timeout=0.5)
        s.reset_input_buffer()
        payload = b"LOOPBACK-TEST-123"
        s.write(payload)
        got = s.read(len(payload))
        s.close()
        ok = got == payload
        print("回环测试:", "PASS（USB-TTL 收发正常）" if ok else "FAIL（收到 %r）" % got)
        print("做法: 把 USB-TTL 的 TX 和 RX 短接在一起（不要接板子）再跑这一项。")
        return 0 if ok else 6

    b = Boot(args.port, args.baud)
    try:
        v = None
        if args.wait > 0:
            print("监听 %.0f 秒：现在按住 BOOT0，点一下 NRST，再松开 BOOT0 ..." % args.wait)
            t0 = time.time()
            while time.time() - t0 < args.wait:
                v = b.sync(tries=1)
                if v == ACK:
                    print("收到 ACK，用时 %.1f 秒" % (time.time() - t0))
                    break
        else:
            v = b.sync()
        if v != ACK:
            print("同步失败: 没收到 0x79（收到了 %s）" % ("无" if v is None else "0x%02X" % v))
            print("-> 确认 BOOT0 已接 3.3V、按过复位；TX<->PA10、RX<->PA9 交叉；共地")
            return 2
        print("synced: 0x7F -> 0x79")

        ver = b.get_version()
        pid = b.get_id()
        print("bootloader 版本:", ver, " PID:", "0x%03X" % pid if pid else "?")
        if args.probe or not args.hex:
            print("bootloader 在线，可以烧录。")
            return 0

        mem = parse_hex(args.hex)
        blks = blocks(mem, args.chunk)
        total = sum(len(d) for _, d in blks)
        print("hex: %d 字节, %d 块, 地址 0x%08X..0x%08X"
              % (total, len(blks), blks[0][0], blks[-1][0] + len(blks[-1][1]) - 1))

        if not args.no_erase:
            mode = b.erase()
            print("擦除:", mode or "失败")
            if not mode:
                return 3
            if args.erase_only:
                print("只擦除, 结束")
                return 0
            time.sleep(0.5)
            # 擦除后尝试重新同步；失败只警告不中断（有些 bootloader 擦完不回 0x7F）
            if b.sync() != ACK:
                print("提示: 擦除后 0x7F 未应答, 继续尝试写入")

        for i, (addr, data) in enumerate(blks, 1):
            err = "?"
            for attempt in range(1, args.retry + 1):
                err = b.write(addr, data)
                if err is None:
                    break
                print("  块 %d 第 %d 次失败(%s), 重试" % (i, attempt, err))
                time.sleep(0.05)
            if err is not None:
                print("写入失败 @0x%08X (块 %d, %s)" % (addr, i, err))
                print("提示: 可改小分块 --chunk 32, 或降速 --baud 57600")
                return 4
            sys.stdout.write("\r写入 %d/%d 块" % (i, len(blks)))
            sys.stdout.flush()
        print()

        if not args.no_verify:
            bad = 0
            for addr, data in blks:
                got = b.read(addr, len(data))
                time.sleep(0.005)
                if got != data:
                    time.sleep(0.02)
                    again = b.read(addr, len(data))
                    if again == data:
                        got = again          # 首次读抖动, 复读一致即通过
                if got != data:
                    bad += 1
                    print("校验不符 @0x%08X" % addr)
                    if bad <= 2:
                        exp = data[:12].hex(" ")
                        g = "None" if got is None else got[:12].hex(" ")
                        print("   期望: %s" % exp)
                        print("   读回: %s" % g)
            if bad:
                print("校验失败 %d 块" % bad)
                return 5
            print("校验通过: %d 块全部一致" % len(blks))

        if args.go:
            print("跳转到 0x08000000:", "OK" if b.go(0x08000000) else "FAIL")

        print("完成。把 BOOT0 接回 GND, 按复位, 程序开始运行。")
        return 0
    finally:
        b.close()


if __name__ == "__main__":
    sys.exit(main())
