# BeagleBone Black

Placeholder for work using a BeagleBone Black as a (likely faster, more capable) SWD debug host
in place of the Arduino Nano clone used in [/arduino](../arduino). Nothing BBB-specific has been
built yet; this records the board's as-received state and how to reach it.

## Board, as received (2026-10-29)

- Fresh out of the box, factory image: **Debian GNU/Linux 7**, kernel `3.8.13-bone47 #1 SMP Fri
  Apr 11 01:36:09 UTC 2014 armv7l`. This is the original 2014 factory eMMC image BeagleBoard.org
  shipped on new boards for years; current images are far newer. Reflashing is worth considering
  before building anything on this board (see below).
- Hostname: `beaglebone`.
- Default login: `debian` / `temppwd`.
- USB enumeration on macOS: a composite USB device (`bDeviceClass=239`, `bDeviceSubClass=2`,
  i.e. a multi-interface/"miscellaneous" composite device), identified in `ioreg -p IOUSB -l` as
  `BeagleBoneBlack@...`, USB vendor name `Circuitco` (the board's original manufacturer).

## Connecting

### Serial console (confirmed working)

The board exposes a USB CDC-ACM serial console directly, no drivers or network needed. On
macOS it showed up as `/dev/cu.usbmodem14603` (the trailing number is assigned per-port/per-hub
and may differ on reconnect or on a different machine -- check `ls /dev/cu.usbmodem*`).

```sh
stty -f /dev/cu.usbmodemXXXXX 115200 raw -echo
cat /dev/cu.usbmodemXXXXX &          # watch output
printf 'debian\r' > /dev/cu.usbmodemXXXXX   # after the login prompt appears
printf 'temppwd\r' > /dev/cu.usbmodemXXXXX  # after the password prompt appears
```

Confirmed boot banner:

```
Debian GNU/Linux 7 beaglebone ttyGS0
default username:password is [debian:temppwd]
Support/FAQ: http://elinux.org/Beagleboard:BeagleBoneBlack_Debian
The IP Address for usb0 is: 192.168.7.2
beaglebone login:
```

### USB networking (not yet working on this Mac)

The board's own `usb0` interface is configured at `192.168.7.2` (per the banner above), which
would normally let the host reach it directly over `ssh debian@192.168.7.2` and the bundled
Cloud9 IDE, once the host side is assigned the conventional `192.168.7.1`. On this Mac, the
board enumerates two candidate virtual Ethernet interfaces (seen as `en3`/`en4` via
`networksetup -listallhardwareports`, both labelled generic "Ethernet Adapter"), but both show
`status: inactive` / `media: none` -- no link negotiated on the host side, even well after the
board finished booting (confirmed separately via the serial console). Assigning either a static
IP (`192.168.7.1`/`255.255.255.0`) requires admin/`sudo` privileges not available in the
session that set this up; this was not resolved, only documented. A real terminal with an
interactive `sudo` password, or manually adding a manual-IP network service in **System
Settings -> Network** for whichever of the two interfaces corresponds to the board, is the
likely next step.

## Before building anything here

Worth doing early, before this becomes the debug host for further SWD work:

- **Reflash to a current image.** The 2014 factory image is well past end-of-support; a current
  Debian or minimal image from beagleboard.org (or balenaEtcher/`bbb-eMMC-flasher`) would be a
  more sensible base, especially if the plan is to run OpenOCD, pyOCD, or similar directly on
  the board against the shaver's SWD pins.
- **Decide the role.** If this is meant to replace the Arduino as the SWD host (its Linux
  userspace, real GPIO count, and access to mature tools like OpenOCD/pyOCD would remove the
  bit-bang-timing problems documented in [../arduino/docs/swd-read-methods-research.md](../arduino/docs/swd-read-methods-research.md)), that's a different code path from the Arduino sketches in
  [/arduino](../arduino) -- most likely a Python or C tool using `libgpiod`/sysfs GPIO plus an
  existing SWD library, rather than a from-scratch bit-bang like the Nano's.
