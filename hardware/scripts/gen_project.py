#!/usr/bin/env python3
"""One-time project scaffolding for the 2068 PicoCart (safe to rerun):

  lib/picocart.kicad_sym                      TS2068_Cart36 symbol (dock edge, 36 pins)
  lib/picocart.pretty/TS2068_Cart36.kicad_mod the fingers, from geometry.py
  picocart.kicad_pro, sym-lib-table, fp-lib-table
  picocart.kicad_sch                          root sheet with the hierarchical sheets

Root and sheet UUIDs are kept when the files already exist, so the sheet
generators' instance paths stay valid.
"""
import json
import os
import re
import uuid

from geometry import PIN_NAMES, REV, fingers

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.dirname(HERE)
NAME = "picocart"
SHEETS = [("dock.kicad_sch", "Dock Interface"), ("mcu.kicad_sch", "RP2354B Core"), ("power.kicad_sch", "Power, USB, Controls")]
TSPICO_PRO = os.path.expanduser("~/Documents/github/tspico-hardware/hardware/tspico-v3.kicad_pro")


def u():
    return str(uuid.uuid4())


def symbol_lib():
    pins = []
    for n in range(1, 37):
        left = n % 2 == 1                      # odd pins (bottom fingers) on the left
        row = (n - 1) // 2
        y = 22.86 - row * 2.54
        x, ang = (-10.16, 0) if left else (10.16, 180)
        etype = "power_in" if n in (2, 36) else "passive"
        pins.append(f'      (pin {etype} line (at {x} {y:.2f} {ang}) (length 2.54) (name "{PIN_NAMES[n]}" (effects (font (size 1.27 1.27)))) (number "{n}" (effects (font (size 1.27 1.27)))))')
    return f'''(kicad_symbol_lib (version 20241209) (generator "gen_project.py") (generator_version "1")
  (symbol "TS2068_Cart36" (pin_names (offset 1.016)) (exclude_from_sim no) (in_bom no) (on_board yes)
    (property "Reference" "J" (at 0 25.4 0) (effects (font (size 1.27 1.27))))
    (property "Value" "TS2068_Cart36" (at 0 -24.13 0) (effects (font (size 1.27 1.27))))
    (property "Footprint" "picocart:TS2068_Cart36" (at 0 0 0) (effects (font (size 1.27 1.27)) (hide yes)))
    (property "Datasheet" "" (at 0 0 0) (effects (font (size 1.27 1.27)) (hide yes)))
    (property "Description" "TS2068 dock (cartridge) edge fingers, 2x18 at 0.1 in; odd pins bottom side, even pins top side" (at 0 0 0) (effects (font (size 1.27 1.27)) (hide yes)))
    (symbol "TS2068_Cart36_0_1"
      (rectangle (start -7.62 24.13) (end 7.62 -22.86) (stroke (width 0.254) (type default)) (fill (type background)))
    )
    (symbol "TS2068_Cart36_1_1"
{chr(10).join(pins)}
    )
  )
)
'''


def footprint():
    L = ['(footprint "TS2068_Cart36"',
         '  (version 20241229) (generator "gen_project.py") (generator_version "1")',
         '  (layer "F.Cu")',
         '  (descr "TS2068 dock edge fingers, 2x18 at 0.1 in, key slot between positions 2 and 3. Pads are in board coordinates: place at (0,0). Geometry from the EasyEDA 2068-cart-SMD board.")',
         '  (tags "edge connector TS2068 dock cartridge")',
         f'  (property "Reference" "J1" (at 27 37.5 0) (layer "F.SilkS") (hide yes) (uuid "{u()}") (effects (font (size 0.8 0.8) (thickness 0.12))))',
         f'  (property "Value" "TS2068_Cart36" (at 27 39 0) (layer "F.Fab") (uuid "{u()}") (effects (font (size 0.8 0.8) (thickness 0.12))))',
         f'  (property "Footprint" "" (at 0 0 0) (layer "F.Fab") (hide yes) (uuid "{u()}") (effects (font (size 1.27 1.27) (thickness 0.15))))',
         f'  (property "Datasheet" "" (at 0 0 0) (layer "F.Fab") (hide yes) (uuid "{u()}") (effects (font (size 1.27 1.27) (thickness 0.15))))',
         f'  (property "Description" "" (at 0 0 0) (layer "F.Fab") (hide yes) (uuid "{u()}") (effects (font (size 1.27 1.27) (thickness 0.15))))',
         '  (attr smd exclude_from_pos_files exclude_from_bom)']
    for pin, layer, cx, cy, w, h in fingers():
        mask = "F.Mask" if layer == "F.Cu" else "B.Mask"
        L.append(f'  (pad "{pin}" smd rect (at {cx} {cy}) (size {w} {h}) (layers "{layer}" "{mask}") (solder_mask_margin 0.05) (uuid "{u()}"))')
    L.append(')')
    return "\n".join(L) + "\n"


def root_sch():
    path = os.path.join(HW, NAME + ".kicad_sch")
    root_uuid, sheet_uuids = u(), {}
    if os.path.exists(path):
        txt = open(path).read()
        root_uuid = re.search(r'\(uuid "([^"]+)"\)', txt).group(1)
        for m in re.finditer(r'\(sheet .*?\(uuid "([^"]+)"\).*?"Sheetfile" "([^"]+)"', txt, re.S):
            sheet_uuids[m.group(2)] = m.group(1)
    out = ['(kicad_sch (version 20250114) (generator "gen_project.py") (generator_version "1")',
           f'  (uuid "{root_uuid}")', '  (paper "A4")',
           '  (title_block (title "2068 PicoCart") (rev "' + REV + '") (company "Timex/Sinclair Online User Group") (comment 1 "Root sheet: see ../PLAN.md and ../SCHEMATIC.md"))',
           '  (lib_symbols)']
    for i, (f, title) in enumerate(SHEETS):
        x, y = 30 + 80 * i, 40
        su = sheet_uuids.get(f, u())
        out.append(f'''  (sheet (at {x} {y}) (size 60 30) (fields_autoplaced yes) (stroke (width 0.1524) (type solid)) (fill (color 0 0 0 0.0000)) (uuid "{su}")
    (property "Sheetname" "{title}" (at {x} {y - 0.7} 0) (effects (font (size 1.27 1.27)) (justify left bottom)))
    (property "Sheetfile" "{f}" (at {x} {y + 30.6} 0) (effects (font (size 1.27 1.27)) (justify left top)))
    (instances (project "{NAME}" (path "/{root_uuid}" (page "{i + 2}"))))
  )''')
    out.append('  (sheet_instances (path "/" (page "1")))')
    out.append('  (embedded_fonts no)')
    out.append(')')
    open(path, "w").write("\n".join(out) + "\n")


def project():
    d = json.load(open(TSPICO_PRO))
    d["meta"]["filename"] = NAME + ".kicad_pro"
    d["sheets"] = []
    ns = d["net_settings"]
    ns["netclass_patterns"] = [{"netclass": "Power", "pattern": p} for p in ("DOCK_5V", "VBUS", "+5V")]
    d["boards"] = []
    txt = json.dumps(d, indent=2).replace("tspico-v3", NAME)
    open(os.path.join(HW, NAME + ".kicad_pro"), "w").write(txt + "\n")
    open(os.path.join(HW, "sym-lib-table"), "w").write(
        '(sym_lib_table\n  (version 7)\n  (lib (name "picocart")(type "KiCad")(uri "${KIPRJMOD}/lib/picocart.kicad_sym")(options "")(descr "2068 PicoCart symbols"))\n)\n')
    open(os.path.join(HW, "fp-lib-table"), "w").write(
        '(fp_lib_table\n  (version 7)\n  (lib (name "picocart")(type "KiCad")(uri "${KIPRJMOD}/lib/picocart.pretty")(options "")(descr "2068 PicoCart footprints"))\n)\n')


def main():
    os.makedirs(os.path.join(HW, "lib", "picocart.pretty"), exist_ok=True)
    open(os.path.join(HW, "lib", "picocart.kicad_sym"), "w").write(symbol_lib())
    open(os.path.join(HW, "lib", "picocart.pretty", "TS2068_Cart36.kicad_mod"), "w").write(footprint())
    # the Abracon inductor footprint was copied from tspico-hardware; rename its library prefix
    project()
    root_sch()
    print("project scaffolding written")


if __name__ == "__main__":
    main()
