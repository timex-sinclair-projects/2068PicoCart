#!/usr/bin/env python3
"""power.kicad_sch: power input, 3V3 regulator, sense dividers, USB-C, MENU button, LED.

  D1/D2  B5819W diode-OR: DOCK_5V and VBUS -> +5V (5V_INT). Neither source back-feeds the other.
  U4     AP2112K-3.3, C5-C9
  R2/R3, R4/R5  10k/15k dividers -> DOCK_SENSE, VBUS_SENSE (ADC0/ADC1, 3.0 V at 5 V)
  J2     USB-C C5178539 (HRO TYPE-C-31-M-12 land pattern), R6/R7 5k1 Rd, U5 USBLC6-2SC6,
         R8/R9 33R series (as tspico v3: JLC basic; Raspberry Pi suggests 27R)
  SW1    MENU, D3/R11 and D4/R15 status LEDs
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from schlib import Sheet  # noqa: E402

R0603 = "Resistor_SMD:R_0603_1608Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
C0805 = "Capacitor_SMD:C_0805_2012Metric"
OUT, IN, BI = "output", "input", "bidirectional"


def rc(s, kind, ref, val, fp, x, y, a, b, ga=False, gb=False):
    P = s.place("Device:" + kind, ref, val, fp, x, y)
    (s.glabel if ga else s.label)(a, P.pin("1"), *([BI] if ga else []))
    (s.glabel if gb else s.label)(b, P.pin("2"), *([BI] if gb else []))
    return P


def main():
    s = Sheet("picocart.kicad_sch", "power.kicad_sch", "2068 PicoCart: Power, USB, Controls")
    s.text("Power: dock +5V and USB VBUS diode-ORed into +5V (5V_INT), AP2112K-3.3. The firmware keeps BUS_OFF high unless DOCK_SENSE shows the 2068 is on.", 20, 280, 2.0)
    s.power("+3V3", 250, 30, "#PWR05", flag=False)
    s.power("+5V", 280, 30, "#PWR06", flag=True)
    s.power("GND", 310, 30, "#PWR07", flag=False)

    # ---- diode-OR, regulator --------------------------------------------------------------
    D1 = s.place("Device:D_Schottky", "D1", "B5819W", "Diode_SMD:D_SOD-123", 60, 50)
    s.label("+5V", D1.pin("1")); s.glabel("DOCK_5V", D1.pin("2"), IN)
    D2 = s.place("Device:D_Schottky", "D2", "B5819W", "Diode_SMD:D_SOD-123", 60, 70)
    s.label("+5V", D2.pin("1")); s.label("VBUS", D2.pin("2"))
    rc(s, "C", "C5", "10u", C0805, 90, 50, "+5V", "GND")
    rc(s, "C", "C6", "100n", C0603, 100, 50, "+5V", "GND")
    U4 = s.place("Regulator_Linear:AP2112K-3.3", "U4", "AP2112K-3.3", "Package_TO_SOT_SMD:SOT-23-5", 140, 60)
    s.label("+5V", U4.pin("VIN")); s.label("+5V", U4.pin("EN")); s.label("GND", U4.pin("GND"))
    s.label("+3V3", U4.pin("VOUT")); s.nc(U4.pin("NC"))
    rc(s, "C", "C7", "1u", C0603, 115, 75, "+5V", "GND")
    rc(s, "C", "C8", "10u", C0603, 170, 75, "+3V3", "GND")
    rc(s, "C", "C9", "1u", C0603, 180, 75, "+3V3", "GND")

    # ---- sense dividers -------------------------------------------------------------------
    rc(s, "R", "R2", "10k", R0603, 60, 110, "DOCK_5V", "DOCK_SENSE", ga=True, gb=True)
    rc(s, "R", "R3", "15k", R0603, 75, 110, "DOCK_SENSE", "GND", ga=True)
    rc(s, "C", "C10", "100n", C0603, 90, 110, "DOCK_SENSE", "GND", ga=True)
    rc(s, "R", "R4", "10k", R0603, 60, 135, "VBUS", "VBUS_SENSE", gb=True)
    rc(s, "R", "R5", "15k", R0603, 75, 135, "VBUS_SENSE", "GND", ga=True)
    rc(s, "C", "C11", "100n", C0603, 90, 135, "VBUS_SENSE", "GND", ga=True)

    # ---- USB-C ----------------------------------------------------------------------------
    J2 = s.place("Connector:USB_C_Receptacle_USB2.0_16P", "J2", "USB-C", "Connector_USB:USB_C_Receptacle_HRO_TYPE-C-31-M-12", 220, 160)
    for pin in ("A4", "A9", "B4", "B9"):
        s.label("VBUS", J2.pin(pin))
    for pin in ("A8", "B8"):
        s.nc(J2.pin(pin))
    for pin in ("A1", "A12", "B1", "B12", "SH"):
        s.label("GND", J2.pin(pin))
    s.label("USB_DP_C", J2.pin("A6")); s.label("USB_DP_C", J2.pin("B6"))
    s.label("USB_DM_C", J2.pin("A7")); s.label("USB_DM_C", J2.pin("B7"))
    s.label("CC1", J2.pin("A5")); s.label("CC2", J2.pin("B5"))
    F = s.place("power:PWR_FLAG", "#FLG04", "PWR_FLAG", "", 260, 120)
    s.label("VBUS", F.pin("1"))
    rc(s, "R", "R6", "5k1", R0603, 270, 140, "CC1", "GND")
    rc(s, "R", "R7", "5k1", R0603, 285, 140, "CC2", "GND")
    rc(s, "R", "R8", "33", R0603, 270, 180, "USB_DP_C", "USB_DP", gb=True)
    rc(s, "R", "R9", "33", R0603, 285, 180, "USB_DM_C", "USB_DM", gb=True)
    rc(s, "C", "C12", "10u", C0805, 300, 140, "VBUS", "GND")
    U5 = s.place("Power_Protection:USBLC6-2SC6", "U5", "USBLC6-2SC6", "Package_TO_SOT_SMD:SOT-23-6", 320, 180)
    s.label("USB_DP_C", U5.pin("1")); s.label("USB_DP_C", U5.pin("6"))
    s.label("USB_DM_C", U5.pin("3")); s.label("USB_DM_C", U5.pin("4"))
    s.label("GND", U5.pin("2")); s.label("VBUS", U5.pin("5"))
    s.text("J2: USB 2.0 device. R6/R7 separate 5.1 k Rd per CC pin. U5 between J2 and R8/R9.", 200, 210, 1.5)

    # ---- MENU button, LED ------------------------------------------------------------------
    SW1 = s.place("Switch:SW_Push", "SW1", "MENU", "Button_Switch_SMD:SW_Push_1P1T_XKB_TS-1187A", 60, 200)
    s.glabel("MENU_n", SW1.pin("1"), OUT); s.label("GND", SW1.pin("2"))
    R11 = s.place("Device:R", "R11", "1k", R0603, 60, 230)
    s.glabel("LED", R11.pin("1"), IN); s.label("LED_A", R11.pin("2"))
    D3 = s.place("Device:LED", "D3", "red", "LED_SMD:LED_0603_1608Metric", 90, 230)
    s.label("GND", D3.pin("1")); s.label("LED_A", D3.pin("2"))
    R15 = s.place("Device:R", "R15", "3k3", R0603, 60, 260)   # white LED: 100 R was far too bright
    s.glabel("LED2", R15.pin("1"), IN); s.label("LED2_A", R15.pin("2"))
    D4 = s.place("Device:LED", "D4", "white", "LED_SMD:LED_0603_1608Metric", 90, 260)
    s.label("GND", D4.pin("1")); s.label("LED2_A", D4.pin("2"))
    s.text("SW1 MENU (internal pull-up on GPIO45): hold at power-on for the menu. D3 red LED on GPIO44, D4 white LED on GPIO43 (R15 3k3: a white LED is far brighter than the red one at the same current).", 30, 270, 1.5)


    s.write()
    print("wrote power.kicad_sch")


if __name__ == "__main__":
    main()
