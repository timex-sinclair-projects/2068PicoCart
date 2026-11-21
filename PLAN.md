# TS2068 RP2350 Dock Cartridge — Plan (rev 7)

Sources:
- `SCH_2068-cart-SMD` / `PCB_2068-cart-SMD` EasyEDA exports (2026-11-11).
- Photos of the current board in the shell and of both shell halves (IMG_0593/0594).

Floorplan sketch, to scale from the exported outline: [floorplan.svg](floorplan.svg)

Fixed requirements:
- Full chip-down design. The 2068's +5 V powers the cart.
- The cart emulates ROM and RAM.
- Configuration is done over USB-C.
- The PCB outline and the side notches (shell keys) stay as they are.
- Storage is on-board flash. There is no TF card.
- The top-shell holes are free to fill or redesign.

Changes since rev 6:
- **Decided:** USB-C is **JLC/LCSC C5178539** (HX TYPE-C 16PIN), a top-mount right-angle part. There's no board slot. The shell gets a local pocket over the connector.

Rev 6:
- **Decided:** horizontal USB-C on the bottom edge (the handle end), with no tongue. The shell will be adjusted to fit.

Rev 5:
- Corrected shell dimensions: 3.0 mm from the PCB to the inside of the top shell, and the outer surface 4.5 mm above the PCB (1.5 mm wall).
- No vertical USB-C fits flush at that height (§3). Options are re-ranked, and the MENU switch is now a low-profile part.

Rev 4:
- The write-protect slide is gone. Protection is a per-cart setting in `CONFIG.TXT`.

Earlier changes (rev 3):
- RP2354B with the flash inside the chip package; no external flash chip.
- Vertical USB-C through the top shell, so the handle is left alone.
- MENU button and LED instead of the rotary.

## 1. Current board (reference)

| Part | Role |
|---|---|
| J1 | 36-pin dock edge, 2×18 @ 0.1". Pin 2 = +5V, pin 36 = GND |
| U3 SST39SF010A | 128 KB flash. A0–A13 come from the dock, A14–A16 from SW1 → 8 × 16 KB slots |
| SW1 RDS4 rotary | Slot select, reached through a hole in the top shell |
| SW2 "WriteEnable" | `/WE` slide, reached through the small hole in the top shell |
| U1 74LS32 | `CE = /MREQ OR /ROSCS`. `/OE` = `/RD` |

Dock pins unused today: 1 `A14B`, 16 `A15B`, 19 `/IORQ`, 20 `A7RB`, 22 `/M1`, 32 `/RFSH`, 33 `BE`, 34 `EXROM`.

## 2. Architecture

```
 TS2068 dock ── A0–A15, ctrl (5V) ────────────────► RP2354B GPIO (5V-tolerant inputs)
            ── D0–D7 ◄──► 74LVC245 (3V3) ◄──► RP2354B GPIO
                          /OE = OR(/ROSCS, /MREQ, BUS_OFF)  74LVC1G332
                          DIR = /RD
            ── +5V ──►|──┐
 USB-C VBUS ─────────►|──┴─ 5V_INT ─ LDO 3V3 ─ RP2354B, logic
 USB-C (handle end) D+/D- ── ESD ── 27Ω ── RP2354B USB (device)
 2 MB flash (in RP2354B package): firmware + FAT volume of cart images
 MENU button, status LED ── GPIO
```

### Decisions

1. **MCU: RP2354B. It is an RP2350B (QFN-80, 48 GPIO) with 2 MB of flash stacked in the same package.** There's no external flash chip, no QSPI routing and one fewer part to place.
   - The bus needs 32 GPIO (16 address, 8 data, 8 control). That still rules out the 30-GPIO A variants.
   - **Flash budget:** firmware (Pico SDK + TinyUSB + FatFS + menu ROM) should be under 384 KB. That leaves about 1.6 MB for the drive, roughly three times the 2068's entire software library.
   - **Check before committing:** that JLC/LCSC stocks the RP2354B and can assemble it. If not, fall back to an RP2350B with a W25Q16JV (2 MB) or W25Q32JV (4 MB). They cost about the same as anything smaller, and the smallest common QSPI parts are 1–2 MB anyway.
   - Use stepping A4 or later. A2 has erratum E9, where GPIO inputs latch when the internal pull-down is on.
   - **Caveat:** the firmware runs from the same flash the drive lives on. While flash is being erased or programmed, code can't run from flash.
     - The bus engine (PIO + DMA + core 1 loop) must run entirely from SRAM, so it keeps serving the 2068 during a `.SAV` write.
     - Flash erase/program routines go in SRAM too (`__no_inline_not_in_flash_func`).
     - This is the same arrangement CircuitPython uses.
2. **The data bus is buffered and hardware decides when it's driven.**
   - `/OE` of the 74LVC245 is `/ROSCS | /MREQ | BUS_OFF`.
   - `BUS_OFF` has a pull-up, so the bus stays released until the firmware is serving.
   - The cart can only drive the 2068 bus during a dock-selected read.
   - Address and control lines go straight into the MCU, whose pins tolerate 5 V while IOVDD is powered. Leave their pad pull-ups off.
3. **Power: two Schottkys (e.g. SS14/BAT60A) diode-OR the dock +5V and USB VBUS**, feeding an AP2112K-3.3.
   - This prevents USB from back-powering the 2068, and the 2068 from back-feeding the USB host.
   - A divider from the dock +5V (taken before its diode) gives `DOCK_PRESENT`.
4. **Top-shell controls (the holes are free to redesign):**
   - **USB-C**: C5178539, horizontal top-mount at the centre of the bottom edge (§3). The old rotary hole is filled, or reused for the MENU button.
   - **MENU button:**
     - Held at power-on, it boots the on-screen menu instead of the default cart.
     - Held for 5 s with the 2068 off and USB connected, it does a factory reset of `CONFIG.TXT`.
   - **Status LED** shown by a light pipe or a pinhole, or visible inside the USB-C opening. It shows: serving, saving, USB drive mode, config error.
5. **Connect every dock signal.** That includes A14B/A15B (for 64 KB images), `/WR`, `/IORQ`, `/M1`, `/RFSH` and `EXROM`. Put `BE` on a 0 Ω/jumper to a GPIO, undriven by default.

### GPIO map (RP2354B)

| GPIO | Signal | Notes |
|---|---|---|
| 0–15 | A0–A15 | Contiguous for PIO `in pins,16` |
| 16–23 | D0–D7 (245 A-side) | Contiguous for `out pins/pindirs,8` |
| 24–31 | /ROSCS, /MREQ, /RD, /WR, /IORQ, /M1, /RFSH, EXROM | Whole bus readable with one `gpio_get_all()` |
| 32 | BUS_OFF | Pull-up; bus held off until firmware is ready |
| 33 | BE (0 Ω jumper) | Reserved |
| 34 | MENU button | Internal pull-up |
| 36 | Status LED | |
| 40 (ADC0) | DOCK_5V sense | 2068 power detect |
| 46–47 | UART TX/RX test pads | Debug |
| 35, 37–39, 41–45 | spare | |
| QSPI_SS | BOOTSEL pad | Firmware recovery. The MENU button could double as BOOTSEL through a diode |

## 3. Mechanical: USB-C on the bottom edge

**Decided:** a horizontal USB-C at the centre of the bottom edge, opposite the dock contacts. The board outline doesn't change apart from the connector cutout. The shell (handle) will be adjusted to fit.

**Connector: [C5178539, HX TYPE-C 16PIN](https://jlcpcb.com/partdetail/HX%20TYPE-C%2016PIN/C5178539).** It's a 16-pin USB 2.0, right-angle, top-mount part, and JLC can assemble it.
- **Centre height 1.68 mm**; the body is **3.16 mm** tall (datasheet drawing). It has four slotted through-hole shell legs plus two locating pegs.
- That's **0.16 mm over the 3.0 mm inside clearance.** The top shell needs a local pocket over the connector, about 10 × 8 mm and 0.5 mm deep, leaving 1.0 mm of wall.
- No board slot is needed, and nothing hangs below the board.
- Place it at the centre of the bottom edge, with its front face flush with the board edge or slightly past it, per the footprint drawing.
- Keep the ESD part, 27 Ω resistors and 5.1 kΩ CC resistors within about 5 mm of the connector.

**Needed from the shell (Joe):**
1. A **pocket in the top shell** over the connector (above).
2. An **opening in the handle band** for the plug's moulded grip.
   - Allow about 12.5 × 7 mm, centred 1.68 mm above the board top.
   - The grip reaches roughly 1.6 mm below the board bottom and about 4.9 mm above the board top, so the opening spans the joint between the two shell halves.
3. A **clear path for the plug through the handle.** The current finger hole is only ~12 mm deep, while a plug plus its strain relief is 20–30 mm.
4. **Support:** a rib in the top shell touching the connector's metal shell, and a boss under the board in the bottom shell. Insertion force then goes into the shell, not into the solder joints or the edge fingers.

**Everything else must be ≤ 3.0 mm** (with ~0.3 mm margin, aim for ≤ 2.7 mm). Checked:

| Part | Typical height | OK? |
|---|---|---|
| RP2354B QFN-80 | 0.9 mm | ✓ |
| 74LVC245 TSSOP-20, 74LVC1G332 SOT-353 | 1.2 / 1.1 mm | ✓ |
| AP2112K SOT-23-5, Schottky SOD-123F | 1.1 / 1.0 mm | ✓ |
| 12 MHz crystal 3225, 3.3 µH inductor 2016 | 0.8 / 1.0 mm | ✓ |
| Caps 0402/0603 (avoid 1206 ≥10 µF in tall cases; use 0805 10 µF) | ≤ 1.25 mm | ✓ |
| USBLC6-2SC6 SOT-23-6 | 1.1 mm | ✓ |
| **USB-C C5178539** | 3.16 mm | ✗ — top-shell pocket (§3) |
| **MENU switch** | A 6×6 tact switch is 3.1–5 mm — ✗. Use a **low-profile tact switch (≤ 2.5 mm, e.g. 3×4 mm or 4×4 mm SMT, ~1.5–2.5 mm)** plus a printed plunger in the shell | ✓ |
| LED 0603 | 0.8 mm; a pinhole or a short printed light pipe | ✓ |

**Use a 4-layer board.** QFN-80 at 0.4 mm pitch and the RP2350's built-in switching core regulator (with its 3.3 µH inductor) need a solid ground plane. Copy the layout from *Hardware design with RP2350* exactly.

USB-C basics:
- 5.1 kΩ pull-downs on CC1 and CC2,
- a USBLC6-2SC6 for ESD,
- 27 Ω series resistors on D+/D-,
- a USB 2.0-only 16-pin part (6-pin parts are power-only, with no D+/D-).

## 4. ROM and RAM emulation

The 2068 dock bank is 64 KB in 8 chunks of 8 KB, selected chunk by chunk through the HSR register (port F4). The cart holds the whole 64 KB in SRAM (520 KB available) and gives each chunk a type:

| Chunk type | Read (`/ROSCS·/MREQ·/RD`) | Write (`/ROSCS·/MREQ·/WR`) |
|---|---|---|
| ABSENT | Bus not driven | Ignored |
| ROM | Served from SRAM | Ignored (but seen by the mailbox, §5.3) |
| RAM | Served from SRAM | Stored in SRAM |

- This matches the `.DCK` chunk map directly: 0 = absent, 1 = RAM (empty), 2 = ROM + data, 3 = RAM + data. A `.DCK` file needs no extra configuration.
- Raw `.ROM`/`.BIN` files get their chunk layout from `CONFIG.TXT`.
- **`protect = yes`** on a cart in `CONFIG.TXT` makes its RAM chunks behave like ROM and stops `.SAV` writes. The menu can toggle it through the mailbox.
- **Persistent RAM, emulating battery-backed RAM carts:**
  - Flash can't be written during the power-loss window.
  - Instead, the firmware tracks dirty 4 KB sectors and writes them to `SAVES/<name>.SAV`.
  - The write happens after about 2 s with no writes, and again on a mailbox `SAVE` command.
  - The bus engine keeps running from SRAM while this happens.
- **Bus engine:** two PIO state machines plus DMA, with no CPU in the loop.
  1. **Reads:** the PIO waits for the read condition and pushes the address. A DMA channel uses it to fetch the byte into the TX FIFO, and the PIO drives D0–D7. When `/RD` rises the pins go back to inputs. ABSENT chunks are masked out.
  2. **Writes:** the address and data are captured on the rising edge of `/WR` and pushed to a FIFO. Core 1, running from SRAM, applies them to RAM chunks and feeds the mailbox.
- **Timing budget:** roughly 200 ns from `/ROSCS` to valid data. PIO+DMA typically takes 60–120 ns at 150 MHz. Measure the SCLD's `/ROSCS` delay on real hardware first.

## 5. Configuration

### 5.1 USB: shows up as a thumb drive (primary)

- Plug in USB with the cart out of the 2068 and it appears as a FAT drive called `TS2068CART` (about 1.6 MB) on Mac, PC or Linux. No app is needed.
- TinyUSB MSC serves the FAT region of the in-package flash.
- MSC only runs while `DOCK_PRESENT` is false, so the host and the cart are never both writing the FAT.
- When the drive is ejected or unplugged, the firmware re-reads the config.
- With the cart in the 2068, USB comes up as a CDC serial port only: logs and a status/command console.

Drive layout:
```
/CARTS/        *.DCK *.ROM *.BIN   (drag and drop)
/SAVES/        *.SAV               (RAM contents, written by the cart)
/CONFIG.TXT                        (plain text, see below)
/README.TXT                        (generated: firmware version, cart list, errors from the last parse)
```

`CONFIG.TXT`:
```ini
[boot]
default = ZEBRA         ; cart booted at power-on; "menu" = always show the menu
mailbox = off           ; for loaded carts: off = max fidelity, on = homebrew API

[ZEBRA]
file = CARTS/ZEBRA.DCK  ; chunk map taken from the .DCK

[MYGAME]
file = CARTS/MYGAME.ROM
load = 0x8000           ; AROS at 8000h
chunks = ROM ROM

[RAM64]
ram = 64K               ; blank 64 KB RAM cart
persist = SAVES/RAM64.SAV
protect = no            ; yes = RAM behaves as ROM, no .SAV writes
```

Carts without a section get defaults: `.DCK` files describe themselves, and `.ROM` files of 8/16 KB load as LROS at 0000h. So you only need a section to change something.

### 5.2 Web configurator (later, optional)

- A static web page in Chrome/Edge that talks to the cart over Web Serial (the CDC port).
- It handles setting the default cart, firmware info and RAM dumps, with nothing to install.
- Build this only if editing `CONFIG.TXT` by hand turns out to be a pain.

### 5.3 From the 2068: magic-address mailbox

This is how the on-screen menu works, and it gives homebrew programs an API.

- The cart snoops writes to the dock bank, including writes to ROM chunks, which are otherwise discarded.
- **Unlock sequence, to avoid false triggers:** write `0xA5` to `xFFE`, then `0x5A` to `xFFE`, then the command byte to `xFFF`. Here `x` is the top of the chunk the menu runs from.
- Arguments go in a short parameter window just below that, and status is read back from a mirror byte.
- Commands: `LIST`, `SELECT n`, `ARM` (load into a second SRAM buffer), `GO`, `SAVE`, `SETDEFAULT n` (writes `CONFIG.TXT`).
- The mailbox is always on while the menu image is active. For loaded carts it's off unless `mailbox = on`.
- I'd avoid an I/O-port mailbox. The 2068's partial port decoding (ULA on even ports; F4/F5/F6/FF taken) makes a clean free port hard to guarantee.

**Launching a selected cart without a reset line:**
1. The menu copies a small stub into home RAM and jumps to it.
2. The stub sends `GO`. The RP2354B swaps the dock image atomically.
3. The stub pages the home ROM back in through HSR and does `JP 0`.
4. The home ROM's cold start finds the new LROS/AROS header in the dock and boots it.

Check in the 2068 ROM disassembly that `JP 0` re-runs the dock check. If it doesn't, the fallback is to set the default cart and power-cycle.

**Boot:**
1. At power-on, the MCU loads the default cart into SRAM. It loads the menu instead if MENU is held or `default = menu`.
2. It releases `BUS_OFF`.

This must finish before the 2068 home ROM checks the dock. Target under 10 ms and measure it.

## 6. Phases

1. **Bench prototype:**
   - Dock-edge-to-header adapter, 74LVC245 + 1G332 on perfboard, and an RP2350B core board (e.g. WeAct).
   - Measure `/ROSCS` timing and boot-race margin on a real 2068.
   - Prove ROM serving, RAM writes, the mailbox, and serving the bus while flash writes are in progress.
2. **Shell with Joe:**
   - Handle rework for the bottom-edge USB-C opening and plug path (§3).
   - Define the USB-C pocket and handle opening, the MENU button and LED holes, and a support boss. Fill the old WP slide hole.
   - Share one DXF/STEP of the board.
3. **Schematic:** start from the RP2350 minimal design (RP2354B variant: no QSPI flash), then add bus buffering, power, USB-C, the button and the LED.
4. **Layout:** 4 layers. MCU just below the contact tab; USB-C and button near the handle end.
5. **Firmware:**
   1. Bus engine (SRAM-resident).
   2. Flash FAT + `CONFIG.TXT`.
   3. USB MSC/CDC.
   4. Menu ROM (Z80 asm) and mailbox.
   5. RAM persistence.
   6. `.DCK` tests against images from ZEsarUX.

## 7. Open questions

1. ~~RP2354B stock~~: JLC has 759 in stock (checked 2026-11-12).
2. Is the cart's top face reachable when it's docked? That decides whether USB can be used in the computer or only on the bench.
3. Can `JP 0` from home RAM re-trigger the dock boot? This needs checking against the ROM disassembly.
