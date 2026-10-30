#!/usr/bin/env python3
"""Set/read the 4 PRU0-driven pins (P9_31, P9_29, P9_30, P9_28 = bits 0-3)
via the PRUSS shared RAM word that pru0_4pin.asm polls. Requires root
(opens /dev/mem) and requires the pru0-4pin.fw firmware to already be
running on PRU0 (remoteproc1).

Usage:
    sudo python3 pru4pin.py            # print currently applied state
    sudo python3 pru4pin.py 0b1010     # set P9_30 and P9_28 high, read back
"""
import mmap, os, struct, sys

SHARED_RAM_PHYS = 0x4a310000  # PRUSS shared RAM, physical address
MAP_SIZE = 0x1000


def open_map():
    f = os.open("/dev/mem", os.O_RDWR | os.O_SYNC)
    m = mmap.mmap(f, MAP_SIZE, mmap.MAP_SHARED, mmap.PROT_READ | mmap.PROT_WRITE, offset=SHARED_RAM_PHYS)
    return f, m


def set_pins(m, val):
    m.seek(0)
    m.write(struct.pack("<I", val & 0x0F))


def read_applied(m):
    m.seek(4)
    return struct.unpack("<I", m.read(4))[0] & 0x0F


if __name__ == "__main__":
    f, m = open_map()
    if len(sys.argv) > 1:
        set_pins(m, int(sys.argv[1], 0))
    print(f"applied: {read_applied(m):#06b}")
    m.close()
    os.close(f)
