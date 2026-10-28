# SWD read methods: research log and conclusion

This documents every method attempted to read SRAM/flash from the CY8C4125 over its two SWD
pins (SWDIO, SWDCLK) using only the Arduino Nano clone as the debug host, why each one was
tried, what the hardware actually did, and the conclusion. Primary sources:

- Infineon "PSoC 4100S" datasheet, doc 002-00122 Rev. *Q, 2024-02-27. Local copy:
  [docs/datasheets/psoc4100s-datasheet.pdf](datasheets/psoc4100s-datasheet.pdf).
- Infineon "CY8C4xxx, CYBLxxxx Programming specification", doc 002-22325 Rev. *I, 2024-07-26.
  Local copy: [docs/datasheets/psoc4-programming-spec.pdf](datasheets/psoc4-programming-spec.pdf).
  This is the authoritative source for the acquire sequence, register addresses, and chip-level
  protection behaviour; all section/page references below are to it unless stated otherwise.

Fault injection / voltage glitching to bypass read protection was out of scope and not
attempted. Everything below is normal SWD protocol usage.

## 1. Pinout and wiring audit

Before any protocol work, the pin table in the repo's original notes was checked against the
datasheet's Table 1 (pin list, 40L QFN column) and Figure 10 (package outline, 001-80659). All
40 pins matched, including SWDIO at pin 12 (P3.2, alternate function `cpuss.swd_data`), SWDCLK
at pin 13 (P3.3, `cpuss.swd_clk`), and XRES at pin 30. Pin 1 orientation (top-left, dot marker,
counterclockwise numbering) matched the photographed package marking and the measured supply
rails (VCCD 1.8 V at pin 31, VDDD/VDDA 3.3 V at pins 32/33, VSSA ground at pin 34). This ruled
out a wiring or pin-identity error early, before any protocol-level debugging.

Supply measurement showed the board runs at 3.3 V, not 5 V as first assumed, which drove the
1k/2k level-dropping dividers on SWDCLK/SWDIO documented in the main README.

## 2. First attempt: loop-based bit-bang, line reset + DPIDR only

The original sketch used a 4-NOP software delay per half-clock and a `while(n--)` loop per
transfer. Every attempt returned `ack=0` (no response at all) on every register, including
`DPIDR`, which the SW-DP always answers once the port is live. Measuring with an ACK histogram
(200 probes after reset) showed 100% `ack=0`.

A register-level probe (`nano_protection_probe.ino`'s immediate ancestor) found that after
enough retries, `DPIDR` eventually *did* come back correctly (`0x0BC11477`, the expected
Cortex-M0+ SW-DP ID), but every subsequent register access -- including `ABORT`, a DP
housekeeping register specified as always available -- came back `ack=0`. Timing
instrumentation (`micros()`) showed this took **14 tries and 7,396 µs** from XRES release, and
that even a single line-reset-plus-read attempt measured **~528 µs** on its own, at the fastest
delay setting then in use.

## 3. The clock was far under spec

The datasheet's Table 3-4 states SWDCLK must run at **1.5-14 MHz**. Disassembling the compiled
sketch (`avr-objdump`) showed the `dly()` function and the `clk_*()`/`dio_*()` helpers, all
declared `static inline`, were **not actually inlined** by avr-gcc at `-Os` -- they compiled to
real `call`/`ret` pairs, adding far more overhead than the toggle itself. Real measured bit rate
with that overhead: **~330 kHz to ~1 MHz**, 1.5x to 4.5x under the spec floor. A single line
reset plus one transfer (~528 µs, measured) already exceeded the ~400 µs acquire window the
datasheet's Figure 4-2 shows for Test Mode entry. The bit-bang could not physically land inside
that window even once, independent of any protection question -- which explained observation 2
above without needing to invoke chip protection at all.

## 4. Fully unrolled bit-bang

Fix: eliminate all function-call overhead. Every fixed-length SWD transfer (8-bit request,
32-bit data, 3-bit ACK, the line-reset pattern, the JTAG-to-SWD magic sequence) was unrolled
into straight-line macro invocations with no loop counter and no shared branch targets, using
`#define CLK_HI()/CLK_LO()/DIO_SET()/...` macros instead of `static inline` functions. One
correctness bug surfaced during this: `swd_send8()` was initially declared to take a `uint32_t`
parameter even though it only ever sends 8 bits; disassembly showed avr-gcc was re-copying the
original 32-bit value and re-deriving each bit's shift count from scratch on every call inside
the unrolled body -- an accidental O(n²) blowup, not just a missed optimization. Changing the
parameter to `uint8_t` fixed it; the compiler then emitted a sequence of direct `sbrs`
bit-tests against the original register with no shifting at all.

An isolated DPIDR-only test (fastest clock, no retry limit) found the chip's **real** acquire
window opens around **6.7-7.4 ms after XRES release**, not the ~400 µs the datasheet's generic
timing figure implies. That bound is explicitly dependent on this firmware's actual boot code
size and clock configuration (section 4.3: "The duration of the internal reset (<1 ms) and boot
code (<4 ms) are not specified exactly because they depend on the CPU clock and the size of the
code"); this firmware's real boot evidently runs longer than the datasheet's illustrative
figure. Extending the search window to cover this closed the gap: acquiring Test Mode
(`TEST_MODE` register write of `0x80000000` to `0x40030014`, per section 4.3's Step 1A) then
succeeded **reliably, on every attempt (10/10)**, with the readback confirming bit 31 set.

This is the key result: the chip is not broadly debug-disabled. The original "silent chip"
symptom was fully explained by the host being too slow, not by anything on the target side.

## 5. Memory access faults: chip-level protection

With Test Mode reliably entered, the next step -- reading SRAM via the AP (`TAR`/`DRW`/RDBUFF,
the standard ADIv5 sequence, same technique section 4.3's own flowchart uses) -- hard-faulted
(`ACK=4`, FAULT, not WAIT) on the very first word, and identically for flash. Register-level
diagnostics (preserved as
[nano_protection_probe/nano_protection_probe.ino](../nano_protection_probe/nano_protection_probe.ino))
ruled out every protocol-level explanation that was checked:

- **CSW configuration**: tried plain word-size (`0x00000002`, the value proven to work for the
  acquire sequence's own AP writes), word-size with the implementation HPROT bits from the
  original notes (`0x23000002`), and with `DbgSwEnable` set (`0x80000002`). All three faulted
  identically.
- **Power-up acknowledgment**: `CTRL/STAT` readback confirmed `CSYSPWRUPACK`/`CDBGPWRUPACK`
  both set (`0xF4000040`) before the SRAM attempt, so the debug power domain was genuinely up.
- **State drift**: a known-good register (`TEST_MODE`, which had worked throughout) was
  re-read immediately before the SRAM attempt in the same run and still succeeded, ruling out
  "something degraded since acquire" as the cause.
- **SROM-mediated access**: the spec's `GET_SILICON_ID` SROM call (section 4.5, via
  `CPUSS_SYSREQ`/`CPUSS_SYSARG` at `0x40100004`/`0x40100008` for the 4100S family, Table 1-1)
  **succeeded**, returning `status=0xA0111B05` (top nibble `0xA` = `SROM_STATUS_SUCCEEDED`):
  Silicon ID Hi=`0x1B`, Lo=`0x05`, Revision=`0x11`, Family=`0xAB`. This proves the CPU is
  genuinely under debugger control and the SROM subsystem is fully functional -- the chip is
  not "locked" in any broad sense.
- **`SET_IMO_48MHz`**: tried via SROM in case the main system bus clock specifically needed it
  (the footnote that it's "not required for flash read operation" might not hold for a capture
  point this late in boot). Made no difference.
- **An unrelated guessed address** (`0x40100000`, adjacent to the two registers that do work,
  on the theory it might be `CPUSS_PROTECTION`) also hard-faulted.

The pattern -- Silicon ID readable via SROM, essentially everything else (SRAM, flash, and even
an unrelated peripheral-range address) rejected -- is precisely what Appendix A of the
programming specification describes for **PROTECTED** chip-level protection:

> "In this mode, the silicon allows limited access via DAP. Access to Flash, SRAM, and most of
> the registers is disabled, so SWD transactions are NACKed for master. This is true for read
> and write requests on the SWD bus. In this mode it is possible to read the silicon ID and move
> the chip back to OPEN mode."

This is now the best-supported explanation, though it was inferred from behaviour rather than
from a direct read of the `CPUSS_PROTECTION` register (its exact address is not given in the
programming specification text; a guess at a plausible address also faulted, which is
consistent with but does not independently confirm the theory).

## Conclusion and what it means for reading the firmware

- The debug host (Arduino Nano bit-bang) is no longer the limiting factor. A fully unrolled,
  loop-free implementation reliably enters Test Mode and can perform standard ADIv5 memory
  reads whenever the target permits them -- `dump_mem()` in
  [nano_swd_dump.ino](../nano_swd_dump/nano_swd_dump.ino) is ready to use immediately if that
  ever changes.
- The chip most likely has chip-level protection set to PROTECTED, which Infineon's own
  specification documents as deliberately blocking flash/SRAM reads over SWD while still
  allowing the device to be identified and moved back to OPEN mode.
- Per Appendix A's transition diagram, moving from PROTECTED back to OPEN is documented as
  **erasing all user flash first** ("PROTECTED > OPEN: Erases all User Region flash. Sets chip
  protection to OPEN..."). There is no documented path to read the existing firmware out before
  that erase. This is the mechanism Infineon designed specifically to prevent exactly what this
  investigation was trying to do, and no further SWD-protocol technique was found or attempted
  to get around it.
- Fault injection (voltage glitching, clock glitching, or similar) was not investigated and is
  out of scope for this repository.

Remaining non-destructive options are outside the scope of the two SWD pins: contacting Braun
or Infineon, or treating the device as a black box and working with its external interfaces
(charging contacts, sensors) rather than extracting the firmware image.
