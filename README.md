# Braun shaver charger PCB: PSoC 4125 SWD reverse engineering

Notes, diagrams and a bit-bang sketch for reading memory from the PSoC 4125 on a shaver charger PCB over SWD, using an Arduino Nano clone as the debug host.

Status: untested against hardware. Treat everything here as a starting point for review, not a verified design.

## Repository contents

| Path | Content |
|------|---------|
| [nano_swd_dump/nano_swd_dump.ino](nano_swd_dump/nano_swd_dump.ino) | Arduino sketch that bit-bangs SWD and dumps SRAM over the serial port |
| [docs/psoc4125-pinout-diagram.svg](docs/psoc4125-pinout-diagram.svg) | 40-pin QFN pinout, colour-coded, with the wiring table |
| [docs/nano-to-psoc-wiring.svg](docs/nano-to-psoc-wiring.svg) | Wiring schematic including the level-dropping dividers |
| [docs/breadboard-pcb-nano-wiring.png](docs/breadboard-pcb-nano-wiring.png) | Photo of the shaver PCB beside the Nano on a breadboard |
| [docs/psoc4125-chip-marking-closeup.png](docs/psoc4125-chip-marking-closeup.png) | Close-up of the package marking |
| [docs/nano-clone-underside.png](docs/nano-clone-underside.png) | Underside of the Nano clone |

## Target

- Chip marking: `CY8C4125LQI-S425`, read from the close-up photo (2219 A 03, CYP616078). The suffix has not been confirmed against the datasheet.
- Family: Infineon/Cypress PSoC 4100S, Arm Cortex-M0+
- Package: 40-pin QFN, 6x6x0.6 mm
- Context: shaver charger PCB, 5 V DC input via the included charger
- Datasheet: Infineon 002-10662 (PSoC 4100S datasheet)

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

Top view, pin 1 at the marked corner, numbering counterclockwise. See [the pinout diagram](docs/psoc4125-pinout-diagram.svg).

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

Verify this table against the current English datasheet for the exact ordering code before relying on it. The side of the package on which each pin number falls is assumed from the standard QFN layout. The adjacent VCCD, VDDD, VDDA and VSSA readings above are consistent with it.

## Goal

Read SRAM (and flash, if unprotected) over SWD using a microcontroller as the debug host, with the board powered from its own 5 V charger input.

## Debug host: Arduino Nano clone

The board in use is a generic Nano V3-style clone: ATmega328P (TQFP-32), USB-C connector, a probable CH340G-style USB-serial chip and an AMS1117-style regulator. No maker marking is visible. See the [breadboard photo](docs/breadboard-pcb-nano-wiring.png) and the [underside photo](docs/nano-clone-underside.png). The chip markings in the underside photo are too small to read, so the USB-serial chip and regulator remain unconfirmed.

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

SRAM base is assumed at 0x20000000 and size at 4 KB for the "4125" part number (per the ordering table). Not verified against the actual chip.

## Option B: Arduino Nano bit-bang (ATmega328P, 5 V logic, 3.3 V target)

See the [wiring schematic](docs/nano-to-psoc-wiring.svg) and the [pinout diagram](docs/psoc4125-pinout-diagram.svg).

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

The sketch is [nano_swd_dump/nano_swd_dump.ino](nano_swd_dump/nano_swd_dump.ino). It performs a line reset, reads DPIDR, powers up the debug domain and dumps 4 KB of SRAM from 0x20000000 as `address value` lines at 115200 baud.

Reviewed behaviour:

- Clock edges and sampling points follow the usual bit-bang SWD convention.
- The pipelined AP read sequence in `dump_mem` is correct: one discarded posted read, then DRW reads, then RDBUFF for the final word.
- `CSW_WORD_INC` (0x23000012) should be checked against the ADIv5 specification.

## Known risks / open items

- **Missing PSoC 4 acquire step.** The sketch sends a line reset and reads DPIDR, but it does not perform the PSoC 4 acquire. That procedure writes the TEST_MODE register (recalled as 0x40030014, bit 31) inside the short window after XRES release, to halt the CPU before the firmware runs. Without it, the firmware may reconfigure the SWD pins or enter Deep Sleep and the link will time out. Confirm the register address and timing window against the PSoC 4 Programming Specification before adding it.
- The ordering suffix (S425) is read from the photo, not confirmed against the datasheet. SRAM size and feature set are assumed from the "4125" row of the ordering table.
- DP IDR for a Cortex-M0+ SW-DP is typically `0x0BC11477`. Anything other than `0x00000000` or `0xFFFFFFFF` indicates the link is up.
- If `link_up()` times out, the firmware may have remapped the SWD pins (Smart I/O / HSIOM), or the part is asleep when probed. The XRES pulse in `setup()` attempts to catch the part at boot, following the same idea as the PSoC 4 "acquire" procedure.
- Flash protection, if set, blocks reads at the hardware level. No bypass is known or provided.
- The pad-to-pin identification of the supply rails rests on voltage readings and visual inspection. Confirm with continuity that the 1.8 V pad is the VCCD pin.
- Code is untested against actual hardware.
