"""离线自测: 用假串口设备跑一遍 flash_an3155 的擦除/写入/校验流程。

不接硬件, 只验证协议时序与参数有没有写错(例如缺 --baud 这类错误)。
"""
import sys
import types

ACK, NACK = 0x79, 0x1F
MEM = {}


class FakeSerial:
    def __init__(self, port=None, baud=115200, **kw):
        self.inb = bytearray()
        self.state = "idle"
        self.pending = bytearray()
        self.written = 0

    # --- 串口 API ---
    def reset_input_buffer(self):
        self.inb.clear()

    def read(self, n=1):
        out = bytes(self.inb[:n])
        del self.inb[:n]
        return out

    def write(self, data):
        data = bytes(data)
        if self.state == "idle":
            if data[0] == 0x7F:
                self.inb += bytes([ACK])
                return
            cmd = data[0]
            if cmd == 0x00:                      # GET
                cmds = bytes([0x00, 0x01, 0x02, 0x11, 0x21, 0x31, 0x43, 0x44,
                              0x63, 0x73, 0x82, 0x92])
                self.inb += bytes([ACK, len(cmds), 0x1F]) + cmds + bytes([ACK])
            elif cmd == 0x02:                    # GET_ID
                self.inb += bytes([ACK, 0x01, 0x04, 0x31, ACK])
            elif cmd in (0x43, 0x44):            # ERASE
                self.inb += bytes([ACK])
                self.state = "erase_%02X" % cmd
            elif cmd == 0x31:                    # WRITE
                self.inb += bytes([ACK])
                self.state = "write_addr"
            elif cmd == 0x11:                    # READ
                self.inb += bytes([ACK])
                self.state = "read_addr"
            elif cmd == 0x21:                    # GO
                self.inb += bytes([ACK])
                self.state = "go_addr"
            else:
                self.inb += bytes([NACK])
            return

        if self.state == "erase_44":
            self.inb += bytes([ACK]); MEM.clear(); self.state = "idle"; return
        if self.state == "erase_43":
            self.inb += bytes([ACK]); MEM.clear(); self.state = "idle"; return
        if self.state == "write_addr":
            addr = int.from_bytes(data[:4], "big")
            ok = (data[4] == 0x00) or True
            self.addr = addr
            self.inb += bytes([ACK]); self.state = "write_data"; return
        if self.state == "write_data":
            n = data[0] + 1
            payload = data[1:1 + n]
            for i, b in enumerate(payload):
                MEM[self.addr + i] = b
            self.written += n
            self.inb += bytes([ACK]); self.state = "idle"; return
        if self.state == "read_addr":
            self.addr = int.from_bytes(data[:4], "big")
            self.inb += bytes([ACK]); self.state = "read_len"; return
        if self.state == "read_len":
            n = data[0] + 1
            self.inb += bytes([ACK])
            self.inb += bytes(MEM.get(self.addr + i, 0xFF) for i in range(n))
            self.state = "idle"; return
        if self.state == "go_addr":
            self.inb += bytes([ACK]); self.state = "idle"; return

    def close(self):
        pass





def main():
    import serial
    real = serial.Serial
    serial.Serial = FakeSerial
    try:
        import importlib.util
        spec = importlib.util.spec_from_file_location("flashmod", "tools/flash_an3155.py")
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        sys.argv = ["flash_an3155.py", "--port", "COMX", "--hex",
                    "out/f411_uart_test.hex", "--chunk", "64", "--retry", "2"] + sys.argv[1:]
        rc = mod.main()
    finally:
        serial.Serial = real

    mem = mod.parse_hex("out/f411_uart_test.hex")
    bad = [a for a, v in mem.items() if MEM.get(a) != v]
    print("rc=", rc, " 写入字节=", len(MEM), " 不一致=", len(bad))
    ok = (rc == 0 and not bad)
    print("SIM:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
