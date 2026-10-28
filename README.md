# Braun shaver charger PCB: PSoC 4125 SWD reverse engineering

Notes, diagrams and an Arduino Nano bit-bang tool for reading memory from the PSoC 4125 on a shaver charger PCB over SWD.

Status: the Nano reliably enters the chip's Test Mode and can perform standard SWD memory reads. Those reads currently fault, in a pattern that matches Infineon's documented PROTECTED chip-level protection -- see [Status: memory access blocked](#status-memory-access-blocked) below and [docs/swd-read-methods-research.md](docs/swd-read-methods-research.md) for the full investigation. Wiring and pinout are verified against the datasheet; the acquire sequence is verified working against the real chip.

## Repository contents

| Path | Content |
|------|---------|
| [nano_swd_dump/nano_swd_dump.ino](nano_swd_dump/nano_swd_dump.ino) | Arduino sketch: acquires the chip (enters Test Mode) and dumps SRAM + flash over serial |
| [nano_protection_probe/nano_protection_probe.ino](nano_protection_probe/nano_protection_probe.ino) | Preserved diagnostic sketch that isolated the memory-access fault (see research doc) |
| [scripts/flash_and_capture.sh](scripts/flash_and_capture.sh), [scripts/capture_serial.py](scripts/capture_serial.py) | Compile, upload and capture serial output from the Nano, macOS, old-bootloader clones |
| [scripts/parse_dump.py](scripts/parse_dump.py) | Turns a captured `BEGIN_x`/`END_x` log into `.bin` and Intel HEX files, run automatically by `flash_and_capture.sh` |
| [docs/swd-read-methods-research.md](docs/swd-read-methods-research.md) | Full research log: every method tried, why, what happened, and the conclusion |
| [docs/datasheets/psoc4100s-datasheet.pdf](docs/datasheets/psoc4100s-datasheet.pdf) | Infineon PSoC 4100S datasheet (002-00122) |
| [docs/datasheets/psoc4-programming-spec.pdf](docs/datasheets/psoc4-programming-spec.pdf) | Infineon CY8C4xxx/CYBLxxxx programming specification (002-22325) -- acquire sequence, register map, chip-level protection |
| [docs/diagrams/psoc4125-pinout-diagram.svg](docs/diagrams/psoc4125-pinout-diagram.svg) | 40-pin QFN pinout, colour-coded, with the wiring table |
| [docs/diagrams/nano-to-psoc-wiring.svg](docs/diagrams/nano-to-psoc-wiring.svg) | Wiring schematic including the level-dropping dividers |
| [docs/photos/breadboard-pcb-nano-wiring.png](docs/photos/breadboard-pcb-nano-wiring.png) | Photo of the shaver PCB beside the Nano on a breadboard |
| [docs/photos/psoc4125-chip-marking-closeup.png](docs/photos/psoc4125-chip-marking-closeup.png) | Close-up of the package marking |
| [docs/photos/nano-clone-underside.png](docs/photos/nano-clone-underside.png) | Underside of the Nano clone |

## Target

- Chip marking: `CY8C4125LQI-S425`, read from the close-up photo (2219 A 03, CYP616078). The suffix is uncertain: the datasheet ordering table (Table 39) lists no S425. The 4125 QFN codes it does list are S412, S413, S422, S423, S432 and S433. The last glyph may be misread, or this may be a customer-specific code. A sharper photo would settle it.
- Family: Infineon/Cypress PSoC 4100S, Arm Cortex-M0+
- Package: 40-pin QFN, 6x6x0.6 mm
- Context: shaver charger PCB, 5 V DC input via the included charger
- Datasheet: Infineon 002-00122 Rev. *Q, 2024-02-27 (PSoC 4100S), stored at [docs/datasheets/psoc4100s-datasheet.pdf](docs/datasheets/psoc4100s-datasheet.pdf). The earlier reference to 002-10662 was incorrect.
- 4125 family features per Table 39: 24 MHz CPU, 32 KB flash, 4 KB SRAM

### Measured supply rails

Measured on the pads next to the supply pins and checked visually:

| Pin | Name | Measured |
|-----|------|----------|
| 31 | VCCD | 1.8 V (internal regulator output, never drive) |
| 32 | VDDD | 3.3 V |
| 33 | VDDA | 3.3 V |
| 34 | VSSA | GND |

The board therefore runs at 3.3 V logic. This drives the wiring choices below.

### Pin table, 40-pin QFN

Top view, pin 1 at the marked corner, numbering counterclockwise. See [the pinout diagram](docs/diagrams/psoc4125-pinout-diagram.svg).

| Pin | Name  | Note |
|-----|-------|------|
| 1   | P2.0  | GPIO |
| 2   | P2.1  | GPIO |
| 3   | P2.2  | GPIO |
| 4   | P2.3  | GPIO |
| 5   | P2.4  | GPIO |
| 6   | P2.5  | GPIO |
| 7   | P2.6  | GPIO |
| 8   | P2.7  | GPIO |
| 9   | VSSD  | ground, digital |
| 10  | P3.0  | GPIO |
| 11  | P3.1  | GPIO |
| 12  | P3.2  | **SWDIO** |
| 13  | P3.3  | **SWDCLK** |
| 14  | P3.4  | GPIO |
| 15  | P3.5  | GPIO |
| 16  | P3.6  | GPIO |
| 17  | P3.7  | GPIO |
| 18  | P4.0  | GPIO |
| 19  | P4.1  | GPIO |
| 20  | P4.2  | GPIO |
| 21  | P4.3  | GPIO |
| 22  | P0.0  | GPIO |
| 23  | P0.1  | GPIO |
| 24  | P0.2  | GPIO |
| 25  | P0.3  | GPIO |
| 26  | P0.4  | GPIO |
| 27  | P0.5  | GPIO |
| 28  | P0.6  | GPIO |
| 29  | P0.7  | GPIO |
| 30  | XRES  | **reset, active low, internal pull-up** |
| 31  | VCCD  | internal regulator output (1.8 V measured), bypass cap only, do not drive |
| 32  | VDDD  | digital supply (3.3 V measured) |
| 33  | VDDA  | analog supply (3.3 V measured) |
| 34  | VSSA  | ground, analog |
| 35  | P1.0  | GPIO |
| 36  | P1.1  | GPIO |
| 37  | P1.2  | GPIO |
| 38  | P1.3  | GPIO |
| 39  | P1.4  | GPIO |
| 40  | P1.7  | GPIO, also VREF |

Exposed pad: ground.

Audited against the datasheet: all 40 entries match the 40L QFN column of Table 1 (pp. 16 to 17). The pin 1 position and numbering direction match Figure 10 (package outline 001-80659, p. 52): pin 1 dot at the top-left of the top view, pins 1 to 10 down the left side, 11 to 20 along the bottom, 21 to 30 up the right side and 31 to 40 along the top from right to left. SWDIO and SWDCLK are the alternate functions `cpuss.swd_data` on P3.2 and `cpuss.swd_clk` on P3.3 (Table 2). The datasheet states that the exposed pad should be connected to VSS, or left floating. The measured rails are consistent with this table.

## Goal

Read SRAM (and flash, if unprotected) over SWD using a microcontroller as the debug host, with the board powered from its own 5 V charger input.

## Debug host: Arduino Nano clone

The board in use is a generic Nano V3-style clone: ATmega328P (TQFP-32), USB-C connector, a probable CH340G-style USB-serial chip and an AMS1117-style regulator. No maker marking is visible. See the [breadboard photo](docs/photos/breadboard-pcb-nano-wiring.png) and the [underside photo](docs/photos/nano-clone-underside.png). The chip markings in the underside photo are too small to read, so the USB-serial chip and regulator remain unconfirmed.

Many clones ship with the old bootloader. If an upload fails with a sync error, select the old bootloader (`arduino:avr:nano:cpu=atmega328old`).

## Safety notes

- Confirmed: the charger is a low voltage DC adapter, not mains-referenced. Safe to connect a debug host ground to this board under that condition.
- Do not tie the debug host supply rail to the board VDD. Share ground only.
- The board runs at 3.3 V. The PSoC absolute maximum on a GPIO is VDD + 0.5 V, which is 3.8 V. The Nano drives 5 V, so SWDCLK and SWDIO must pass through the dividers described below. Never connect a Nano output directly to a PSoC pin.
- VCCD (pin 31, 1.8 V) must never be driven.
- SWD does not function while the part is in Deep Sleep.
- If the firmware set a flash protection level, memory reads are blocked at the hardware level. No known bypass.

## Option A: CMSIS-DAP probe + OpenOCD

Flash a Raspberry Pi Pico with the `debugprobe` firmware to get a CMSIS-DAP probe. The Pico uses 3.3 V logic, which matches the board, so no dividers are needed.

Wiring:
- Probe SWCLK -> chip pin 13
- Probe SWDIO -> chip pin 12
- Probe GND -> board GND
- Probe GPIO (reset) -> chip pin 30 (XRES)
- Do NOT connect probe 3V3 to the board

```
openocd -f interface/cmsis-dap.cfg -f target/psoc4.cfg
```

Then via telnet (port 4444):

```
halt
dump_image sram.bin 0x20000000 0x1000
```

SRAM size is 4 KB for the 4125 family (Table 39). The base address of 0x20000000 is standard for Cortex-M0+ and is not stated in the datasheet text.

## Option B: Arduino Nano bit-bang (ATmega328P, 5 V logic, 3.3 V target)

See the [wiring schematic](docs/diagrams/nano-to-psoc-wiring.svg) and the [pinout diagram](docs/diagrams/psoc4125-pinout-diagram.svg).

| Nano pin | Connection | Chip pin |
|----------|------------|----------|
| D2 (PD2) | 1 kohm in series, 2 kohm from the chip side to GND | 13, SWDCLK |
| D3 (PD3) | 1 kohm in series, 2 kohm from the chip side to GND | 12, SWDIO |
| D4 (PD4) | direct | 30, XRES |
| GND | direct | board GND |
| 5V | NOT connected | none |

Divider reasoning:

- With the Nano driving 5 V, the chip pin sees 5 V x 2k / (1k + 2k) = 3.33 V, below the 3.8 V limit. Verify the actual resistor values before powering.
- SWDIO is bidirectional. The Nano pin sits on the 1k side of the divider, so a 3.3 V high from the chip reaches the Nano as a valid logic high (the ATmega328P input threshold at 5 V is about 3 V). The 2k load of about 1.6 mA is within the PSoC output drive capability. Confirm that SWDIO reads cleanly on the bench.
- XRES connects directly. The sketch only pulls the line low and then releases it to high-impedance, and the internal pull-up to 3.3 V completes the release. A 3.3 V level is a valid input for the Nano.
- Divider edge rates are not a concern at the bit-bang clock rate.

The sketch is [nano_swd_dump/nano_swd_dump.ino](nano_swd_dump/nano_swd_dump.ino). On every run it:

1. Pulses XRES and races the chip's post-reset acquire window with a fully unrolled (loop-free) bit-bang, fast enough to clear the datasheet's 1.5-14 MHz SWDCLK floor (Table 3-4 of the programming spec). This reliably enters Test Mode (confirmed 10/10 on the bench) -- see [docs/swd-read-methods-research.md](docs/swd-read-methods-research.md) for why the original loop-based bit-bang never could, and how this was found and fixed.
2. Polls `SROM_PRIVILEGED_BIT` and the `CTRL/STAT` power-up acknowledgment, per the programming spec's Step 1A (section 4.3).
3. Dumps SRAM (1024 words from 0x20000000) then flash (8192 words from 0x00000000) via the standard ADIv5 AP sequence (TAR write, posted DRW read, RDBUFF read), printing `BEGIN_SRAM` / `address value` lines / `END_SRAM`, then the same for flash, at 115200 baud.

### Status: memory access blocked

Step 3 currently fails immediately (`ERR 4`, an ADIv5 FAULT response) on the first word of both regions. This was diagnosed in detail -- see [docs/swd-read-methods-research.md](docs/swd-read-methods-research.md) for the full log, including every CSW configuration and SROM call tried. In short: the chip's Silicon ID reads fine via an SROM call (`status=0xA0111B05`, Family ID `0xAB`), proving the CPU is genuinely under debugger control, but every direct memory access -- SRAM, flash, even an unrelated guessed peripheral address -- hard-faults. This matches Infineon's documented **PROTECTED** chip-level protection (programming spec, Appendix A) exactly: Silicon ID readable, flash/SRAM/most registers NACKed. Moving back to OPEN mode is documented as erasing all flash first, so there is no non-destructive path to the existing firmware found or available via these two pins.

The diagnostic sequence that established this is preserved as [nano_protection_probe/nano_protection_probe.ino](nano_protection_probe/nano_protection_probe.ino). `dump_mem()` in the main sketch is left as-is: it is the correct, spec-compliant way to read memory and needs no changes if the chip is ever in OPEN mode.

## Known risks / open items

- **Memory access protected.** See "Status: memory access blocked" above and the research doc. This is the main open item; everything below is secondary.
- The ordering suffix (S425) is read from the photo and does not appear in the datasheet ordering table. The feature set is therefore assumed from the 4125 family rows. The Silicon ID actually read from the chip (Hi=`0x1B`, Family=`0xAB`, Revision=`0x11`) has not yet been cross-referenced against a clean copy of the datasheet's Silicon ID table (Table 1-1 of the programming spec); the extracted text was too garbled to align columns reliably.
- SWD clock limits (Table 30, SID213 and SID214 of the datasheet): 14 MHz at 3.3 V to 5.5 V, 7 MHz at 1.71 V to 3.3 V, and at most one third of the CPU clock. The current sketch's unrolled bit-bang has not been precisely measured against this ceiling (only confirmed to be fast enough in practice); see the research doc for the measured floor it had to clear.
- DP IDR for a Cortex-M0+ SW-DP is `0x0BC11477`, confirmed read from the actual chip.
- The pad-to-pin identification of the supply rails rests on voltage readings and visual inspection. Confirm with continuity that the 1.8 V pad is the VCCD pin.
- `CSW_WORD_INC` (0x23000012, used only if memory access is ever unblocked) should be checked against the ADIv5 specification; a simpler proven-working value (`CSW_WORD_ONLY`, 0x00000002) is used internally during acquire.
