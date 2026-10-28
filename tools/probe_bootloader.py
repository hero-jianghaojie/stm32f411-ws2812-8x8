"""STM32 ROM bootloader 握手探测 (AN3155 USART 协议)

用法:
    python probe_bootloader.py --list
    python probe_bootloader.py --port COM7
    python probe_bootloader.py            # 自动挑一个非蓝牙串口

判定: 收到 0x79 = bootloader 在运行, 且串口链路(电平/接线/波特率)全部正确。
"""
import argparse
import sys
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.exit("缺 pyserial: pip install pyserial")

ACK = 0x79
NACK = 0x1F
SYNC = 0x7F


def ports():
    return list(list_ports.comports())


def show_ports():
    ps = ports()
    if not ps:
        print("没有发现任何串口。检查 USB-TTL 是否插好、驱动是否安装。")
        return []
    print("发现 %d 个串口:" % len(ps))
    for p in ps:
        print("  %-6s %s" % (p.device, p.description))
    return ps


def pick_port(ps):
    for p in ps:
        d = (p.description or "").lower()
        if any(k in d for k in ("ch340", "ch341", "cp210", "ft232", "ftdi", "usb-serial", "usb serial")):
            return p.device
    for p in ps:
        if "bluetooth" not in (p.description or "").lower() and "蓝牙" not in (p.description or ""):
            return p.device
    return None


def sync(ser):
    ser.reset_input_buffer()
    ser.write(bytes([SYNC]))
    t0 = time.time()
    r = ser.read(1)
    return (r[0] if r else None), time.time() - t0


def cmd(ser, code, data=b""):
    ser.write(bytes([code]) + bytes([code ^ 0xFF]))
    a = ser.read(1)
    if not a or a[0] != ACK:
        return None
    if data:
        ser.write(bytes(data))
        a = ser.read(1)
        if not a or a[0] != ACK:
            return None
    return True


def get_info(ser):
    info = {}
    ser.write(bytes([0x00, 0xFF]))
    a = ser.read(1)
    if not a or a[0] != ACK:
        return info
    n = ser.read(1)
    if not n:
        return info
    n = n[0]
    body = ser.read(n)
    ser.read(1)
    if len(body) >= 2:
        info["version"] = "%.1f" % (body[0] / 10.0)
        info["commands"] = body[1:]
    return info


def get_id(ser):
    ser.write(bytes([0x02, 0xFD]))
    a = ser.read(1)
    if not a or a[0] != ACK:
        return None
    n = ser.read(1)
    if not n:
        return None
    n = n[0]
    body = ser.read(n + 1)
    if len(body) < 3:
        return None
    pid = (body[0] << 8) | body[1]
    return pid


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--list", action="store_true")
    args = ap.parse_args()

    ps = show_ports()
    if args.list:
        return 0

    port = args.port or pick_port(ps)
    if not port:
        print("没有可用的 USB 串口，插上 USB-TTL 再试。")
        return 2

    print("\n探测 %s @ %d 8E1 ..." % (port, args.baud))
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = args.baud
    ser.bytesize = serial.EIGHTBITS
    ser.parity = serial.PARITY_EVEN
    ser.stopbits = serial.STOPBITS_ONE
    ser.timeout = 0.6
    ser.open()
    time.sleep(0.1)

    got = None
    for attempt in range(5):
        v, dt = sync(ser)
        if v is not None:
            got = v
            print("  第 %d 次同步: 0x%02X  (%.0f ms)" % (attempt + 1, v, dt * 1000))
            break
        time.sleep(0.05)

    if got != ACK:
        print("\n结果: 没有收到 ACK" + ("" if got is None else " (收到 0x%02X)" % got))
        print("排查顺序:")
        print("  1. BOOT0 是否接 3.3V, 然后按过复位 (BOOT0=0 时跑的是用户程序, 不会应答)")
        print("  2. TX/RX 是否交叉: USB-TTL 的 TX 接 PA10, RX 接 PA9")
        print("  3. 是否共地; USB-TTL 是否是 3.3V 电平 (5V 电平可能收不到)")
        print("  4. 换 9600 / 57600 再试 (个别 bootloader 版本)")
        ser.close()
        return 1

    info = get_info(ser)
    pid = get_id(ser)
    ser.close()

    print("\n结果: bootloader 在线 OK")
    if info.get("version"):
        print("  bootloader 版本: %s" % info["version"])
        print("  支持的命令数  : %d" % len(info.get("commands", [])))
    if pid is not None:
        print("  PID           : 0x%03X" % pid)
        if pid in (0x413,):
            print("  -> 属于 STM32F40x/41x 家族 (AN2606 口径，F411 常见)")
        elif pid == 0x431:
            print("  -> 与 F411 的 DBGMCU IDCODE 一致")
    print("\n这条 ACK 就是「USB-TTL 能烧录」的直接证据: 芯片的系统存储器 bootloader 正在响应。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
