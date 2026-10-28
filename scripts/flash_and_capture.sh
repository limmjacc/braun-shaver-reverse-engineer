#!/bin/sh
# Compile, upload and capture the SWD dump from the Nano clone (old bootloader, macOS), then
# parse any BEGIN_x/END_x regions in the captured log into .bin/.hex files alongside it.
# Usage: scripts/flash_and_capture.sh [serial-port] [capture-seconds]
# Default capture window (30s) covers acquire plus a full SRAM+flash dump at 115200 baud
# (~9200 lines of text takes roughly 15-20s to transmit on its own); raise it if "sweep done" /
# "done" never appears in the output before the capture ends.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
PORT=${1:-$(ls /dev/cu.usbserial-* 2>/dev/null | head -n 1)}
SECS=${2:-30}
FQBN=arduino:avr:nano:cpu=atmega328old
OUT="$ROOT/dump_$(date +%Y%m%d_%H%M%S).txt"

[ -n "$PORT" ] || { echo "No serial port found" >&2; exit 1; }
arduino-cli compile --fqbn "$FQBN" "$ROOT/nano_swd_dump"
arduino-cli upload -p "$PORT" --fqbn "$FQBN" "$ROOT/nano_swd_dump"
python3 -I "$ROOT/scripts/capture_serial.py" "$PORT" "$SECS" "$OUT"
cat "$OUT"
echo "Saved to $OUT"
python3 -I "$ROOT/scripts/parse_dump.py" "$OUT" "$ROOT" || echo "(no BEGIN_x/END_x regions to parse -- see $OUT)"
