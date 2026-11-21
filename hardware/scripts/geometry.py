"""Board outline and dock edge-connector geometry for the 2068 PicoCart.

Source: the EasyEDA export PCB_2068-cart-SMD_2026-11-11.json (layer 10 outline,
J1 pads), the board that fits Joe's shell. EasyEDA units are 10 mil, y down.

KiCad board frame: the EasyEDA board turned 180 degrees, so the dock fingers are
at the BOTTOM (as in tspico-hardware) and the USB-C edge is at the top (y = 0):
    X = (678.15 - x) * 0.254,  Y = (330.7 - y) * 0.254   (mm)
Turning in the plane keeps the sides: EasyEDA TopLayer (even pins, the parts)
is still F.Cu.
"""

REV = "0.2"               # board revision: title blocks and the underside silkscreen
U = 0.254
XR, YB = 678.15, 330.7


def T(x, y):
    return (round((XR - x) * U, 4), round((YB - y) * U, 4))


# Outline as a closed chain in EasyEDA coordinates: ("L", point) is a line to the
# point, ("A", mid, end) an arc through mid to end. Starts at the left shoulder.
_CHAIN = [
    ("P", (475.86, 185.30)),
    ("L", (475.86, 140.18)), ("L", (478.36, 137.68)), ("L", (495.86, 137.68)),
    ("L", (498.36, 140.18)), ("L", (498.36, 166.48)),
    ("A", (500.86, 168.98), (503.36, 166.48)),          # key slot bottom, r 0.635 mm
    ("L", (503.36, 140.18)), ("L", (505.86, 137.68)), ("L", (663.36, 137.68)),
    ("L", (665.86, 140.18)), ("L", (665.86, 185.30)),
    ("A", (667.32, 188.84), (670.86, 190.30)),          # right shoulder fillet (concave)
    ("L", (673.15, 190.30)),
    ("A", (676.69, 191.76), (678.15, 195.30)),          # body corner (convex)
    ("L", (678.15, 225.30)), ("L", (660.43, 225.30)), ("L", (660.43, 276.77)),   # shell key notch
    ("L", (678.15, 276.77)), ("L", (678.15, 324.60)), ("L", (664.17, 324.60)),
    ("L", (664.17, 330.70)), ("L", (477.80, 330.70)), ("L", (477.80, 324.60)),   # USB-C edge, corner steps
    ("L", (463.55, 324.60)), ("L", (463.55, 297.74)), ("L", (483.46, 297.74)),
    ("L", (483.46, 226.87)), ("L", (463.55, 226.87)),                           # shell key notch
    ("L", (463.55, 195.30)),
    ("A", (465.01, 191.76), (468.55, 190.30)),          # body corner (convex)
    ("L", (470.86, 190.30)),
    ("A", (474.40, 188.84), (475.86, 185.30)),          # left shoulder fillet (concave)
]


def outline():
    """List of ("line", a, b) / ("arc", a, mid, b) in KiCad mm."""
    segs = []
    cur = T(*_CHAIN[0][1])
    for item in _CHAIN[1:]:
        if item[0] == "L":
            nxt = T(*item[1])
            segs.append(("line", cur, nxt))
        else:
            mid, nxt = T(*item[1]), T(*item[2])
            segs.append(("arc", cur, mid, nxt))
        cur = nxt
    assert cur == T(*_CHAIN[0][1])
    return segs


BOARD_W = round((XR - 463.55) * U, 3)      # 54.52
BOARD_H = round((YB - 137.68) * U, 3)      # 49.03
BODY_H = round((YB - 190.30) * U, 3)       # 35.66: y of the shoulders (body / tongue)

# Dock connector: 18 positions at 0.1 in, pins 2k+1 (B.Cu) and 2k+2 (F.Cu) at x = 485.85 + 10k
PIN_NAMES = {
    1: "A14B", 2: "+5V", 3: "A12", 4: "A13B", 5: "D0", 6: "D7", 7: "D1", 8: "A0", 9: "D2",
    10: "A1", 11: "D6", 12: "A2", 13: "D5", 14: "A3", 15: "D3", 16: "A15B", 17: "D4",
    18: "~{MREQ}", 19: "~{IORQ}", 20: "A7RB", 21: "~{RD}", 22: "~{M1}", 23: "~{WR}", 24: "A8",
    25: "A7", 26: "A9", 27: "A6", 28: "A10", 29: "A5", 30: "A11", 31: "A4", 32: "~{RFSH}",
    33: "BE", 34: "EXROM", 35: "~{ROSCS}", 36: "GND",
}
FINGER_W = 5 * U          # 1.27
FINGER_TOP = 167.7        # EasyEDA y of the finger end away from the tip
FINGER_TIP = 143.7        # signal fingers stop 1.52 mm short of the tip
POWER_TIP = 137.68 + 0.3 / U   # pins 2 and 36 (mate first) run to 0.3 mm from the tip (EasyEDA: to the edge)
SLOT_TRIM = 0.32          # fingers beside the key slot lose 0.3 mm on the slot side (EasyEDA: flush with the slot)


def fingers():
    """[(pin, layer, cx, cy, w, h)] in KiCad mm."""
    out = []
    for k in range(18):
        x = 485.85 + 10 * k
        for pin, layer in ((2 * k + 1, "B.Cu"), (2 * k + 2, "F.Cu")):
            tip = POWER_TIP if pin in (2, 36) else FINGER_TIP
            x0, x1 = (x - 2.5) , (x + 2.5)
            X0, X1 = sorted((T(x0, 0)[0], T(x1, 0)[0]))
            if k == 1:          # pins 3/4: slot on their EasyEDA right = KiCad left
                X0 += SLOT_TRIM
            if k == 2:          # pins 5/6: slot on their EasyEDA left = KiCad right
                X1 -= SLOT_TRIM
            Y0, Y1 = sorted((T(0, FINGER_TOP)[1], T(0, tip)[1]))
            out.append((pin, layer, round((X0 + X1) / 2, 4), round((Y0 + Y1) / 2, 4), round(X1 - X0, 4), round(Y1 - Y0, 4)))
    return out
