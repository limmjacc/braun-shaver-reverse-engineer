# Project log: 2026-10-27

Exhaustive record of hardware, wiring, firmware, code, diagnostics and results from this
session's work on the Braun shaver charger PCB / PSoC 4125 SWD investigation. Organized by
topic, roughly chronological within each. Cross-references to files are relative to the repo
root.

## 1. Hardware identification

### 1.1 Target chip

- Package marking (read from close-up photo, [docs/photos/psoc4125-chip-marking-closeup.png](photos/psoc4125-chip-marking-closeup.png)): `CY8C4125` / `LQI-S425` / `2219 A 03` / `CYP616078` / `PHI` / `300` / `DC` (approx layout).
- Family: Infineon/Cypress PSoC 4100S, Arm Cortex-M0+.
- Package: 40-pin QFN, 6x6x0.6 mm (JEDEC MO-248, Infineon drawing 001-80659).
- The exact ordering suffix `S425` does not appear in the datasheet's ordering table (Table 39). The 4125 QFN codes that table does list are S412, S413, S422, S423, S432 and S433. Either the photo's last glyph is misread, or this is a customer-specific/non-catalog code. Not resolved this session.
- 4125-family features per Table 39: 24 MHz CPU, 32 KB flash, 4 KB SRAM.
- Silicon ID actually read from the chip via SROM call (see §4.6): Hi byte = `0x1B`, Lo byte = `0x05`, Revision = `0x11`, Family ID = `0xAB`. Not yet cross-referenced cleanly against the datasheet's Silicon ID table (Table 1-1 of the programming spec) -- that table's text extraction was too garbled by column-merging to align reliably. This is an open item.

### 1.2 Pin table and orientation audit

The original pin table (from before this session) was audited line-by-line against the datasheet:

- Infineon PSoC 4100S datasheet, doc **002-00122 Rev. *Q, 2024-02-27**. Downloaded to [docs/datasheets/psoc4100s-datasheet.pdf](datasheets/psoc4100s-datasheet.pdf).
- All 40 pins matched Table 1 (pin list, 40L QFN column, datasheet pp. 16-17) exactly.
- Pin 1 position and numbering direction matched Figure 10 (package outline, p. 52): pin 1 dot at top-left in top view, pins 1-10 down the left side, 11-20 along the bottom, 21-30 up the right side, 31-40 along the top (right to left).
- SWDIO = pin 12 = P3.2, alternate function `cpuss.swd_data` (Table 2, Alternate pin functions, p. 18).
- SWDCLK = pin 13 = P3.3, alternate function `cpuss.swd_clk` (same table).
- XRES = pin 30.
- VCCD = pin 31 (internal regulator output, 1.8 V +/-5%, datasheet p. 16: "Regulated digital supply").
- VDDD = pin 32, VDDA = pin 33, VSSA (ground) = pin 34.
- Exposed pad: per datasheet note on Figure 10, should be connected to VSS (ground) or left electrically floating, never any other signal.

### 1.3 Measured supply rails

Measured on the shaver board, powered from its own 5 V charger, chip unpowered by any external source:

| Pin | Name | Measured |
|-----|------|----------|
| 31  | VCCD | 1.8 V (never to be driven) |
| 32  | VDDD | 3.3 V |
| 33  | VDDA | 3.3 V |
| 34  | VSSA | GND |

Conclusion: the board runs 3.3 V logic, not 5 V as first assumed. This is consistent with (and was cross-checked against) the pin audit above -- the 1.8/3.3/3.3/GND sequence on four adjacent pins matches VCCD/VDDD/VDDA/VSSA exactly in that order, supporting the pin-table orientation independent of the datasheet check.

### 1.4 Debug host: Arduino Nano clone

Identified from two photos ([docs/photos/breadboard-pcb-nano-wiring.png](photos/breadboard-pcb-nano-wiring.png), [docs/photos/nano-clone-underside.png](photos/nano-clone-underside.png)):

- Generic Nano V3-style clone.
- MCU: ATmega328P, TQFP-32 package.
- USB-C connector (not mini/micro USB, not the original Nano's mini-B).
- Probable CH340G-family USB-serial chip: inferred from a 16-pin SOIC package near the USB end on the underside photo. Package markings too small to read at the photo's resolution; not confirmed.
- Probable AMS1117-style 5 V linear regulator: inferred from an SOT-223 package near the power pins on the underside. Also unconfirmed by readable marking.
- No maker/brand marking visible anywhere on the board.
- Bootloader: the "new" (optiboot-style) bootloader FQBN (`arduino:avr:nano`) failed to upload ("programmer is not responding", "Warning: attempt N of 10: not in sync: resp=0x00"). The old-bootloader FQBN **`arduino:avr:nano:cpu=atmega328old`** uploads successfully and was used for every sketch this session.
- Serial port on the development Mac: **`/dev/cu.usbserial-110`**.
- Toolchain: `arduino-cli`, with `arduino:avr` core version 1.8.8 installed this session (`arduino-cli core install arduino:avr`).

## 2. Wiring

Final wiring (3.3 V target, 5 V Nano), per [docs/diagrams/nano-to-psoc-wiring.svg](diagrams/nano-to-psoc-wiring.svg) and [docs/diagrams/psoc4125-pinout-diagram.svg](diagrams/psoc4125-pinout-diagram.svg):

| Nano pin | Connection | Chip pin |
|----------|------------|----------|
| D2 (PD2) | 1 kohm in series, then 2 kohm from the chip-side node to GND | 13, SWDCLK |
| D3 (PD3) | 1 kohm in series, then 2 kohm from the chip-side node to GND | 12, SWDIO |
| D4 (PD4) | direct, no divider | 30, XRES |
| GND      | direct | board GND |
| 5V       | not connected | (none) |

Divider arithmetic: with the Nano driving 5 V, the chip-side node sees `5 V x 2k / (1k + 2k) = 3.33 V`, under the chip's absolute maximum GPIO input of `VDD + 0.5 V = 3.8 V`. Measured on the bench (multimeter) at the chip-side pads during a dedicated toggle test: 0 V to 3.4 V, consistent with this calculation (observed slightly above the nominal 3.33 V, consistent with USB 5 V rail commonly sitting a little above 5.00 V, e.g. 5.1 V).

SWDIO is bidirectional: when the chip drives it high (3.3 V) during a read, that level reaches the Nano's D3 pin through the 1k leg of the same divider and registers as a valid logic high at 5 V (ATmega328P `V_IH` threshold is about 3.0 V at `V_CC` = 5 V).

A software swap test (D2<->D3 roles swapped in firmware, same physical wiring) was performed to rule out a SWDIO/SWDCLK mixup; both arrangements produced identical results (`ack=0` on every probe at that stage of the investigation), ruling out a swap as the cause of the then-current failure.

## 3. Reference documents obtained

| File | Document | Revision |
|------|----------|----------|
| [docs/datasheets/psoc4100s-datasheet.pdf](datasheets/psoc4100s-datasheet.pdf) | Infineon PSoC 4100S datasheet | 002-00122 Rev. *Q, 2024-02-27 |
| [docs/datasheets/psoc4-programming-spec.pdf](datasheets/psoc4-programming-spec.pdf) | Infineon CY8C4xxx, CYBLxxxx Programming specification | 002-22325 Rev. *I, 2024-07-26 |

Both downloaded directly from infineon.com during this session. The programming specification is the primary source for the acquire sequence, register addresses/values, and chip-level protection behaviour used throughout §4 and §5 below; section/page numbers cited are from it unless noted otherwise.

## 4. Firmware (Arduino sketches): chronological evolution

### 4.1 Starting point (before this session)

The original `nano_swd_dump.ino` (from the project's earlier notes) used:

- A fixed software delay macro `DLY()` (4 NOPs) for the bit-bang half-period.
- `static inline` helper functions for clock/data pin toggling.
- A simple `link_up()`: line reset, DPIDR read, `ABORT` write (`0x1E`), `CTRL/STAT` write (`0x50000000`), then a 50-iteration poll of `CTRL/STAT` for `0xA0000000` (both power-up acks).
- No PSoC-specific Test Mode acquire step at all.
- `dump_mem()` using `CSW_WORD_INC = 0x23000012` (word size + auto-increment + implementation HPROT-like bits) with TAR auto-increment relied on for each 256-word (1 KB) chunk.

First hardware run of this version, captured via an ACK histogram (200 probes immediately after an XRES pulse): **200/200 probes returned `ack=0`** (no response at all) on every register, including DPIDR, which the SW-DP is expected to always answer once powered.

### 4.2 `nano_pin_test.ino` (now removed; findings preserved here)

Purpose: verify the physical SWD wiring independent of protocol correctness. Held D2 (SWCLK) and D3 (SWDIO) at fixed levels for 3 seconds at a time (`HIGH,LOW` / `LOW,HIGH` / `LOW,LOW`) while reading D4 (XRES) as an input.

Result: XRES read `1` (high) in all three phases, consistent with the chip's internal pull-up holding the line high once the Nano releases it. Multimeter readings at the chip-side divider nodes during this test: 0 V to 3.4 V, matching the divider calculation (§2). This confirmed the physical wiring was sound before any protocol debugging began.

### 4.3 Register-level diagnostics that found the real fault (content now in `nano_dp_probe.ino`, superseded)

A diagnostic sketch performed a single successful DPIDR read (after up to 60 retries of line-reset + read) and then probed DP/AP registers one at a time, printing the exact 3-bit ACK for each, with `micros()` timing instrumentation.

Key result on real hardware, at the fastest loop-based clock setting then available:

```
DPIDR ack=1 data=0BC11477
tries=14 us_since_xres=7396
us_from_dpidr_to_abort=252
ABORT_write ack=0 data=0000001E
CTRLSTAT_write ack=0 data=54000000
CTRLSTAT_read ack=0 data=00000000
SELECT_write ack=0 data=00000000
AP_CSW_write ack=0 data=00000002
AP_CSW_read_posted ack=0 data=00000000
AP_CSW_read_rdbuff ack=0 data=00000000
AP_TAR_write ack=0 data=40030014
AP_DRW_read_posted ack=0 data=00000000
AP_DRW_read_rdbuff ack=0 data=00000000
```

DPIDR correctly reads `0x0BC11477` (the expected Cortex-M0+ SW-DP ID), but it took **14 retries and 7,396 microseconds** after the XRES pulse to get that single successful read -- and every register access attempted afterward, starting with `ABORT` (a DP housekeeping register specified as always accessible, independent of power-up state), returned `ack=0` (no response whatsoever, not even a WAIT or FAULT code).

### 4.4 Root-causing the clock speed

Disassembly of the compiled sketch with `avr-objdump` (toolchain path: `/Users/liamjack/Library/Arduino15/packages/arduino/tools/avr-gcc/7.3.0-atmel3.6.1-arduino7/bin/avr-objdump`) showed that the `static inline` functions (`dly()`, `clk_hi()`, `clk_lo()`, `dio_set()`, etc.) were **not actually inlined** by avr-gcc at `-Os`: they compiled to genuine `call`/`ret` instruction pairs. This added far more cycle overhead than the pin toggle itself.

Measured real achieved bit rate with that overhead: approximately **330 kHz to 1 MHz**, computed from the timing data above (116 tries in a fixed time budget, cross-checked two ways). The datasheet's Table 3-4 (SWD interface specifications) requires SWDCLK to run at **1.5 MHz to 14 MHz**. A single line-reset-plus-one-transfer attempt measured **~528 microseconds** on its own at this speed -- already longer than the ~400 microsecond acquire window the programming spec's Figure 4-2 shows for Test Mode entry. This alone was sufficient to explain the total silence observed in §4.1: the host could not physically complete even one full acquire attempt within the window, independent of any question about the chip's protection state.

### 4.5 Fully unrolled bit-bang rewrite

Fix: eliminate the function-call overhead entirely. All fixed-length SWD transfers were rewritten as fully unrolled sequences (no loop counter, no shared branch targets) using C preprocessor macros instead of `static inline` functions:

```c
#define CLK_HI()   do { PORTD |= SWCLK_MASK; } while (0)
#define CLK_LO()   do { PORTD &= (uint8_t)~SWCLK_MASK; } while (0)
#define DIO_OUT()  do { DDRD |= SWDIO_MASK; } while (0)
#define DIO_IN()   do { DDRD &= (uint8_t)~SWDIO_MASK; PORTD &= (uint8_t)~SWDIO_MASK; } while (0)
#define DIO_SET(b) do { if (b) { PORTD |= SWDIO_MASK; } else { PORTD &= (uint8_t)~SWDIO_MASK; } } while (0)
#define DIO_GET()  ((PIND & SWDIO_MASK) ? 1u : 0u)
```

Unrolled primitives built from these macros: `swd_send8`, `swd_send32`, `swd_recv3`, `swd_recv32`, `swd_recv1`, `swd_turn`, `swd_ones32` (32 forced-high bits), `swd_jtag2swd` (the 16-bit `0xE79E` JTAG-to-SWD magic sequence, each bit hardcoded), `swd_zeros8`.

**Bug found and fixed during this rewrite**: `swd_send8()` was initially declared `static void swd_send8(uint32_t v)` even though it only ever sends 8 bits. Disassembly showed avr-gcc was re-copying the *original* 32-bit value and re-deriving each bit's shift amount from scratch inside the unrolled body on every single bit -- an accidental **O(n^2)** blowup in code size and cycle count, not merely a missed optimization (visible in the disassembly as a `movw` re-copy plus a `dec`/`brne` loop of growing length before each bit test). Changing the parameter type to `uint8_t` fixed it: the compiler then emitted a clean sequence of `sbrs` (skip-if-bit-set) instructions testing the original register directly, with no shifting or re-copying at all.

### 4.6 Finding the real acquire-window timing (`nano_fast_probe.ino`, superseded)

An isolated test, using only the fast unrolled DPIDR read (no acquire sequence beyond that), looped for a 10 ms budget after an XRES pulse. Result:

```
CORRECT ID at try 86 t=6780
CORRECT ID at try 87 t=7380
tries=116 ack1_count=2 parity_fail=0 last_ack=0 last_id=00000000
```

116 attempts fit in the 10 ms budget (~86 microseconds per attempt with the unrolled primitives, versus ~528 microseconds per attempt with the original loop-based code -- roughly a 6x speedup). Only two of those 116 attempts got any response at all (`ack1_count=2`), and both were the correct ID with correct parity, landing at **6,780 and 7,380 microseconds** after XRES release -- a single window roughly 600 microseconds wide, consistent with the datasheet's stated ~400 microsecond window width, just **occurring much later than the datasheet's generic illustrative figure implies**. Section 4.3 of the programming spec explicitly notes the boot-code duration (and therefore when the window opens) "depend[s] on the CPU clock and the size of the code" -- this firmware's actual boot evidently takes roughly 6.7-7.4 ms, not the sub-millisecond figure shown in the datasheet's illustrative diagram.

### 4.7 `nano_fast_acquire.ino` (superseded): first full acquire attempt, then the window-length bug

First version searched for only 6 ms before giving up:

```c
while ((micros() - t0) < 6000ul) { ... }
```

Result: every attempt (0 through 9) reported `"DPIDR never answered in 6 ms window"` -- the search cutoff was shorter than the real window found in §4.6 (~6.7-7.4 ms), so it was aborting just before the window opened on every single run.

Extending the search to 20 ms fixed this immediately. Result, 10/10 attempts:

```
attempt 0: tries=78 id=0BC11477 us_to_dpidr=6568 us_dpidr_to_testmode=364 testmode_write_ack=1
  TEST_MODE readback ack=1 data=80000004
  *** TEST MODE ENTERED ***
attempt 1: tries=77 id=0BC11477 us_to_dpidr=6516 us_dpidr_to_testmode=368 testmode_write_ack=1
  ... (identical pattern for attempts 2 through 9, us_to_dpidr in the 6512-6568 range)
```

This is the first point in the investigation where Test Mode entry (per programming spec section 4.3, "Step 1A -- Acquire chip after hard reset") succeeded, and it did so reliably on every attempt. `TEST_MODE` register readback consistently showed `0x80000004`: bit 31 set (Test Mode active, per spec), plus bit 2 set (meaning not identified from the documents available).

Acquire sequence implemented (register values and addresses per programming spec section 4.3 pseudocode):

1. Pulse XRES, release.
2. Loop (up to the search budget): line reset, DPIDR read (single-shot, no internal WAIT retry during this time-critical phase). Expect `0x0BC11477` (Cortex-M0+) or `0x0BB11477` (Cortex-M0).
3. Immediately on DPIDR success, in the same breath: `CTRL/STAT` write `0x54000000` (`CSYSPWRUPREQ | CDBGPWRUPREQ | CDBGRSTREQ`), `SELECT` write `0x00000000`, AP `CSW` write `0x00000002` (word size, no auto-increment), AP `TAR` write `0x40030014` (the `TEST_MODE` register address), AP `DRW` write `0x80000000` (sets the Test Mode bit).
4. Not time-critical from here: read back `TEST_MODE` to confirm bit 31 set; poll `CPUSS_SYSREQ` (`0x40100004`) for the `SROM_PRIVILEGED_BIT` (`0x10000000`) to clear; poll `CTRL/STAT` until both power-up ack bits (`0xA0000000`) are set.

### 4.8 `nano_swd_dump.ino` (current production sketch)

Combines the proven acquire sequence from §4.7 with a `dump_mem()` function for reading memory, intended to output SRAM (base `0x20000000`, 1024 words = 4 KB, per the 4125 family's datasheet entry) and flash (base `0x00000000`, 8192 words = 32 KB, same source) as line-oriented text: `BEGIN_<label>`, then `ADDR VALUE` hex pairs (one per line), then `END_<label>`, at 115200 baud.

First full run:

```
acquire ack=1
BEGIN_SRAM
ERR 4
sram dump ack=4
BEGIN_FLASH
ERR 4
flash dump ack=4
done
```

Acquire succeeded, but the dump faulted immediately (`ack=4`, the ADIv5 FAULT response code, not a protocol-level ACK code like OK/WAIT) on the very first word of both regions. This triggered the extended diagnostic sequence in §5.

The sketch now in the repo is the "clean" version: the acquire function and `dump_mem()` are unchanged and correct (per the spec and per the working Test Mode entry), but all the ad-hoc diagnostic code used to investigate the fault (§5) was moved out into a separate preserved file rather than left inline.

### 4.9 `nano_protection_probe.ino` (current, preserved diagnostic record)

A full copy of the sketch at the point where all the register-by-register, CSW-variant, and SROM-call diagnostics (§5) were added inline in `setup()`. Kept intact in the repo specifically as the historical record of exactly what was tried and in what order, rather than trimmed or summarized in code form.

## 5. Diagnosing the memory-access fault

All of the following were tried, in this order, against real hardware, after a successful acquire (Test Mode confirmed entered each time):

### 5.1 CSW variants

| CSW value | Meaning | Result |
|-----------|---------|--------|
| `0x23000012` | word size + auto-increment + HPROT-like bits (original, from before this session) | `ERR 4` on first word |
| `0x00000002` | word size only, no auto-increment (the exact value already proven to work for the acquire sequence's own AP writes) | `ERR 4` on first word, identical |
| `0x23000002` | word size + the same HPROT-like top byte as the original, but auto-increment removed | `ERR 4`, identical |
| `0x80000002` | word size + `DbgSwEnable` (CSW bit 31) | `ERR 4`, identical |

An `ABORT` write (`0x1E`, clearing sticky flags) was added before each CSW variant was tried; made no difference.

### 5.2 Confirming the fault is a genuine bus-level fault, not a protocol/state issue

Sequence and exact captured values from one diagnostic run:

```
diag CTRLSTAT ack=1 data=F4000040
diag SELECT_write ack=1
diag CSW_write ack=1
diag TAR(TEST_MODE)_write ack=1
diag DRW_read_posted ack=1
diag RDBUFF ack=1 data=80000004
diag TAR(SRAM_BASE)_write ack=1
diag SRAM_DRW_read_posted ack=1
diag SRAM_RDBUFF ack=4 data=00000000
```

`CTRL/STAT` readback `0xF4000040`: both power-up ack bits set (`0x80000000` `CSYSPWRUPACK`, `0x40000000` `CDBGPWRUPACK`), plus `CDBGRSTACK`, plus the `READOK` bit (bit 6, `0x40`), and no `STICKYERR` bit (bit 5, `0x20`) yet. The `TAR`/`DRW`-posted-read/`RDBUFF` sequence for `TEST_MODE` succeeds and correctly returns `0x80000004` again (same as the acquire-time readback), confirming the AP and the connection are both still functioning normally for that specific address immediately before the SRAM attempt.

The same sequence for `SRAM_BASE` (`0x20000000`): the `TAR` write succeeds (`ack=1`), the posted `DRW` read succeeds at the protocol level (`ack=1`, returns a stale/pipelined value as expected), but the `RDBUFF` read -- the step that actually returns the result of the real bus transaction -- comes back `ack=4` (FAULT), data `00000000`.

A `CTRL/STAT` re-read immediately after this fault showed `data=F4000020`: `READOK` (bit 6) now **cleared**, `STICKYERR` (bit 5) now **set**. This confirms the fault set a genuine sticky error flag at the Debug Port level, consistent with a real AHB bus error response (`HRESP=ERROR`), not a timing race or a protocol misstep on the host's part.

### 5.3 Flash faulted identically

The same fault pattern (`TAR` write OK, posted `DRW` read OK, `RDBUFF` FAULT) was observed for flash (`base 0x00000000`) as for SRAM, ruling out an SRAM-specific cause (e.g. a wrong SRAM base address) since two architecturally unrelated memory regions failed the same way.

### 5.4 SROM call: GET_SILICON_ID succeeded

Register addresses for the PSoC 4100S family, per the programming specification's Table 1-1 (target overview):

- `CPUSS_SYSREQ` = `0x40100004`
- `CPUSS_SYSARG` = `0x40100008`

SROM call constants (Table 4-1):

- `SROM_KEY1` = `0xB6`
- `SROM_KEY2` = `0xD3`
- `SROM_SYSREQ_BIT` = `0x80000000`
- `SROM_PRIVILEGED_BIT` = `0x10000000`
- `SROM_STATUS_SUCCEEDED` = `0xA0000000` (top nibble of the result)
- `SROM_CMD_GET_SILICON_ID` = `0x00`

Procedure (per section 4.5 pseudocode): write `Params = SROM_KEY1 | ((SROM_KEY2 + CMD) << 8)` to `CPUSS_SYSARG`, write `SROM_SYSREQ_BIT | CMD` to `CPUSS_SYSREQ`, poll `CPUSS_SYSREQ` until `(SROM_SYSREQ_BIT | SROM_PRIVILEGED_BIT) == 0`, then read `CPUSS_SYSARG` for the result.

Captured result:

```
diag4 poll_ack=1 status=600020AB
diag4 SYSARG_readback ack=1 data=A0111B05
```

Top nibble of the readback is `0xA`, matching `SROM_STATUS_SUCCEEDED`. Decoded per the spec's bit layout: Silicon ID Hi = `0x1B`, Lo = `0x05`, Revision = `0x11`. Family ID, read from the low 12 bits of the `status` value captured during polling (`0x600020AB` -> low 12 bits `0x0AB`): **`0xAB`**.

This is the single most informative result of the investigation: it proves the CPU is genuinely halted and under the debugger's control, and that the SROM subsystem is fully functional, while the direct AP memory path is not.

### 5.5 SET_IMO_48MHz: no change

Per Table 1-1, the 4100S family's "SET_IMO_48MHz required for flash operations" column reads "Yes", though a footnote (b) on the same table states this is "not required for flash READ operation" specifically. Tried anyway, in case the main system-bus clock (distinct from flash-program timing) needed it at this particular (late) point in boot:

```
diag5 poll_ack=1 status=60000015
diag5 TAR(SRAM)_write ack=1
diag5 SRAM_RDBUFF ack=4 data=00000000
```

The SROM call itself completed (poll exited normally), but the subsequent SRAM read faulted identically to every previous attempt.

### 5.6 Guessed CPUSS_PROTECTION address: also faulted

The exact address of the `CPUSS_PROTECTION` register is not given in the programming specification's text (only that it exists, referenced at line "writing to the CPUSS_PROTECTION register" in the chip-level-protection discussion). A guess was tried: `0x40100000`, adjacent to the two registers (`0x40100004`, `0x40100008`) already confirmed working, on the theory it might be the base of the same peripheral block.

```
diag6 TAR(0x40100000)_write ack=1
diag6 RDBUFF ack=4 data=00000000
```

Also faulted. This is consistent with (but does not independently confirm) the chip-level-protection theory -- it shows that essentially everything *except* the two specific SROM-protocol registers is rejected, not just general memory.

### 5.7 Conclusion

The pattern across §5.1-5.6 -- Silicon ID readable via the documented SROM mechanism, but every direct memory-mapped access (SRAM, flash, and an unrelated guessed peripheral address) hard-faulting, regardless of CSW configuration or additional SROM calls -- matches the programming specification's Appendix A description of **PROTECTED** chip-level protection, verbatim:

> "In this mode, the silicon allows limited access via DAP. Access to Flash, SRAM, and most of the registers is disabled, so SWD transactions are NACKed for master. This is true for read and write requests on the SWD bus. In this mode it is possible to read the silicon ID and move the chip back to OPEN mode."

Per the same appendix's protection-mode transition diagram, moving from PROTECTED back to OPEN is documented as erasing all user-region flash first: "PROTECTED > OPEN: Erases all User Region flash. Sets chip protection to OPEN, sets flash row-level protection to unprotected." No read-before-erase path is documented.

This was not independently confirmed by reading the actual `CPUSS_PROTECTION` register value (its address was only guessed, not found in the spec text), so it remains the best-supported inference from behavior rather than a directly-verified fact.

## 6. Software tooling built this session

### 6.1 `scripts/capture_serial.py`

Resets the Nano via a DTR pulse (macOS-specific `termios`/`fcntl` ioctl constants, `TIOCMBIS`/`TIOCMBIC` = `0x8004746C`/`0x8004746B`) and logs raw serial bytes for a given duration, without depending on `pyserial`. Usage: `python3 -I capture_serial.py <device> <seconds> <outfile>`.

### 6.2 `scripts/flash_and_capture.sh`

Compiles (`arduino-cli compile --fqbn arduino:avr:nano:cpu=atmega328old`), uploads, and captures output from `nano_swd_dump`, then invokes `parse_dump.py` on the result automatically. Default capture window is 30 seconds (raised from an initial 15 seconds during this session, after estimating that a full SRAM+flash dump -- roughly 9,216 lines of hex text -- takes on the order of 15-20 seconds to transmit at 115200 baud on its own, on top of acquire time).

### 6.3 `scripts/parse_dump.py`

Parses `BEGIN_<name>` / `ADDR VALUE` / `END_<name>` blocks from a captured log into:

- `<name>.bin`: raw binary, little-endian 32-bit words, gaps (missing addresses) filled with `0xFF`.
- `<name>.hex`: Intel HEX, with automatic Extended Linear Address (type `0x04`) records emitted whenever the upper 16 bits of the address change.

**Bug found and fixed during validation**: the first version of the Intel HEX writer truncated every address to its low 16 bits (`addr & 0xFFFF`) without ever emitting an Extended Linear Address record. This silently produced **wrong** addresses for anything outside the first 64 KB -- which includes the SRAM region (base `0x20000000`): a decoder reading that file would see the SRAM dump's bytes attached to address `0x00000000`, not `0x20000000`. Fixed by tracking the upper 16 bits of the address across records and emitting a type-`0x04` record whenever it changes.

Validated against a synthetic log (not real chip data, used only to exercise the parser): manually computed the Intel HEX checksums by hand for both an ELA record and a data record and confirmed they matched the script's output (`0xDA` and `0xF7` respectively, via the standard two's-complement-of-byte-sum algorithm), and confirmed the ELA record correctly encoded a `0x20000000` base address.

## 7. Repository structure (current)

```
README.md
.gitignore
docs/
  swd-read-methods-research.md       (prior research writeup, cross-referenced throughout this log)
  project-log-2026-10-27.md          (this file)
  airtag-method.md                   (external reference transcript, added by the user; not acted on in any code in this repo)
  datasheets/
    psoc4100s-datasheet.pdf
    psoc4-programming-spec.pdf
  diagrams/
    psoc4125-pinout-diagram.svg
    nano-to-psoc-wiring.svg
  photos/
    breadboard-pcb-nano-wiring.png
    psoc4125-chip-marking-closeup.png
    nano-clone-underside.png
nano_swd_dump/
  nano_swd_dump.ino                  (production: acquire + SRAM/flash dump)
nano_protection_probe/
  nano_protection_probe.ino          (preserved diagnostic sketch, see section 5)
scripts/
  flash_and_capture.sh
  capture_serial.py
  parse_dump.py
```

Sketch folders removed during cleanup this session (superseded; their content and findings are preserved in this log and in `docs/swd-read-methods-research.md`): `nano_pin_test`, `nano_dp_probe`, `nano_fast_acquire`, `nano_fast_probe`.

## 8. Git history (this session)

In order:

1. `f951d53` -- Add PSoC 4125 SWD notes, Nano bit-bang sketch and diagrams (prior session's work, first commit to this repo).
2. `3b2f51c` -- Audit PSoC 4100S pinout against datasheet, add level dividers, reorganize docs.
3. `8956344` -- Fix acquire timing with unrolled bit-bang; diagnose memory access as chip-protected.
4. `2d16711` -- Add airtag-method.md reference notes.
5. (pending) -- this log file.

Remote: `https://github.com/limmjacc/braun-shaver-reverse-engineer.git`, branch `main`.

## 9. Open items / unresolved

- Exact ordering suffix (`S425`) not confirmed against the datasheet's ordering table; closest listed codes are S412/S413/S422/S423/S432/S433.
- Silicon ID read from the chip (Hi `0x1B`, Family `0xAB`, Revision `0x11`) not yet cross-referenced against a cleanly-extracted copy of the datasheet's Silicon ID table -- the PDF-to-text extraction of that specific table was too garbled (merged columns) to align reliably by hand.
- USB-serial chip and regulator on the Nano clone are inferred from package shape only (16-pin SOIC, SOT-223), not from a readable part marking.
- `CSW_WORD_INC` (`0x23000012`), used only in `dump_mem()`'s bulk-transfer path and never actually exercised successfully against real memory, has not been checked bit-by-bit against the ADIv5 specification; the simpler `CSW_WORD_ONLY` (`0x00000002`) used during acquire is the only CSW value confirmed working on this hardware.
- The exact address of the `CPUSS_PROTECTION` register was never found in the programming specification text and was only guessed (`0x40100000`); that guess also faulted, which is consistent with the PROTECTED-mode theory but does not independently prove it. No direct read of the actual protection byte/register was achieved.
- SRAM base (`0x20000000`) and flash base (`0x00000000`) are standard Cortex-M0+ convention, not stated explicitly in the datasheet text for this specific part.
- The `TEST_MODE` readback value of `0x80000004` has bit 2 set in addition to the documented bit 31 (Test Mode active); the meaning of bit 2 was not identified from either document.
