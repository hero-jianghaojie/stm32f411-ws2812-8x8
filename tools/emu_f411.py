import sys
from unicorn import *
from unicorn.arm_const import *

BIN = "out/f411_uart_test.bin"
data = open(BIN, "rb").read()
sp = int.from_bytes(data[0:4], "little")
pc = int.from_bytes(data[4:8], "little")

USART1_SR = 0x40011000
USART1_DR = 0x40011004
GPIOA_BSRR = 0x40020018
DWT_CYCCNT = 0xE0001004

uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M4)
uc.mem_map(0x08000000, 0x80000)
uc.mem_map(0x20000000, 0x20000)
uc.mem_map(0x40000000, 0x40000)
uc.mem_map(0xE0000000, 0x100000)
uc.mem_write(0x08000000, data)
uc.reg_write(UC_ARM_REG_SP, sp)
# USART1 SR 常驻 TXE=1, 否则 uart_putc 死等
uc.mem_write(USART1_SR, (0x80).to_bytes(4, "little"))

cyc = [0]
tx = []
gpio = []
done = [False]


def on_code(uc_, addr, size, user):
    cyc[0] += 1
    uc_.mem_write(DWT_CYCCNT, (cyc[0] & 0xFFFFFFFF).to_bytes(4, "little"))
    if len(tx) >= 56:
        raise UcError(UC_ERR_EXCEPTION)


def on_write(uc_, access, addr, size, value, user):
    if addr == USART1_DR:
        tx.append(chr(value & 0xFF))
    elif addr == GPIOA_BSRR:
        gpio.append(value)


uc.hook_add(UC_HOOK_CODE, on_code)
uc.hook_add(UC_HOOK_MEM_WRITE, on_write)

try:
    uc.emu_start(pc, 0xFFFFFFFF, count=3_000_000)
except UcError as e:
    pass

s = "".join(tx)
print("instructions:", cyc[0])
print("uart bytes  :", len(tx))
print("uart text   :", repr(s))
print("gpio writes :", len(gpio), [hex(v) for v in gpio[:3]])

ok = "F411-RUN-OK" in s and "PA9/PA10" in s and "115200" in s
print("VERDICT:", "PASS" if ok else "FAIL")
sys.exit(0 if ok else 1)
