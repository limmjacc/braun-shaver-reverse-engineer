# BeagleBone Black

A BeagleBone Black used both as a general Linux-based hardware platform and as a candidate
(likely faster, more capable) SWD debug host in place of the Arduino Nano clone used in
[/arduino](../arduino).

PRU-driven deterministic GPIO work is documented in
[docs/pru-4pin-gpio.md](docs/pru-4pin-gpio.md), source in [pru/](pru/).

## Board, current state (2026-10-30)

- Reflashed from the original 2014 factory image to a current BeagleBoard.org image: **Debian
  GNU/Linux 13 (trixie)**, kernel `6.18.55-bone57`, build `am335x-debian-13.7-base-v6.18-armhf-2026-10-09-4gb`.
  Reflash was done via the board's own `enable-beagle-flasher` / `beagle-flasher` tooling
  (`bb-beagle-flasher` package), which writes U-Boot and copies the running SD card's rootfs
  directly onto the internal eMMC (`/dev/mmcblk1`) -- not the older `uEnv.txt`-edit-based
  `init-eMMC-flasher-v3.sh` mechanism described in older BeagleBoard docs, which is superseded on
  this image.
- Hostname: `BeagleBone`. Login: `debian` / a password set during bring-up (not the default
  `temppwd` shown in the stock login banner -- that banner text is static and does not reflect
  whether the password has already been changed).
- Root filesystem on `/dev/mmcblk1p3` (eMMC), boot/firmware FAT partition on `/dev/mmcblk1p1`
  mounted at `/boot/firmware`. U-Boot's own config/overlay file is `/boot/uEnv.txt` on the ext4
  root, not on the FAT partition -- the FAT partition only holds a legacy USB-mass-storage
  "getting started" payload (`ID.txt`, `START.HTM`, `sysconf.txt`).
- No RTC battery: clock resets to the image build date on every power-up. NTP (`systemd-timesyncd`)
  did not auto-sync over the USB network path in testing (likely a NAT/UDP quirk in macOS Internet
  Sharing); set manually with `sudo date -u -s "YYYY-MM-DD HH:MM:SS"` if `apt` signature
  verification fails with "Not live until ...".
- USB enumeration on macOS: a composite USB device (`bDeviceClass=239`, `bDeviceSubClass=2`,
  i.e. a multi-interface/"miscellaneous" composite device), identified in `ioreg -p IOUSB -l` as
  `BeagleBoneBlack@...`.

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

### USB networking (working, via macOS Internet Sharing)

The board's own `usb0` interface is configured at `192.168.7.2`. To give it internet access from
the Mac: **System Settings -> General -> Sharing -> Internet Sharing**, share the Mac's working
connection (e.g. Wi-Fi) "to computers using" the Ethernet adapter that appears for the BBB's USB
link. This assigns the Mac side `192.168.7.1` and NATs traffic through. The board itself needs a
manual default route added on top (its static `192.168.7.2/30` config has no gateway by default):

```sh
sudo ip route add default via 192.168.7.1
```

IPv6 and most IPv4 destinations worked after this; a couple of specific IPv4 hosts
(`debian.beagleboard.org`, `rcn-ee.com` -- BeagleBoard's own extra package repos) returned "No
route to host" in testing, origin not fully diagnosed, but core `deb.debian.org` /
`security.debian.org` access (and therefore `apt update && apt full-upgrade`) worked fine.

## Role

This board currently serves as a general PRU/Linux hardware platform (see
[docs/pru-4pin-gpio.md](docs/pru-4pin-gpio.md)) and remains a candidate to replace the Arduino as
the SWD debug host for the shaver chip -- its Linux userspace, real GPIO count, and access to
mature tools like OpenOCD/pyOCD would remove the bit-bang-timing problems documented in
[../arduino/docs/swd-read-methods-research.md](../arduino/docs/swd-read-methods-research.md).
That would be a different code path from the Arduino sketches in [/arduino](../arduino) -- most
likely a Python or C tool using `libgpiod`/sysfs GPIO plus an existing SWD library, rather than a
from-scratch bit-bang like the Nano's. Not yet started.
