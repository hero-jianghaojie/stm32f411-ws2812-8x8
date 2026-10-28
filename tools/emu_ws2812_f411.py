"""在 Unicorn 上跑 ws2812_f411 固件, 解码第一帧并核对颜色。"""
import sys

from unicorn import *
from unicorn.arm_const import *

BIN = sys.argv[1] if len(sys.argv) > 1 else "out/ws2812_PA7.bin"
PIN = int(sys.argv[2]) if len(sys.argv) > 2 else 7
NLED = 64

BSRR = 0x40020018
USART1_SR = 0x40011000
USART1_DR = 0x40011004
RCC_CR = 0x40023800
RCC_CFGR = 0x40023808
DWT_CYCCNT = 0xE0001004

data = open(BIN, "rb").read()
sp = int.from_bytes(data[0:4], "little")
pc = int.from_bytes(data[4:8], "little")

uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M4)
uc.mem_map(0x08000000, 0x80000)
uc.mem_map(0x20000000, 0x20000)
uc.mem_map(0x40000000, 0x40000)
uc.mem_map(0xE0000000, 0x100000)
uc.mem_write(0x08000000, data)
uc.reg_write(UC_ARM_REG_SP, sp)

uc.mem_write(RCC_CR, (1 << 25).to_bytes(4, "little"))       # PLLRDY 已就绪
uc.mem_write(RCC_CFGR, (2 << 2).to_bytes(4, "little"))      # SWS = PLL
uc.mem_write(USART1_SR, (0x80).to_bytes(4, "little"))       # TXE 常驻

cyc = 0
edges = []
tx = []
want = NLED * 24


def on_code(uc_, addr, size, user):
    global cyc
    cyc += 1
    uc_.mem_write(DWT_CYCCNT, (cyc & 0xFFFFFFFF).to_bytes(4, "little"))
    if len(edges) >= want * 2:
        raise UcError(UC_ERR_EXCEPTION)


def on_write(uc_, access, addr, size, value, user):
    if addr == BSRR:
        if value & (1 << PIN):
            edges.append((cyc, 1))
        if value & (1 << (PIN + 16)):
            edges.append((cyc, 0))
    elif addr == USART1_DR:
        tx.append(chr(value & 0xFF))


uc.hook_add(UC_HOOK_CODE, on_code)
uc.hook_add(UC_HOOK_MEM_WRITE, on_write)

try:
    uc.emu_start(pc, 0xFFFFFFFF, count=40_000_000)
except UcError as e:
    if "EXCEPTION" not in str(e):
        print("EMU ERR:", e, "pc=0x%08X" % uc.reg_read(UC_ARM_REG_PC))

pairs = []
i = 0
while i + 1 < len(edges):
    (t0, l0), (t1, l1) = edges[i], edges[i + 1]
    if l0 == 1 and l1 == 0:
        pairs.append(t1 - t0)
        i += 2
    else:
        i += 1

print("uart:", repr("".join(tx)[:60]))
print("instructions:", cyc, " bits:", len(pairs))
if pairs:
    print("high width min/max: %d / %d" % (min(pairs), max(pairs)))
    thr = (min(pairs) + max(pairs)) // 2
    bits = [1 if h > thr else 0 for h in pairs]
    out = bytearray()
    for j in range(0, len(bits) // 8 * 8, 8):
        v = 0
        for b in bits[j:j + 8]:
            v = (v << 1) | b
        out.append(v)
    px = [tuple(out[p * 3:p * 3 + 3]) for p in range(len(out) // 3)]
    uniq = sorted(set(px))
    print("threshold=%d  像素种类=%s  前4=%s" % (thr, uniq[:3], px[:4]))
    print("VERDICT:", "PASS" if uniq == [(0, 40, 0)] else "FAIL")
