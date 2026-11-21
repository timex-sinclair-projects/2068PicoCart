# 2068 PicoCart: bus behaviour

When does the cartridge drive the TS2068's buses? This answers that from the KiCad netlist (exported from `hardware/picocart.kicad_sch`), the firmware source and measurements on a rev 0.1 board. It applies to rev 0.1 and rev 0.2, which have the same bus circuit. It was written for running the cart in the DOCK slot alongside a TS-Pico on the rear expansion bus.

**Confirmed** means read in the schematic netlist or the source, or measured on hardware. **Inferred** means reasoned but not checked. Paths are relative to the repo root.

## Summary

The cartridge drives only D0–D7, and only while its 74LVC245 is enabled and pointed at the dock. That requires /ROSCS, /MREQ and /RD all low, with the firmware's BUS_OFF low. BUS_OFF goes low only after the 2068's +5 V has been steady for 10 ms. The cart returns a byte from its 64 KB image for every such read, in all eight 8 KB chunks, and $FF where it has no data.

It never drives on I/O cycles, on writes, on HOME reads, or on any read with /ROSCS or /MREQ high. The hardware releases the bus within nanoseconds of /RD, /MREQ or /ROSCS rising. The cart drives no other line: BE goes to a link that isn't fitted, and /RESET, /WAIT, /INT, /NMI, /BUSRQ and /ROMCS aren't connected at all. It doesn't snoop ports.

**Open point:** the cart doesn't look at /EXROM. If the 2068 also lowers /ROSCS for reads of its internal EXROM, the cart will answer those reads too.

## 1. Select condition

*Confirmed.* The buffer drives D0–D7 only when /ROSCS, /MREQ and /RD are all low and the firmware's BUS_OFF line is low.

| Part | Role | Connections (netlist) |
|---|---|---|
| U2, 74LVC245A at 3.3 V | data buffer | A side = D0–D7 (dock), B side = MD0–MD7 (MCU) |
| U2 pin 19, /OE | output enable | net `BUF_OE_n`, the output of U3 |
| U2 pin 1, DIR | direction | net `RD_n`: /RD low drives the dock side; otherwise it drives the MCU side |
| U3, 74LVC1G332 | 3-input OR | `BUF_OE_n = ROSCS_n OR MREQ_n OR BUS_OFF` (U3.1 = `ROSCS_n`, U3.3 = `MREQ_n`, U3.6 = `BUS_OFF`) |
| R1, 10 kΩ | pull-up | `BUS_OFF` to 3.3 V, so the bus stays off through reset, boot and crashes |

Nothing else gates the buffer: not A13–A15, /EXROM, /BE or /IORQ. Those lines go only to RP2350 inputs.

The firmware doesn't decode chunks either. It answers every /ROSCS read from a 64 KB image (`firmware/romemu/main.c:86-93`), with unused bytes set to $FF (`firmware/romemu/loader.c:105`, `main.c:128`).

So the cart behaves like a stock cartridge selected by /ROSCS and /MREQ, except that it answers every chunk, returning $FF where it has no data.

## 2. Driving outside a /ROSCS read

*Confirmed (schematic).* The cart never drives the data bus on:

- I/O reads, because /MREQ is high, so /OE is high;
- HOME reads, because /ROSCS is high;
- writes, because DIR points toward the MCU.

All the J1 data pins go only to U2.

**Caveat:** it does drive on any read where /ROSCS and /MREQ are low, whatever /EXROM is doing. See [section 11](#11-differences-from-a-stock-timex-cartridge).

## 3. Port snooping

*Confirmed.* The firmware doesn't watch I/O cycles or ports $F4 and $FF, and makes no banking decisions from them.

- `IORQ_n` (GPIO28) is read but never used (`main.c:81-104`).
- The only "banking" is which 64 KB image the firmware loads, which it chooses at each 2068 power-up (`main.c:155-170`).

The cart's on-screen menu is Z80 code that runs on the 2068, and it writes the 2068's ports itself:

- At start: HSR $F4 ← $30, $FF ← $00, $FE ← 7 (`firmware/romemu/menu/menu.asm:51,53,55`).
- When restarting into the chosen cart: $F4 ← 0, $FF ← 0, then `JP 0` (`menu.asm:158-160`).
- It reads the keyboard through port $FE (`menu.asm:339-371`).

## 4. Other bus lines

*Confirmed.*

Only three J1 lines leave the board other than D0–D7:

- `BE` (pin 33): goes only to R10, a 0 Ω link that isn't fitted, so it's open.
- `DOCK_5V`.
- GND.

Every other dock line goes only to an RP2354B GPIO input: A0–A15, /MREQ, /IORQ, /RD, /WR, /M1, /RFSH, EXROM, /ROSCS and A7R.

The firmware sets GPIO0–31 as inputs with no pulls (`main.c:194-199`). The only outputs it ever enables are MD0–MD7, which are on the MCU side of U2 (`main.c:91,93,112`). It never configures GPIO33 (BE).

/WAIT, /RESET, /INT, /NMI, /BUSRQ and /ROMCS aren't wired on this board at all.

## 5. Memory writes

*Confirmed.* The cart captures data when /ROSCS, /MREQ and /WR are low (`main.c:95-101`). It stores the byte only if that 8 KB chunk is RAM in the current image (`main.c:99`): in a .dck file, chunk type bit 0 marks RAM.

A write to $BFFF is also latched as a mailbox (`main.c:100`). The firmware acts on it only while it is serving the menu (`main.c:254`). That's how the menu passes the chosen cart to the firmware.

*Measured:* during power-up the 2068 ROM made 16,385 writes into the dock. The cart ignored them, because those chunks were ROM.

## 6. After reset

*Confirmed.* At power-up the cart serves one of three images.

1. **The remembered cart** from its USB flash drive (`main.c:215`, via `loader.c`). An LROS image fills chunk 0; an AROS image fills chunks 4 and up.
2. **The built-in fallback**, if the drive has no usable cart (`main.c:216`). The default build uses a small "Hello world" AROS BASIC cart at $8000 (`firmware/romemu/CMakeLists.txt:13`).
3. **The menu**, if MENU was held at power-up or pressed while a cart ran (`main.c:160`):
   - Chunk 4 holds the menu program. It starts at $8000 with an AROS header: machine code, chunk map $CF, autostart (`menu.asm:41-46`).
   - The cart list is at $9000, also in chunk 4.
   - Chunk 5 holds the $BFFF mailbox (`main.c:140`).

Both of the ROM's power-up probes get an answer: the LROS probe at $0000 and the AROS probe at $8000. Any chunk without data reads $FF.

*Measured:* after power-on the ROM reads $0000–$0004, then $8000–$8007.

## 7. Boot timing

*Measured* with the cart powered only by the 2068:

| Event | Time |
|---|---|
| Firmware running | 0.12 ms after the RP2350 comes out of reset |
| DOCK +5 V first above 4.4 V | 848 ms after reset (the 2068's 5 V rises slowly) |
| Bus enabled | 857 ms after reset, 10 ms after the 4.4 V point (`main.c:239-249`) |
| ROM's first dock read | 1.789 s after the 4.4 V point |

That leaves about 1.78 s between the bus being enabled and the ROM's first dock read. The ROM read came 1.789 s after the 4.4 V point with and without USB power.

/RESET isn't on this board, so the cart neither holds the 2068 in reset nor needs you to press reset. A reset without a power cycle leaves the cart serving the same image.

## 8. Read timing

*Confirmed: no PIO.* The bus is served by a polling loop on core 1. It runs from SRAM with interrupts off (`main.c:81-104`):

1. Wait until /ROSCS, /MREQ and /RD are all low.
2. Take A0–A15 from that same sample.
3. Put `image[a]` on MD0–MD7 and enable the MD outputs.
4. Hold them until /RD or /ROSCS rises, then release MD (`main.c:92-93`).

The release on the dock side doesn't depend on the firmware. U2 stops driving D0–D7 when /RD rises (DIR flips) or when /MREQ or /ROSCS rises (/OE goes high). That takes the 74LVC245's few-nanosecond propagation delay.

*Inferred, not measured on a scope:* about 50–100 ns from the last of the three falling edges to valid data on D0–D7. That's a few loop iterations at the 150 MHz default clock, plus about 5 ns through the buffer.

*Measured:* 650,000–830,000 dock reads per second, and every cart tested worked.

## 9. Levels and power

- *Confirmed:* D0–D7 go through the 74LVC245A powered at 3.3 V. Its inputs tolerate 5 V, and its 3.3 V outputs are valid TTL highs for the Z80.
- *Confirmed:* the address and control lines go straight to the RP2354B's 5 V-tolerant pins (`SCHEMATIC.md`, GPIO table), with no level shifters.
- *Confirmed:* power comes from DOCK +5 V and USB VBUS through two Schottky diodes (D1, D2) into an AP2112K 3.3 V regulator. USB can't back-feed the dock.
- *Inferred:* the cart draws well under 150 mA from the dock (the design budget in `SCHEMATIC.md`). This hasn't been measured.

## 10. HOME ROM use

*Confirmed.* The menu relies on the HOME ROM in three ways:

- It uses the HOME ROM's font at $3D00 (`menu.asm:26`).
- It reads the keyboard on port $FE.
- To restart, it sets HSR = 0 and $FF = 0, then jumps to `JP 0`, into whatever ROM is at HOME $0000 (`menu.asm:158-160`).

So a modified TS-Pico HOME ROM would be underneath the menu.

The firmware doesn't care what a cart does. LROS images replace chunk 0 through the dock; AROS carts run on top of HOME.

## 11. Differences from a stock Timex cartridge

- **No chunk decode.** The cart answers every /ROSCS read in all eight chunks, with $FF where it has no data. A stock cart answers only where its ROM is.
- **No /EXROM gating.** *Inferred:* if the 2068 lowers /ROSCS on reads of its internal EXROM, the cart drives the bus during those reads too, against the internal EXROM. Every cart tested so far ran correctly, including AROS BASIC carts, so it hasn't caused a collision in practice. This hasn't been checked directly. The check is to count dock reads while the EXROM line (GPIO31) is active, with the 2068 running BASIC.
- **RAM chunks** in the dock bank.
- **A write mailbox** at $BFFF.
- **A different image at each power-up**, from the USB drive or the on-screen menu.
- **Stays off the bus when told to:**
  - BUS_OFF stays high (bus off) until DOCK +5 V is steady.
  - After the menu's "no cartridge" entry, it stays high until the next power cycle (`main.c:172-176,246-249`).

## Files

- Schematic: `hardware/dock.kicad_sch` (J1, U2, U3, R1, R10), `hardware/power.kicad_sch`, `hardware/mcu.kicad_sch`, and the pin tables in `SCHEMATIC.md`.
- Firmware:
  - `firmware/romemu/main.c`: the bus loop (lines 81-104); power-up and BUS_OFF handling (lines 155-176 and 239-249).
  - `firmware/romemu/loader.c`.
  - `firmware/romemu/menu/menu.asm`.

Line numbers are as of commit 855ebab.
