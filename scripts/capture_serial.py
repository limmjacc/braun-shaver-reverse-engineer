# Reset the Nano by pulsing DTR, then log serial output (macOS ioctl constants, no pyserial needed).
# Usage: python3 -I capture_serial.py <device> <seconds> <outfile>
import os, sys, time, termios, fcntl, select
dev, secs, out = sys.argv[1], float(sys.argv[2]), sys.argv[3]
fd = os.open(dev, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
a = termios.tcgetattr(fd)
a[0] = 0; a[1] = 0; a[3] = 0
a[2] = (a[2] & ~(termios.CSIZE | termios.PARENB)) | termios.CS8 | termios.CLOCAL | termios.CREAD
a[4] = a[5] = termios.B115200
termios.tcsetattr(fd, termios.TCSANOW, a)
TIOCM_DTR = 0x2
TIOCMBIC, TIOCMBIS = 0x8004746B, 0x8004746C
def dtr(on):
    fcntl.ioctl(fd, TIOCMBIS if on else TIOCMBIC, TIOCM_DTR.to_bytes(4, "little"))
dtr(False); time.sleep(0.1); dtr(True)   # pulse DTR to reset the Nano
buf = b""
end = time.time() + secs
while time.time() < end:
    r, _, _ = select.select([fd], [], [], 0.2)
    if r:
        try: buf += os.read(fd, 4096)
        except BlockingIOError: pass
open(out, "wb").write(buf)
print(len(buf), "bytes")
