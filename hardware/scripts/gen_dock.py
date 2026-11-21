#!/usr/bin/env python3
"""dock.kicad_sch: the TS2068 dock fingers, the data-bus buffer and its enable gate.

  J1  TS2068_Cart36: every dock signal goes to the MCU sheet (BE through R10, DNP)
  U2  74LVC245 (3V3): A side = dock D0-D7, B side = MD0-MD7. DIR = /RD (read: B->A, the
      cart drives the bus), /OE = BUF_OE_n
  U3  74LVC1G332: BUF_OE_n = /ROSCS OR /MREQ OR BUS_OFF
  R1  10k pull-up on BUS_OFF: the bus stays released until the firmware is serving
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from schlib import Sheet  # noqa: E402

R0603 = "Resistor_SMD:R_0603_1608Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
C0805 = "Capacitor_SMD:C_0805_2012Metric"
OUT, IN, BI = "output", "input", "bidirectional"

# dock pin -> (net, shape as seen from this sheet); None = local
J1_NETS = {
    1: "A14", 2: "DOCK_5V", 3: "A12", 4: "A13", 5: "D0", 6: "D7", 7: "D1", 8: "A0", 9: "D2", 10: "A1",
    11: "D6", 12: "A2", 13: "D5", 14: "A3", 15: "D3", 16: "A15", 17: "D4", 18: "MREQ_n", 19: "IORQ_n",
    20: "A7R", 21: "RD_n", 22: "M1_n", 23: "WR_n", 24: "A8", 25: "A7", 26: "A9", 27: "A6", 28: "A10",
    29: "A5", 30: "A11", 31: "A4", 32: "RFSH_n", 33: "BE", 34: "EXROM", 35: "ROSCS_n", 36: "GND",
}
LOCAL = {"D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7", "BE", "GND"}


def main():
    s = Sheet("picocart.kicad_sch", "dock.kicad_sch", "2068 PicoCart: Dock Interface")
    s.text("Dock fingers: odd pins on the bottom side, even on the top (EasyEDA 2068-cart-SMD). A13B/A14B/A15B wired straight; firmware can invert.", 20, 270, 2.0)
    s.text("U2 only drives the dock during a dock-selected memory cycle (/ROSCS and /MREQ low) and only after the firmware pulls BUS_OFF low.", 20, 275, 2.0)
    s.power("+3V3", 250, 30, "#PWR01", flag=False)
    s.power("GND", 280, 30, "#PWR02", flag=True)

    J1 = s.place("picocart:TS2068_Cart36", "J1", "TS2068_Cart36", "picocart:TS2068_Cart36", 80, 140)
    for pin, net in J1_NETS.items():
        if net in LOCAL:
            s.label(net, J1.pin(str(pin)))
        elif net == "DOCK_5V":
            s.glabel(net, J1.pin(str(pin)), OUT)
        else:
            s.glabel(net, J1.pin(str(pin)), IN)
    F = s.place("power:PWR_FLAG", "#FLG01", "PWR_FLAG", "", 40, 60)
    s.glabel("DOCK_5V", F.pin("1"), OUT)
    C1 = s.place("Device:C", "C1", "10u", C0805, 40, 80)
    s.glabel("DOCK_5V", C1.pin("1"), OUT); s.label("GND", C1.pin("2"))
    C2 = s.place("Device:C", "C2", "100n", C0603, 55, 80)
    s.glabel("DOCK_5V", C2.pin("1"), OUT); s.label("GND", C2.pin("2"))
    R10 = s.place("Device:R", "R10", "0", R0603, 40, 230, dnp=True)
    s.label("BE", R10.pin("1")); s.glabel("BE_GPIO", R10.pin("2"), BI)
    s.text("C1/C2 at finger 2. R10 (DNP) connects BE to GPIO33 only if wanted.", 20, 250, 1.5)

    # ---- U2 74LVC245: pin 1 DIR, 2-9 A0-A7, 10 GND, 11-18 B7-B0, 19 /OE, 20 VCC ------------
    U2 = s.place("74xx:74LS245", "U2", "74LVC245APW", "Package_SO:TSSOP-20_4.4x6.5mm_P0.65mm", 200, 140)
    s.glabel("RD_n", U2.pin("1"), IN)
    for k in range(8):
        s.label(f"D{k}", U2.pin(str(2 + k)))
        s.glabel(f"MD{k}", U2.pin(str(18 - k)), BI)
    s.label("GND", U2.pin("10"))
    s.label("BUF_OE_n", U2.pin("19"))
    s.label("+3V3", U2.pin("20"))
    C3 = s.place("Device:C", "C3", "100n", C0603, 240, 120)
    s.label("+3V3", C3.pin("1")); s.label("GND", C3.pin("2"))

    # ---- U3 74LVC1G332: 1, 3, 6 inputs, 4 Y, 5 VCC, 2 GND --------------------------------
    U3 = s.place("74xGxx:74LVC1G332", "U3", "74LVC1G332", "Package_TO_SOT_SMD:SOT-363_SC-70-6", 200, 220)
    s.glabel("ROSCS_n", U3.pin("1"), IN)
    s.glabel("MREQ_n", U3.pin("3"), IN)
    s.glabel("BUS_OFF", U3.pin("6"), IN)
    s.label("BUF_OE_n", U3.pin("4"))
    s.label("+3V3", U3.pin("5")); s.label("GND", U3.pin("2"))
    C4 = s.place("Device:C", "C4", "100n", C0603, 240, 210)
    s.label("+3V3", C4.pin("1")); s.label("GND", C4.pin("2"))
    R1 = s.place("Device:R", "R1", "10k", R0603, 240, 235)
    s.label("+3V3", R1.pin("1")); s.glabel("BUS_OFF", R1.pin("2"), IN)
    s.text("BUF_OE_n = /ROSCS | /MREQ | BUS_OFF. R1 holds BUS_OFF high through reset, boot and USB-only power.", 170, 250, 1.5)

    s.write()
    print("wrote dock.kicad_sch")


if __name__ == "__main__":
    main()
