"""Orderable part numbers for the 2068 PicoCart BOM.

Keyed by (Value, footprint basename). Each entry: (MPN, LCSC, note).
"basic" = JLCPCB basic part (no loading fee). Stock checked on jlcpcb.com 2026-11-12
for the parts marked "JLC stock"; passives reuse the TS-Pico v3 table (proven at JLC).
"""

R = "R_0603_1608Metric"
C6 = "C_0603_1608Metric"
C4 = "C_0402_1005Metric"
C8 = "C_0805_2012Metric"

PARTS = {
    # ---- resistors (0603, 1%) ------------------------------------------------
    ("0", R): ("0603WAF0000T5E", "C21189", "basic"),
    ("33", R): ("0603WAF330JT5E", "C23140", "basic"),
    ("100", R): ("0603WAF1000T5E", "C22775", "basic"),
    ("1k", R): ("0603WAF1001T5E", "C21190", "basic"),
    ("3k3", R): ("0603WAF3301T5E", "C22978", "basic"),
    ("5k1", R): ("0603WAF5101T5E", "C23186", "basic"),
    ("10k", R): ("0603WAF1002T5E", "C25804", "basic"),
    ("15k", R): ("0603WAF1502T5E", "C22809", "basic"),
    # ---- capacitors ----------------------------------------------------------
    ("18p", C6): ("CL10C180JB8NNNC", "C1647", "basic, C0G; JLC stock 389,973"),
    ("100n", C6): ("CC0603KRX7R9BB104", "C14663", "basic, X7R 50V"),
    ("1u", C6): ("CL10A105KB8NNNC", "C15849", "basic, X5R 25V"),
    ("4.7u", C4): ("CL05A475MP5NRNC", "C23733", "basic, X5R 10V, 0402 as in the RP2350 design guide; JLC stock 758,151"),
    ("10u", C6): ("CL10A106KP8NNNC", "C19702", "basic, X5R 10V"),
    ("10u", C8): ("CL21A106KAYNNNE", "C15850", "basic, X5R 25V (5 V rails)"),
    # ---- semiconductors ------------------------------------------------------
    ("RP2354B", "QFN-80-1EP_10x10mm_P0.4mm_EP3.4x3.4mm"): ("RP2354B", "C39843328", "extended; RP2350B + 2 MB flash in package; JLC stock 759 (LCSC 0)"),
    ("74LVC245APW", "TSSOP-20_4.4x6.5mm_P0.65mm"): ("74LVC245APW,118", "C6082", "extended, Nexperia"),
    ("74LVC1G332", "SOT-363_SC-70-6"): ("74LVC1G332GW,125", "C548342", "extended, Nexperia 3-input OR"),
    ("AP2112K-3.3", "SOT-23-5"): ("AP2112K-3.3TRG1", "C51118", "extended, Diodes Inc"),
    ("USBLC6-2SC6", "SOT-23-6"): ("USBLC6-2SC6", "C7519", "extended, ST"),
    ("B5819W", "D_SOD-123"): ("B5819W SL", "C8598", "basic, JSCJ 1 A 40 V; JLC stock 375,932"),
    ("red", "LED_0603_1608Metric"): ("KT-0603R", "C2286", "basic, KENTO"),
    ("white", "LED_0603_1608Metric"): ("KT-0603W", "C2290", "basic, KENTO (the only other basic 0603 colour); JLC stock 2,668,168"),
    ("3.3uH", "L_Abracon_AOTA-B201610"): ("AOTA-B201610S3R3-101-T", "C42411119", "extended, Abracon polarised inductor from the RP2350 design guide; dot = pad 2 = DVDD; JLC stock 1,000"),
    ("12MHz", "Crystal_SMD_3225-4Pin_3.2x2.5mm"): ("X322512MOB4SI", "C70565", "extended, YXC 12 MHz CL 12 pF (18 pF loads); JLC stock 45,476"),
    # ---- electromechanical ---------------------------------------------------
    ("USB-C", "USB_C_Receptacle_HRO_TYPE-C-31-M-12"): ("HX TYPE-C 16PIN", "C5178539", "extended, hanxia; same land pattern as HRO TYPE-C-31-M-12 (4 shell legs 8.65 mm apart, pegs 5.78 mm): check against the drawing; JLC stock 41,128"),
    # 5.1 x 5.1 mm, 5.0 mm stem (shell top surface is 4.5 mm above the board). Same family: 4.5 mm C692462, 6.0 mm C318890
    ("MENU", "SW_Push_1P1T_XKB_TS-1187A"): ("TS-1187A-C-J-B", "C318885", "extended, XKB 5.1x5.1 mm, 5.0 mm stem, 2.6 N; JLC stock 3,622"),
    ("BOOTSEL", "SW_Push_1P1T_XKB_TS-1187A"): ("TS-1187A-C-J-B", "C318885", "extended, XKB 5.1x5.1 mm, 5.0 mm stem, 2.6 N; JLC stock 3,622"),
}


def lookup(value, footprint):
    """Return (MPN, LCSC, note) or None."""
    fp = footprint.split(":")[-1] if footprint else ""
    return PARTS.get((value, fp))
