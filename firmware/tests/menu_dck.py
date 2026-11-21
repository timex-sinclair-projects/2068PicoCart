#!/usr/bin/env python3
"""Build MENUTEST.DCK: the cart menu with a directory table filled in, for an emulator.

  menu_dck.py menu.bin out.dck [N] [CURRENT] [--ram-mailbox]

N test carts (default 25, enough to scroll) named like the firmware would list them;
CURRENT (1-N, default 3) is the one marked current. Chunks 4 and 5 are ROM, as the
firmware serves them; chunk 5 holds the mailbox at $BFFF. With --ram-mailbox, chunk 5 is
RAM instead, so an emulator keeps the menu's mailbox write where it can be read back.
"""
import sys

ram_mailbox = "--ram-mailbox" in sys.argv
sys.argv = [a for a in sys.argv if a != "--ram-mailbox"]
menu = open(sys.argv[1], "rb").read()
out = sys.argv[2]
n = int(sys.argv[3]) if len(sys.argv) > 3 else 25
cur = int(sys.argv[4]) if len(sys.argv) > 4 else 3

img = bytearray(b"\xff" * 16384)                       # $8000-$BFFF
img[0:len(menu)] = menu
names = [f"Test cart {i + 1:02d}.dck" for i in range(n)]
if n >= 2:
    names[1] = "A very long cartridge file name that will not fit.dck"
img[0x1000:0x1010] = bytes([n, cur] + [0] * 14)
for i, name in enumerate(names):
    b = name.encode()[:31]
    img[0x1010 + 32 * i:0x1010 + 32 * i + 32] = b + bytes(32 - len(b))
with open(out, "wb") as f:
    f.write(bytes([0, 0, 0, 0, 0, 2, 3 if ram_mailbox else 2, 0, 0]) + img)
print(f"wrote {out}: {n} carts, current {cur}")
