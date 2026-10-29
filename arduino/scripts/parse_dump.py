#!/usr/bin/env python3
"""Turn a captured nano_swd_dump.ino serial log into real .bin and Intel HEX files.

The sketch prints, per region: `BEGIN_<NAME>`, then `ADDR VALUE` lines (both 8 hex digits,
space-separated, one 32-bit little-endian word per line), then `END_<NAME>`. This parses one or
more such regions out of a log file and writes `<name>.bin` (raw, gap-filled with 0xFF) and
`<name>.hex` (Intel HEX, 16-bit addressing only -- fine for this chip's <=64 KB address range)
for each.

Usage:
    python3 -I scripts/parse_dump.py <captured_log.txt> [out_dir]
"""
import re
import sys
from pathlib import Path

LINE_RE = re.compile(r"^([0-9A-Fa-f]{8}) ([0-9A-Fa-f]{8})\s*$")


def parse_regions(text):
    """Yield (name, {addr: word}) for each BEGIN_x/END_x block found."""
    name = None
    words = {}
    for raw in text.splitlines():
        line = raw.strip()
        if line.startswith("BEGIN_"):
            name = line[len("BEGIN_"):]
            words = {}
            continue
        if line.startswith("END_"):
            if name is not None:
                yield name, words
            name = None
            continue
        if name is None:
            continue
        m = LINE_RE.match(line)
        if m:
            addr = int(m.group(1), 16)
            val = int(m.group(2), 16)
            words[addr] = val
        # Lines like "ERR 4" inside a region are skipped; the region is still emitted with
        # whatever words were read before the error, so a partial dump is not silently lost.


def write_bin(words, out_path):
    if not words:
        out_path.write_bytes(b"")
        return
    lo = min(words)
    hi = max(words) + 4
    buf = bytearray(b"\xff" * (hi - lo))
    for addr, val in words.items():
        off = addr - lo
        buf[off:off + 4] = val.to_bytes(4, "little")
    out_path.write_bytes(bytes(buf))
    print(f"  {out_path.name}: {len(buf)} bytes, base 0x{lo:08X}")


def ihex_record(rectype, addr16, data):
    length = len(data)
    payload = bytes([length, (addr16 >> 8) & 0xFF, addr16 & 0xFF, rectype]) + data
    checksum = (-sum(payload)) & 0xFF
    return ":" + payload.hex().upper() + f"{checksum:02X}"


def write_hex(words, out_path):
    lines = []
    if words:
        lo = min(words)
        hi = max(words) + 4
        buf = bytearray()
        for a in range(lo, hi, 4):
            buf += words.get(a, 0xFFFFFFFF).to_bytes(4, "little") if a in words else b"\xff\xff\xff\xff"
        current_ela = None  # upper 16 bits of address last emitted via a type-0x04 record
        for off in range(0, len(buf), 16):
            chunk = buf[off:off + 16]
            addr32 = lo + off
            ela = (addr32 >> 16) & 0xFFFF
            if ela != current_ela:
                lines.append(ihex_record(0x04, 0x0000, ela.to_bytes(2, "big")))
                current_ela = ela
            lines.append(ihex_record(0x00, addr32 & 0xFFFF, chunk))
    lines.append(ihex_record(0x01, 0x0000, b""))  # EOF record
    out_path.write_text("\n".join(lines) + "\n")
    print(f"  {out_path.name}: {len(lines)} records")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    log_path = Path(sys.argv[1])
    out_dir = Path(sys.argv[2]) if len(sys.argv) > 2 else log_path.parent
    out_dir.mkdir(parents=True, exist_ok=True)

    text = log_path.read_text(errors="replace")
    found = False
    for name, words in parse_regions(text):
        found = True
        safe = name.lower()
        print(f"{name}: {len(words)} words parsed")
        write_bin(words, out_dir / f"{safe}.bin")
        write_hex(words, out_dir / f"{safe}.hex")
    if not found:
        print("No BEGIN_x/END_x regions found in the log. Nothing written.")
        sys.exit(1)


if __name__ == "__main__":
    main()
