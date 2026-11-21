; 2068 PicoCart cart menu: a machine-code AROS cart the firmware serves when MENU is held
; as the 2068 powers up.
;
;   sjasmplus --raw=menu.bin menu.asm
;
; Memory: the menu runs from dock chunk 4 ($8000). Dock chunk 5 holds the mailbox at $BFFF.
; Everything else is HOME: the 2068 ROM supplies the font at $3D00, and HOME RAM at $F000+
; holds the switch stub, the variables and the stack.
;
; The firmware fills the directory table at $9000 before serving the menu:
;   $9000      number of carts N (0-100)
;   $9001      the current cart, 1-N (0 = none)
;   $9010+32*i  name of cart i+1, NUL-terminated (up to 31 characters)
; List entry 0 is "no cartridge"; entries 1-N are the carts.
;
; To switch, the menu copies a stub to HOME RAM and jumps to it. The stub writes the entry
; number to $BFFF (the firmware sees that dock write), waits about a second while the
; firmware swaps the image, pages all of HOME back in and jumps to $0000. The 2068 then
; starts up again and reads the new cart's header.

            ORG $8000

DIR         EQU $9000
DIR_NAMES   EQU $9010
MAILBOX     EQU $BFFF
FONT        EQU $3D00          ; HOME ROM character set, from the space character
STUB_RAM    EQU $F000
VARS        EQU $F100
SEL         EQU VARS           ; selected list entry
TOP         EQU VARS+1         ; first entry shown
COUNT       EQU VARS+2         ; entries in the list: N + 1
DRAWN       EQU VARS+3         ; TOP when the names were last drawn ($FF: never)
REDRAW      EQU VARS+4         ; nonzero: draw() rewrites the names, not just the bar
REPEAT      EQU VARS+5         ; key-repeat countdown
ROWS        EQU 20             ; list rows on screen (2-21)
ATTR_TEXT   EQU $38            ; black on white
ATTR_BAR    EQU $4F            ; bright white on blue
ATTR_TITLE  EQU $47            ; bright white on black

; ---- AROS header ----
            DB 2               ; machine code
            DB 2               ; AROS
            DW start
            DB $CF             ; chunk map, bit set = HOME: chunks 4 and 5 from the dock
            DB 1               ; autostart
            DB 0, 0

start:      DI
            LD SP, $FF00
            LD A, $30          ; HSR: chunks 4 and 5 from the dock
            OUT ($F4), A
            XOR A              ; dock, not EXROM; normal screen
            OUT ($FF), A
            LD A, 7
            OUT ($FE), A       ; white border

            ; list size and starting selection
            LD A, (DIR)
            CP 101
            JR C, .n_ok
            LD A, 100
.n_ok:      LD B, A
            INC A
            LD (COUNT), A
            LD A, (DIR+1)
            CP B
            JR C, .cur_ok
            JR Z, .cur_ok
            XOR A
.cur_ok:    OR A
            JR NZ, .have_sel
            LD A, B            ; no current cart: first cart, or "no cartridge" if none
            OR A
            JR Z, .have_sel
            LD A, 1
.have_sel:  LD (SEL), A
            XOR A
            LD (TOP), A
            DEC A
            LD (DRAWN), A
            CALL scroll

            CALL cls
            LD BC, $0000
            LD HL, title
            CALL print_row
            LD A, ATTR_TITLE
            LD B, 0
            CALL attr_row
            LD BC, $1700
            LD HL, help
            CALL print_row

            CALL wait_release

            CALL draw
.key:       CALL read_keys     ; wait for a key
            OR A
            JR Z, .key
            LD B, 40           ; first repeat after ~0.3 s
.act:       PUSH AF
            LD A, B
            LD (REPEAT), A
            POP AF
            CP 3
            JR Z, choose
            LD HL, SEL
            CP 1
            JR NZ, .down
            LD A, (HL)         ; up
            OR A
            JR Z, .hold
            DEC (HL)
            JR .moved
.down:      LD A, (COUNT)
            DEC A
            CP (HL)
            JR Z, .hold
            INC (HL)
.moved:     CALL scroll
            CALL draw
.hold:      LD BC, 1000        ; poll every ~7 ms while the key is held
            CALL delay
            CALL read_keys
            OR A
            JR NZ, .held
            LD BC, 1000        ; released, or a bounce? look again
            CALL delay
            CALL read_keys
            OR A
            JR Z, .key
.held:      LD HL, REPEAT
            DEC (HL)
            JR NZ, .hold
            LD B, 10           ; then repeat every ~70 ms
            JR .act

; ---- switch to the selected entry ----
choose:     LD BC, $1600
            LD HL, starting
            CALL print_row
            LD HL, stub
            LD DE, STUB_RAM
            LD BC, stub_end - stub
            LDIR
            LD A, (SEL)
            JP STUB_RAM

stub:       LD (MAILBOX), A    ; position-independent: runs from HOME RAM
            LD D, 2
.wait:      DEC BC
            LD A, B
            OR C
            JR NZ, .wait
            DEC D
            JR NZ, .wait
            XOR A
            OUT ($F4), A       ; all HOME again, as after a reset
            OUT ($FF), A
            JP 0
stub_end:

; ---- list ----

; keep the selection on screen
scroll:     LD HL, TOP
            LD A, (SEL)
            CP (HL)
            JR NC, .not_above
            LD (HL), A
            RET
.not_above: SUB ROWS - 1
            RET C
            CP (HL)
            RET C
            RET Z
            LD (HL), A
            RET

draw:       LD A, (TOP)        ; rewrite the names only when the list has scrolled
            LD HL, DRAWN
            SUB (HL)
            LD (REDRAW), A
            LD A, (TOP)
            LD (HL), A
            LD C, 0            ; C = screen row in the list
.row:       LD A, (TOP)
            ADD A, C
            LD E, A            ; E = entry
            PUSH BC
            PUSH DE
            LD A, (COUNT)
            CP E
            JR Z, .blank
            JR C, .blank
            LD A, E
            OR A
            JR NZ, .cart
            LD HL, no_cart
            JR .print
.cart:      DEC A              ; HL = DIR_NAMES + (entry - 1) * 32
            LD L, A
            LD H, 0
            ADD HL, HL
            ADD HL, HL
            ADD HL, HL
            ADD HL, HL
            ADD HL, HL
            LD DE, DIR_NAMES
            ADD HL, DE
            JR .print
.blank:     LD HL, empty
.print:     POP DE
            POP BC
            PUSH BC
            PUSH DE
            LD A, (REDRAW)
            OR A
            JR Z, .no_text
            LD A, C
            ADD A, 2
            LD B, A
            LD C, 0
            CALL print_row
.no_text:   POP DE
            POP BC
            PUSH BC
            LD A, C
            ADD A, 2
            LD B, A
            LD A, (SEL)
            CP E
            LD A, ATTR_BAR
            JR Z, .attr
            LD A, ATTR_TEXT
.attr:      CALL attr_row
            POP BC
            INC C
            LD A, C
            CP ROWS
            JR NZ, .row
            RET

; ---- screen ----

cls:        LD HL, $4000
            LD DE, $4001
            LD BC, $17FF
            LD (HL), 0
            LDIR
            LD HL, $5800
            LD DE, $5801
            LD BC, $02FF
            LD (HL), ATTR_TEXT
            LDIR
            RET

; A = attribute, B = row
attr_row:   LD L, B
            LD H, 0
            ADD HL, HL
            ADD HL, HL
            ADD HL, HL
            ADD HL, HL
            ADD HL, HL
            LD DE, $5800
            ADD HL, DE
            LD B, 32
.fill:      LD (HL), A
            INC HL
            DJNZ .fill
            RET

; print a whole row: a space, then the NUL-terminated string at HL padded with spaces.
; B = row (0-23); C is ignored. Long names are cut at 30 characters.
print_row:  LD C, 0
            LD A, ' '
            CALL put_char
            LD C, 1
.char:      LD A, (HL)
            OR A
            JR Z, .pad
            INC HL
            CALL put_char
            INC C
            LD A, C
            CP 31
            JR NZ, .char
.pad:       LD A, C
            CP 32
            RET Z
            LD A, ' '
            CALL put_char
            INC C
            JR .pad

; A = character, B = row, C = column; keeps BC and HL
put_char:   PUSH HL
            PUSH BC
            CP ' '
            JR C, .bad
            CP 128
            JR C, .ok
.bad:       LD A, '?'
.ok:        SUB ' '            ; DE = FONT + (A - 32) * 8
            LD L, A
            LD H, 0
            ADD HL, HL
            ADD HL, HL
            ADD HL, HL
            LD DE, FONT
            ADD HL, DE
            EX DE, HL
            LD A, B            ; HL = $4000 | (row & $18) << 8 | (row & 7) << 5 | column
            AND $18
            OR $40
            LD H, A
            LD A, B
            AND 7
            RRCA
            RRCA
            RRCA
            OR C
            LD L, A
            LD B, 8
.line:      LD A, (DE)
            LD (HL), A
            INC DE
            INC H
            DJNZ .line
            POP BC
            POP HL
            RET

; ---- keyboard ----

; returns A: 0 none, 1 up (Q, 7), 2 down (A, 6), 3 select (ENTER, 0)
read_keys:  LD A, $FB          ; Q W E R T
            IN A, ($FE)
            BIT 0, A
            LD B, 1
            JR Z, .got
            LD A, $FD          ; A S D F G
            IN A, ($FE)
            BIT 0, A
            LD B, 2
            JR Z, .got
            LD A, $BF          ; ENTER L K J H
            IN A, ($FE)
            BIT 0, A
            LD B, 3
            JR Z, .got
            LD A, $EF          ; 0 9 8 7 6
            IN A, ($FE)
            LD B, 3
            BIT 0, A
            JR Z, .got
            LD B, 1
            BIT 3, A
            JR Z, .got
            LD B, 2
            BIT 4, A
            JR Z, .got
            XOR A
            RET
.got:       LD A, B
            RET

wait_release:
            XOR A              ; all rows
            IN A, ($FE)
            CPL
            AND $1F
            JR NZ, wait_release
            LD BC, 3000        ; and let the contacts settle
            ; fall through

; BC = count, ~11 us each
delay:      DEC BC
            LD A, B
            OR C
            JR NZ, delay
            RET

; ---- text ----
title:      DB "2068 PicoCart", 0
help:       DB "Q/A or arrows, ENTER to start", 0
starting:   DB "Starting...", 0
no_cart:    DB "(no cartridge: BASIC)", 0
empty:      DB 0

            ASSERT $ <= DIR
