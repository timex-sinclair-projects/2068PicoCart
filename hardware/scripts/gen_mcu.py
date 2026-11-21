#!/usr/bin/env python3
"""mcu.kicad_sch: RP2354B core, after tspico-hardware v3 (proven on proto1-3).

  U1  RP2354B (RP2350B symbol: same QFN-80 pinout; 2 MB flash in package, so the QSPI data
      and clock pins are left open and QSPI_SS only carries BOOTSEL)
  L1, C31, C36: core regulator (design guide fig. 4); R12/C32 VREG_AVDD filter
  C33-C35 DVDD 100n; C20-C28 one 100n per 3.3 V supply pin or pair
  Y1 12 MHz (CL 12 pF), C39/C40 18p, R13 1k in series with XOUT
  R14 1k QSPI_SS -> BOOTSEL; SW2 BOOTSEL button to GND; SWD, RUN and UART test pads
GPIO map: ../SCHEMATIC.md sheet 3. Only GPIO0-39 are 5 V tolerant: every dock signal is on 0-39.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from schlib import Sheet  # noqa: E402

R0603 = "Resistor_SMD:R_0603_1608Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
C0402 = "Capacitor_SMD:C_0402_1005Metric"
L_FP = "picocart:L_Abracon_AOTA-B201610"
TP_FP = "TestPoint:TestPoint_Pad_1.0x1.0mm"
OUT, IN, BI = "output", "input", "bidirectional"

GPIO = {
    **{i: (f"A{i}", IN) for i in range(16)},
    **{16 + i: (f"MD{i}", BI) for i in range(8)},
    24: ("ROSCS_n", IN), 25: ("MREQ_n", IN), 26: ("RD_n", IN), 27: ("WR_n", IN),
    28: ("IORQ_n", IN), 29: ("M1_n", IN), 30: ("RFSH_n", IN), 31: ("EXROM", IN),
    32: ("BUS_OFF", IN), 33: ("BE_GPIO", BI), 37: ("A7R", IN),
    40: ("DOCK_SENSE", IN), 41: ("VBUS_SENSE", IN),
    # 3.3 V-only controls on the top-left pins, under the buttons and LEDs (pins 54-56)
    43: ("LED2", OUT), 44: ("LED", OUT), 45: ("MENU_n", IN),
}
LOCAL = {46: "UART_TX", 47: "UART_RX"}
SPARE = [34, 35, 36, 38, 39, 42]  # 34/35 were I2C for an OLED (dropped: the dock hides it); 38/39 (pins 47/48) sit under the DOCK_SENSE pre-route: keep them spare


def main():
    s = Sheet("picocart.kicad_sch", "mcu.kicad_sch", "2068 PicoCart: RP2354B Core")
    s.text("RP2354B core per 'Hardware design with RP2350' (as tspico-hardware v3). GPIO map: ../SCHEMATIC.md sheet 3.", 20, 280, 2.2)
    s.text("GPIO40-47 are NOT 5 V tolerant: only the ADC sense dividers (<= 3.0 V) use them. GPIO43-45 drive only the LEDs/MENU (3.3 V). Spare: GPIO34, 35, 36, 38, 39, 42.", 20, 285, 2.0)
    s.power("+3V3", 300, 30, "#PWR03", flag=False)
    s.power("GND", 330, 40, "#PWR04", flag=False)

    U = s.place("MCU_RaspberryPi:RP2350B", "U1", "RP2354B", "Package_DFN_QFN:QFN-80-1EP_10x10mm_P0.4mm_EP3.4x3.4mm", 130, 150)
    names = U.names()
    by_name = {}
    for num, nm in names.items():
        by_name.setdefault(nm.split("/")[0], num)
    for g, (net, shape) in GPIO.items():
        s.glabel(net, U.pin(by_name[f"GPIO{g}"]), shape)
    for g, net in LOCAL.items():
        s.label(net, U.pin(by_name[f"GPIO{g}"]))
    for g in SPARE:
        s.nc(U.pin(by_name[f"GPIO{g}"]))
    for num, nm in names.items():
        if nm in ("IOVDD", "QSPI_IOVDD", "USB_OTP_VDD", "ADC_AVDD", "VREG_VIN"):
            s.label("+3V3", U.pin(num))
        elif nm == "VREG_AVDD":
            s.label("VREG_AVDD", U.pin(num))
        elif nm in ("GND", "VREG_PGND"):
            s.label("GND", U.pin(num))
        elif nm == "DVDD":
            s.label("DVDD", U.pin(num))
    s.label("VREG_LX", U.pin(by_name["VREG_LX"]))
    s.label("DVDD", U.pin(by_name["VREG_FB"]))
    s.label("XIN", U.pin(by_name["XIN"])); s.label("XOUT", U.pin(by_name["XOUT"]))
    s.label("SWCLK", U.pin(by_name["SWCLK"])); s.label("SWDIO", U.pin(by_name["SWDIO"]))
    s.label("RUN", U.pin(by_name["RUN"]))
    s.glabel("USB_DM", U.pin(by_name["USB_DM"]), BI); s.glabel("USB_DP", U.pin(by_name["USB_DP"]), BI)
    for nm in ("QSPI_SD0", "QSPI_SD1", "QSPI_SD2", "QSPI_SD3", "QSPI_SCLK"):
        s.nc(U.pin(by_name[nm]))          # bonded to the in-package flash: no copper
    s.label("nQSPI_SS", U.pin(by_name["~{QSPI_SS}"]))

    # ---- core regulator ---------------------------------------------------------------
    L1 = s.place("Device:L", "L1", "3.3uH", L_FP, 250, 60)
    s.label("VREG_LX", L1.pin("1")); s.label("DVDD", L1.pin("2"))     # dot = pad 2 = DVDD side
    for k, (ref, net, val, fp) in enumerate([("C36", "DVDD", "4.7u", C0402), ("C31", "+3V3", "4.7u", C0402),
                                             ("C33", "DVDD", "100n", C0603), ("C34", "DVDD", "100n", C0603), ("C35", "DVDD", "100n", C0603)]):
        C = s.place("Device:C", ref, val, fp, 265 + 10 * k, 60)
        s.label(net, C.pin("1")); s.label("GND", C.pin("2"))
    s.text("L1 Abracon AOTA-B201610S3R3-101-T (polarised: dot = pad 2 = DVDD). C36 COUT, C31 CIN at VREG_VIN. C33-C35 at DVDD pins 51, 32, 10.", 245, 75, 1.5)
    R12 = s.place("Device:R", "R12", "33", R0603, 300, 85)
    s.label("+3V3", R12.pin("1")); s.label("VREG_AVDD", R12.pin("2"))
    C32 = s.place("Device:C", "C32", "4.7u", C0402, 315, 85)
    s.label("VREG_AVDD", C32.pin("1")); s.label("GND", C32.pin("2"))
    FL2 = s.place("power:PWR_FLAG", "#FLG02", "PWR_FLAG", "", 330, 85)
    s.label("VREG_AVDD", FL2.pin("1"))
    FL = s.place("power:PWR_FLAG", "#FLG03", "PWR_FLAG", "", 300, 60)
    s.label("DVDD", FL.pin("1"))

    # ---- 3.3 V decoupling: 9 x 100 nF --------------------------------------------------
    for k in range(9):
        C = s.place("Device:C", f"C{20 + k}", "100n", C0603, 250 + 9 * k, 100)
        s.label("+3V3", C.pin("1")); s.label("GND", C.pin("2"))
    s.text("C20-C28: one per 3.3 V supply pin or adjacent pair (5, 15, 24, 29, 41, 50, 59/60, 68/69, 76); pin 64 has C31.", 245, 115, 1.5)
    C38 = s.place("Device:C", "C38", "10u", C0603, 340, 100)
    s.label("+3V3", C38.pin("1")); s.label("GND", C38.pin("2"))

    # ---- crystal ----------------------------------------------------------------------
    Y1 = s.place("Device:Crystal_GND24", "Y1", "12MHz", "Crystal:Crystal_SMD_3225-4Pin_3.2x2.5mm", 270, 150)
    s.label("XIN", Y1.pin("1")); s.label("XOUT_R", Y1.pin("3")); s.label("GND", Y1.pin("2")); s.label("GND", Y1.pin("4"))
    R13 = s.place("Device:R", "R13", "1k", R0603, 295, 150)
    s.label("XOUT", R13.pin("1")); s.label("XOUT_R", R13.pin("2"))
    C39 = s.place("Device:C", "C39", "18p", C0603, 255, 165)
    s.label("XIN", C39.pin("1")); s.label("GND", C39.pin("2"))
    C40 = s.place("Device:C", "C40", "18p", C0603, 285, 165)
    s.label("XOUT_R", C40.pin("1")); s.label("GND", C40.pin("2"))
    s.text("Y1 YXC X322512MOB4SI 12 MHz CL 12 pF: C39/C40 18 pF (~3 pF stray). R13 1 k in series with XOUT.", 245, 180, 1.5)

    # ---- BOOTSEL, debug, UART -----------------------------------------------------------
    R14 = s.place("Device:R", "R14", "1k", R0603, 250, 210)
    s.label("nQSPI_SS", R14.pin("1")); s.label("BOOTSEL", R14.pin("2"))
    SW2 = s.place("Switch:SW_Push", "SW2", "BOOTSEL", "Button_Switch_SMD:SW_Push_1P1T_XKB_TS-1187A", 250, 195)
    s.label("BOOTSEL", SW2.pin("1")); s.label("GND", SW2.pin("2"))
    pads = [("TP1", "SWCLK"), ("TP2", "SWDIO"), ("TP3", "RUN"), ("TP4", "+3V3"), ("TP5", "GND"),
            ("TP6", "UART_TX"), ("TP7", "UART_RX")]
    for k, (ref, net) in enumerate(pads):
        T = s.place("Connector:TestPoint", ref, net, TP_FP, 270 + 12 * k, 230)
        s.label(net, T.pin("1"))
    s.text("TP1-TP5 SWD/RUN/power, TP6/TP7 UART. SW2 BOOTSEL: hold while plugging in USB (or while pulsing RUN) for the ROM bootloader. RUN has the chip's internal pull-up.", 245, 245, 1.5)

    s.write()
    print("wrote mcu.kicad_sch")


if __name__ == "__main__":
    main()
