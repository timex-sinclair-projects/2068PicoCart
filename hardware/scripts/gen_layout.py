#!/usr/bin/env python3
"""Build the placed (unrouted) board from the schematic netlist (after tspico-hardware v3).

Run with KiCad's bundled Python (it has the pcbnew module):
  /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3 scripts/gen_layout.py

Steps: regenerate the skeleton with gen_board.py, export the netlist with kicad-cli,
load every footprint, assign nets, place parts per P below, add the B.Cu keepout
under the RP2354B, GND (In1.Cu) and +3V3 (In2.Cu) planes, save picocart.kicad_pcb.

Frame (geometry.py): mm, y down; USB-C edge at y = 0, body to y = 35.66, the dock
tongue below it (fingers y 41.4-47.5). Shell-key notches: left x < 4.5 for
y 13.7-26.8, right x > 49.45 for y 8.4-26.4. The four tabs beside the notches take no parts
(TABS below, DRC-enforced). Rotation: degrees CCW as in KiCad.
"""
import os
import re
import subprocess
import sys

import pcbnew

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from geometry import fingers  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.dirname(HERE)
KICAD_FP = "/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints"
KICAD_CLI = "/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli"
PCB = os.path.join(HW, "picocart.kicad_pcb")
NET = "/tmp/picocart.net"
BODY_H = 35.66

# ---------------------------------------------------------------------------
# Placement: ref -> (x, y, rot). Anything not listed is parked off-board.
# ---------------------------------------------------------------------------
P = {"J1": (0, 0, 0)}           # finger pads are drawn in board coordinates

# RP2354B, rotated as tspico's U20 so its bottom pad row (pins 1-20: A4-A15, MD0-MD4) faces
# the fingers, regulator on the left (pins 61-65), crystal on the right (pins 30/31).
UX, UY = 17.5, 19.5
P["U1"] = (UX, UY, 90)


def M(dx, dy, rot=0):
    """Offset from the MCU, in tspico's frame (U20 at 62, 26)."""
    return (round(UX + dx - 62, 3), round(UY + dy - 26, 3), rot)


# core regulator, "Hardware design with RP2350" figure 4 rotated (tspico C22/C20/L1, C39/R28)
P["C31"], P["C36"], P["L1"] = M(55.25, 22.8, 90), M(54.05, 22.8, 90), M(52.25, 22.8, 270)
P["C32"], P["R12"] = M(55.1, 21.2, 180), M(55.6, 19.2, 270)
# crystal (tspico Y1/R20/C35/C36)
P["Y1"], P["R13"], P["C39"], P["C40"] = M(75.5, 26.0), M(71.7, 25.2), M(74.4, 29.3, 270), M(76.6, 22.7, 90)
# decoupling, tspico position -> our ref:
#   left 68/69 C27, 76 C28; bottom 5 C20, DVDD 10 C35, 15 C21; right 29 C23, 24 C22, DVDD 32 C34;
#   top 41 C24, 50 C25, DVDD 51 C33, 59/60 C26
P["C27"], P["C28"] = M(51.2, 26.4), M(51.2, 29.4)
P["C20"], P["C35"], P["C21"] = M(57.6, 37.0, 270), M(61.35, 37.0, 270), M(66.0, 37.0, 270)
P["C23"], P["C22"], P["C34"] = M(69.4, 30.6), M(72.6, 33.0), M(70.6, 19.8)
P["C24"], P["C25"], P["C33"], P["C26"] = M(67.4, 17.4, 90), M(64.2, 14.0), M(62.3, 18.2, 90), M(58.4, 18.5, 90)
P["C38"] = (20.2, 10.0, 0)                   # 3V3 bulk beside the top decouplers
P["R14"] = (8.6, 25.8, 0)                   # QSPI_SS -> BOOTSEL, by pin 75

# USB-C at the centre of the top (handle) edge, mouth at y = 0
P["J2"] = (27.2, 3.65, 180)
P["R6"], P["R7"] = (20.3, 2.6, 90), (18.5, 2.6, 90)          # CC1/CC2 Rd
P["U5"] = (29.5, 11.0, 0)
P["R8"], P["R9"] = (33.7, 10.2, 0), (33.7, 12.0, 0)           # 33R series D+/D-
P["C12"] = (35.6, 15.4, 90)

# controls, top left: MENU and BOOTSEL (5 mm stems) side by side along the handle edge, the two
# LEDs in a column to their right
P["SW1"], P["SW2"] = (7.5, 4.0, 90), (13.3, 4.0, 90)
P["D3"], P["D4"] = (17.0, 2.4, 90), (17.0, 5.6, 90)
P["R11"], P["R15"] = (14.2, 9.4, 0), (17.2, 9.4, 0)

# test pads, top right: SWCLK SWDIO RUN 3V3 GND TX RX
for i, r in enumerate(["TP1", "TP2", "TP3", "TP4", "TP5", "TP6", "TP7"]):
    P[r] = (36.0 + 2.0 * i, 2.2, 0)

# power column, x 35-48.5 between the crystal and the right notch
P["D2"] = (38.6, 9.4, 0)                    # VBUS -> +5V
P["C5"], P["C6"] = (42.2, 9.4, 90), (44.4, 9.4, 90)
P["C7"], P["U4"], P["C8"], P["C9"] = (37.6, 13.6, 90), (40.4, 13.6, 0), (43.25, 13.6, 90), (44.95, 13.6, 90)
P["R4"], P["R5"], P["C11"] = (37.2, 18.0, 0), (40.2, 18.0, 0), (43.2, 18.0, 0)
P["R2"], P["R3"], P["C10"] = (37.2, 20.6, 0), (40.2, 20.6, 0), (43.2, 20.6, 0)

# dock side: 74LVC245 over the data fingers (x 28.5-43.8), A side (pins 2-9) facing them
P["U2"], P["C3"] = (36.5, 30.6, 90), (34.4, 25.6, 0)
P["U3"], P["C4"], P["R1"] = (42.6, 30.6, 90), (42.4, 33.6, 0), (45.4, 30.6, 90)
P["D1"] = (48.0, 29.2, 90)                  # DOCK_5V -> +5V, above finger 2
P["C1"], P["C2"] = (48.0, 33.6, 90), (45.6, 27.0, 90)
P["R10"] = (45.5, 33.8, 0)                  # BE -> BE_GPIO (DNP)

# the tabs beside the shell notches (x0, y0, x1, y1): no footprints
TABS = {"tab_TL": (0, 1.55, 4.5, 13.7), "tab_BL": (0, 26.77, 4.5, 35.66),
        "tab_TR": (49.46, 1.55, 54.52, 8.37), "tab_BR": (49.46, 26.37, 54.52, 35.66)}

# ---------------------------------------------------------------------------


def sexpr(text):
    tokens = re.findall(r'\(|\)|"(?:[^"\\]|\\.)*"|[^\s()"]+', text)
    stack = [[]]
    for t in tokens:
        if t == '(':
            stack.append([])
        elif t == ')':
            top = stack.pop()
            stack[-1].append(top)
        else:
            stack[-1].append(t[1:-1] if t.startswith('"') else t)
    return stack[0]


def find(node, key):
    return [n for n in node if isinstance(n, list) and n and n[0] == key]


def first(node, key):
    f = find(node, key)
    return f[0] if f else None


def load_netlist(path):
    root = sexpr(open(path).read())[0]
    comps = {}
    for c in find(first(root, "components"), "comp"):
        ref = first(c, "ref")[1]
        dnp = any(len(pr) > 1 and isinstance(pr[1], list) and pr[1][:2] == ["name", "dnp"] for pr in find(c, "property"))
        props = {pr[1][1]: pr[2][1] for pr in find(c, "property") if len(pr) > 2 and isinstance(pr[2], list) and pr[2][0] == "value"}
        comps[ref] = {"value": first(c, "value")[1], "footprint": (first(c, "footprint") or ["", ""])[1], "dnp": dnp, "props": props}
    nets = []
    for n in find(first(root, "nets"), "net"):
        name = first(n, "name")[1]
        nodes = [(first(nd, "ref")[1], first(nd, "pin")[1]) for nd in find(n, "node")]
        nets.append((name, nodes))
    return comps, nets


def load_footprint(fpid):
    lib, name = fpid.split(":")
    path = os.path.join(HW, "lib", lib + ".pretty") if lib == "picocart" else os.path.join(KICAD_FP, lib + ".pretty")
    fp = pcbnew.FootprintLoad(path, name)
    if fp is None:
        raise SystemExit(f"footprint not found: {fpid}")
    fp.SetFPID(pcbnew.LIB_ID(lib, name))
    return fp


def mm(x, y):
    return pcbnew.VECTOR2I(pcbnew.FromMM(x), pcbnew.FromMM(y))


def rule_area(board, name, layer, pts):
    ko = pcbnew.ZONE(board)
    ko.SetIsRuleArea(True)
    ko.SetDoNotAllowTracks(True)
    ko.SetDoNotAllowVias(True)
    ko.SetDoNotAllowPads(False)
    ko.SetDoNotAllowFootprints(False)
    ko.SetDoNotAllowZoneFills(False)
    ko.SetLayer(layer)
    ko.SetZoneName(name)
    o = ko.Outline()
    o.NewOutline()
    for x, y in pts:
        o.Append(pcbnew.FromMM(x), pcbnew.FromMM(y))
    board.Add(ko)


def no_parts_area(board, name, x0, y0, x1, y1):
    ko = pcbnew.ZONE(board)
    ko.SetIsRuleArea(True)
    ko.SetDoNotAllowTracks(False)
    ko.SetDoNotAllowVias(False)
    ko.SetDoNotAllowPads(False)
    ko.SetDoNotAllowFootprints(True)
    ko.SetDoNotAllowZoneFills(False)
    ls = pcbnew.LSET(); ls.AddLayer(pcbnew.F_Cu); ls.AddLayer(pcbnew.B_Cu)
    ko.SetLayerSet(ls)
    ko.SetZoneName(name)
    o = ko.Outline()
    o.NewOutline()
    for x, y in ((x0, y0), (x1, y0), (x1, y1), (x0, y1)):
        o.Append(pcbnew.FromMM(x), pcbnew.FromMM(y))
    board.Add(ko)


def main():
    subprocess.run(["python3", os.path.join(HERE, "gen_board.py")], check=True)
    subprocess.run([KICAD_CLI, "sch", "export", "netlist", "--format", "kicadsexpr", "--output", NET,
                    os.path.join(HW, "picocart.kicad_sch")], check=True, capture_output=True)
    comps, nets = load_netlist(NET)
    board = pcbnew.LoadBoard(PCB)

    netinfo = {}
    for name, _ in nets:
        ni = pcbnew.NETINFO_ITEM(board, name)
        board.Add(ni)
        netinfo[name] = ni

    fps = {fp.GetReference(): fp for fp in board.GetFootprints()}
    for i, (ref, c) in enumerate(sorted(comps.items())):
        if ref.startswith("#") or ref in fps:
            continue
        fp = load_footprint(c["footprint"])
        fp.SetReference(ref)
        fp.SetValue(c["value"])
        for k in ("MPN", "LCSC"):
            if k in c["props"]:
                try:
                    fp.SetField(k, c["props"][k])
                    fp.GetField(k).SetVisible(False)
                except Exception:
                    pass
        if c.get("dnp"):
            fp.SetDNP(True)
        if ref in P:
            x, y, rot = P[ref]
        else:
            x, y, rot = 60.0, 2 + (i % 20) * 2.5, 0
            print("unplaced (parked):", ref)
        fp.SetPosition(mm(x, y))
        fp.SetOrientationDegrees(rot)
        board.Add(fp)
        fps[ref] = fp

    pad_index = {}
    for ref, fp in fps.items():
        for pad in fp.Pads():
            pad_index.setdefault((ref, pad.GetNumber()), []).append(pad)
    missing = 0
    for name, nodes in nets:
        for ref, pin in nodes:
            pads = pad_index.get((ref, pin))
            if not pads:
                missing += 1
                print("no pad for", ref, pin)
                continue
            for pad in pads:
                pad.SetNet(netinfo[name])
    print("nets assigned; missing pads:", missing)

    # no tracks or vias on B.Cu under the RP2354B body, around a 4 x 4 mm window for the thermal-pad vias
    for name, (x0, y0, x1, y1) in {"L": (-5.3, -5.3, -2.0, 5.3), "R": (2.0, -5.3, 5.3, 5.3),
                                   "T": (-2.0, -5.3, 2.0, -2.0), "B": (-2.0, 2.0, 2.0, 5.3)}.items():
        rule_area(board, "U1_keepout_B_" + name, pcbnew.B_Cu,
                  [(UX + x0, UY + y0), (UX + x1, UY + y0), (UX + x1, UY + y1), (UX + x0, UY + y1)])
    # fiducials: the router does not see their mask opening
    for fp in board.GetFootprints():
        if fp.GetReference().startswith("FID"):
            cx, cy = pcbnew.ToMM(fp.GetPosition().x), pcbnew.ToMM(fp.GetPosition().y)
            rule_area(board, fp.GetReference() + "_keepout", pcbnew.F_Cu,
                      [(cx - 1.3, cy - 1.3), (cx + 1.3, cy - 1.3), (cx + 1.3, cy + 1.3), (cx - 1.3, cy + 1.3)])

    for name, box in TABS.items():
        no_parts_area(board, name, *box)

    # dock tongue: no tracks or vias below the finger tips or in the gaps between fingers
    # (the dock contacts wipe that area and the bevel cuts it); entry is from the top of each finger
    cols = sorted({(round(cx - w / 2, 3), round(cx + w / 2, 3)) for _, _, cx, _, w, _ in fingers()})
    strips = [(a[1], b[0]) for a, b in zip(cols, cols[1:])]
    for layer, ln in ((pcbnew.F_Cu, "F"), (pcbnew.B_Cu, "B")):
        rule_area(board, "tongue_tips_" + ln, layer, [(2.5, 47.6), (52.0, 47.6), (52.0, 49.5), (2.5, 49.5)])
        for k, (x0, x1) in enumerate(strips):
            if x1 - x0 > 0.3:
                rule_area(board, f"finger_gap_{ln}{k}", layer, [(x0 + 0.13, 41.2), (x1 - 0.13, 41.2), (x1 - 0.13, 49.5), (x0 + 0.13, 49.5)])
        rule_area(board, "tongue_left_" + ln, layer, [(2.5, 41.2), (cols[0][0] - 0.13, 41.2), (cols[0][0] - 0.13, 49.5), (2.5, 49.5)])
        rule_area(board, "tongue_right_" + ln, layer, [(cols[-1][1] + 0.13, 41.2), (52.0, 41.2), (52.0, 49.5), (cols[-1][1] + 0.13, 49.5)])

    # planes: the fill is clipped to the board outline (0.3 mm edge clearance)
    for layer, netname in ((pcbnew.In1_Cu, "GND"), (pcbnew.In2_Cu, "+3V3")):
        z = pcbnew.ZONE(board)
        z.SetLayer(layer)
        z.SetNet(netinfo[netname])
        z.SetZoneName(netname + "_plane")
        o = z.Outline()
        o.NewOutline()
        ybot = 49.03 if netname == "GND" else BODY_H        # +3V3 stays off the tongue
        for x, y in ((0, 0), (54.52, 0), (54.52, ybot), (0, ybot)):
            o.Append(pcbnew.FromMM(x), pcbnew.FromMM(y))
        z.SetLocalClearance(pcbnew.FromMM(0.25))
        z.SetMinThickness(pcbnew.FromMM(0.25))
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
        board.Add(z)
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())

    pcbnew.SaveBoard(PCB, board)
    print("saved", PCB, "footprints:", len(board.GetFootprints()), "nets:", board.GetNetCount())


if __name__ == "__main__":
    main()
