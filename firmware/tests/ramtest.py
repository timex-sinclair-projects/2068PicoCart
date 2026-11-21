#!/usr/bin/env python3
"""Build RAMTEST.DCK: a machine-code AROS cart that tests the cart's dock-bank RAM.

  ramtest.py [out.dck]

Chunk 4 ($8000) is ROM and holds the test. Chunk 6 ($C000-$DFFF) is RAM with no initial
contents. The AROS chunk map leaves chunks 4 and 6 in the dock and the rest in HOME. The
test also sets the HSR (port $F4) itself, so it doesn't rely on how the ROM reads the map.

The test fills $C000-$DFFF with (H xor L), checks it, then repeats with the complement.
It sets the border green if every byte reads back, red on the first mismatch, then stops
with interrupts off. On the cart's serial status, dock writes rise by 16384 and the last
address ends at $DFFF.
"""
import sys

ORG = 0x8000
code = bytearray()
labels, fixups = {}, []


def emit(*b):
    code.extend(b)


def label(name):
    labels[name] = ORG + len(code)


def jr(op, name):                       # op: 0x18 jr, 0x20 jr nz
    emit(op, 0)
    fixups.append((len(code) - 1, name))


def ld_hl(v): emit(0x21, v & 0xff, v >> 8)
def ld_bc(v): emit(0x01, v & 0xff, v >> 8)


# AROS header: language 2 (machine code), 2 = AROS, start $8008, chunk map (bit set = HOME),
# autostart, two unused bytes
CHUNK_MAP = 0xff & ~((1 << 4) | (1 << 6))
emit(0x02, 0x02, 0x08, 0x80, CHUNK_MAP, 0x01, 0x00, 0x00)

emit(0xF3)                              # di
emit(0x3E, 0x50, 0xD3, 0xF4)            # ld a,$50 : out ($F4),a   chunks 4 and 6 from the dock
emit(0x3E, 0x00, 0xD3, 0xFF)            # ld a,0 : out ($FF),a     DEC = dock, not EXROM

for invert in (False, True):
    n = "inv" if invert else "pat"
    ld_hl(0xC000); ld_bc(0x2000)
    label(f"fill_{n}")
    emit(0x7C, 0xAD)                    # ld a,h : xor l
    if invert: emit(0x2F)               # cpl
    emit(0x77, 0x23, 0x0B, 0x78, 0xB1)  # ld (hl),a : inc hl : dec bc : ld a,b : or c
    jr(0x20, f"fill_{n}")
    ld_hl(0xC000); ld_bc(0x2000)
    label(f"check_{n}")
    emit(0x7C, 0xAD)                    # ld a,h : xor l
    if invert: emit(0x2F)
    emit(0xBE)                          # cp (hl)
    jr(0x20, "fail")
    emit(0x23, 0x0B, 0x78, 0xB1)        # inc hl : dec bc : ld a,b : or c
    jr(0x20, f"check_{n}")

emit(0x3E, 0x04)                        # ld a,4 (green)
jr(0x18, "show")
label("fail")
emit(0x3E, 0x02)                        # ld a,2 (red)
label("show")
emit(0xD3, 0xFE)                        # out ($FE),a
label("stop")
jr(0x18, "stop")

for pos, name in fixups:
    rel = labels[name] - (ORG + pos + 1)
    assert -128 <= rel <= 127, name
    code[pos] = rel & 0xff

chunk4 = bytes(code) + b"\xff" * (8192 - len(code))
types = [0, 0, 0, 0, 2, 0, 1, 0]        # chunk 4 ROM with data, chunk 6 empty RAM
out = sys.argv[1] if len(sys.argv) > 1 else "RAMTEST.DCK"
with open(out, "wb") as f:
    f.write(bytes([0] + types) + chunk4)
print(f"wrote {out}: {len(code)} bytes of code, chunk map {CHUNK_MAP:02x}")
