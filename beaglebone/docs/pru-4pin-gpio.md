# PRU-driven 4-pin GPIO (BeagleBone Black)

This sets up PRU0 (one of the AM335x's two 200 MHz real-time coprocessors)
to drive 4 output pins with deterministic, sub-10ns-granularity timing,
each independently settable to 0 or 1 and independently readable, from
Linux userspace, without going through the kernel GPIO subsystem (which
has microsecond-scale, non-deterministic latency).

Source files: [../pru/](../pru/)
- `BB-PRU0-4PIN-00A0.dts` -- device tree overlay (pinmux + PRUSS enable)
- `pru0_4pin.asm` -- PRU0 firmware source
- `pru0_4pin.cmd` -- PRU linker command file
- `pru4pin.py` -- Linux-side control script

## Pins

| Header pin | Signal name     | PRU function         |
|------------|------------------|-----------------------|
| P9_31      | mcasp0_aclkx     | pr1_pru0_pru_r30_0    |
| P9_29      | mcasp0_fsx       | pr1_pru0_pru_r30_1    |
| P9_30      | mcasp0_axr0      | pr1_pru0_pru_r30_2    |
| P9_28      | mcasp0_ahclkr    | pr1_pru0_pru_r30_3    |

These are the standard, widely-documented BeagleBone Black PRU0 GPO pins
(the same ones used by TI's own PRU examples and the `BB-SPIDEV1` cape,
since they're the pins that carry `mcasp0`/`spi1` by default). All 4 are
driven from a single PRU core's R30 output register, so a single PRU
instruction (`MOV r30, ...`) sets all 4 bits simultaneously and
atomically -- there is no skew between pins, unlike splitting the pins
across two PRU cores or going through the regular GPIO banks.

**Trade-off:** these pins double as `mcasp0` (HDMI audio). The overlay
disables `mcasp0`, so HDMI audio output is lost. HDMI video is unaffected.

## Timing

The PRU core runs at 200 MHz; essentially every instruction (including
the single-cycle bit operations on R30) takes exactly 1 cycle = 5 ns.

- Finest edge-placement resolution: **5 ns** (every instruction boundary).
- Fastest possible on/off/on square wave on a single pin: **10 ns period
  (100 MHz)** -- one instruction to set, one to clear.
- The polling design used here (Linux writes desired state to shared RAM,
  PRU reads it, applies it to R30) adds a handful of extra cycles per
  loop iteration for the `LBBO`/`SBBO` shared-RAM accesses (tens of ns
  total per full poll cycle). This only affects how fast a *new* value
  written from Linux gets picked up -- the `MOV r30, r2` instruction that
  actually changes the pins is still a single 5 ns cycle, so pin changes
  remain fully deterministic once the PRU has the value in hand. A
  firmware variant that only needs a fixed, hardcoded pattern (no
  Linux-side control) could hit the full 5 ns/10 ns limit by skipping the
  shared-RAM round trip entirely and just looping on `SET`/`CLR r30`
  directly.

## How it works

`pru0_4pin.asm` loops forever: it reads a 4-bit "desired state" word from
a fixed offset in the PRUSS shared RAM (local PRU address `0x00010000`,
physical address `0x4a310000`, a 12KB region always available once PRUSS
is clocked -- no device-tree resource needed for Linux to reach it, it's
just a known physical address), writes it directly into R30 (which
immediately drives the 4 physical pins since they're pinmuxed to R30 bits
0-3), and mirrors the applied value back to a second offset for
readback.

`pru4pin.py` on the Linux side `mmap()`s that same physical shared-RAM
address via `/dev/mem` (root required) and reads/writes the two 4-bit
words directly -- no kernel driver, no rpmsg, no polling loop on the
Linux side.

Because these pins are PRU-output-only in this pinmux mode (mode 5),
"reading a pin's state" here means reading back the last value
*commanded* by the PRU firmware, which is also the actual driven pin
level (there is no external disturbance path on an output-only pin).
This is not a literal voltage-sense/ADC readback.

## Building

Requires `ti-pru-cgt-v2.3` (TI's PRU C/assembly toolchain) and the
`dtb-6.18.x` device-tree source tree, both of which ship on the
BeagleBoard.org Debian image used for this board
(see [../README.md](../README.md)).

```sh
# Overlay
cpp -nostdinc -I /opt/source/dtb-6.18.x/include -undef \
    -x assembler-with-cpp BB-PRU0-4PIN-00A0.dts BB-PRU0-4PIN-00A0.dts.pp
dtc -@ -O dtb -o BB-PRU0-4PIN-00A0.dtbo BB-PRU0-4PIN-00A0.dts.pp

sudo cp BB-PRU0-4PIN-00A0.dtbo /boot/dtbs/$(uname -r)/overlays/
sudo cp BB-PRU0-4PIN-00A0.dtbo /boot/dtbs/$(uname -r)/

# Firmware
asmpru -v3 pru0_4pin.asm
# -e START is required: without an explicit entry point, lnkpru silently
# falls back to a legacy non-ELF output format that the kernel's pru_rproc
# driver rejects with "Image is corrupted (bad magic)".
lnkpru -e START --ram_model -o pru0_4pin.out pru0_4pin.obj pru0_4pin.cmd \
    -m pru0_4pin.map
sudo cp pru0_4pin.out /lib/firmware/pru0-4pin.fw
```

## Enabling the overlay

Edit `/boot/uEnv.txt` and point `uboot_overlay_pru=` at the new overlay
(the stock default, `AM335X-PRU-UIO-00A0.dtbo`, is a legacy overlay for
the old TI BSP kernels' `uio_pruss` driver and does not work on this
board's mainline-style 6.18 kernel -- it sets the `pruss` node's
`compatible` string to one no driver in this kernel recognizes, so PRUSS
silently never gets a `remoteproc` device):

```
#uboot_overlay_pru=AM335X-PRU-UIO-00A0.dtbo
uboot_overlay_pru=BB-PRU0-4PIN-00A0.dtbo
```

Then `sudo reboot`. U-Boot applies overlays only at boot time -- there is
no dynamic configfs overlay loading available on this kernel build.

## Verifying and using

```sh
# After reboot: PRU0 should be remoteproc1, "4a334000.pru"
cat /sys/class/remoteproc/remoteproc1/name    # -> 4a334000.pru
cat /sys/class/remoteproc/remoteproc1/state   # -> offline

echo pru0-4pin.fw | sudo tee /sys/class/remoteproc/remoteproc1/firmware
echo start        | sudo tee /sys/class/remoteproc/remoteproc1/state
cat /sys/class/remoteproc/remoteproc1/state   # -> running

# Set P9_30 and P9_28 high, P9_31 and P9_29 low, then read back:
sudo python3 pru4pin.py 0b1010
# -> applied: 0b1010

# Read current state without changing it:
sudo python3 pru4pin.py
```

To start the firmware automatically on every boot, add the two `tee`
commands above to a systemd oneshot service or `/etc/rc.local`.

## Troubleshooting notes from bring-up

- `pinctrl-single ...: pin PIN100 already requested by 48038000.mcasp` --
  `mcasp0` (HDMI audio) was still claiming one of the 4 pins because only
  the generic `PX_YY_pinmux` helper nodes were disabled, not `mcasp0`
  itself. Fixed by adding `&mcasp0 { status = "disabled"; };` to the
  overlay.
- `remoteproc ...: Image is corrupted (bad magic)` -- the firmware file
  was valid TI COFF, not ELF, because `lnkpru` was invoked without an
  explicit entry point and silently produced a non-ELF image (no
  `0x7F 'E' 'L' 'F'` header). The mainline `pru_rproc` driver requires a
  real ELF32 image. Fixed by linking with `-e START --ram_model`, after
  which the output file's first 4 bytes are the correct ELF magic.
