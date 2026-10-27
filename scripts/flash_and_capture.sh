#!/bin/sh
# Compile, upload and capture the SWD dump from the Nano clone (old bootloader, macOS).
# Usage: scripts/flash_and_capture.sh [serial-port] [capture-seconds]
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
PORT=${1:-$(ls /dev/cu.usbserial-* 2>/dev/null | head -n 1)}
SECS=${2:-15}
FQBN=arduino:avr:nano:cpu=atmega328old
OUT="$ROOT/dump_$(date +%Y%m%d_%H%M%S).txt"

[ -n "$PORT" ] || { echo "No serial port found" >&2; exit 1; }
arduino-cli compile --fqbn "$FQBN" "$ROOT/nano_swd_dump"
arduino-cli upload -p "$PORT" --fqbn "$FQBN" "$ROOT/nano_swd_dump"
python3 -I "$ROOT/scripts/capture_serial.py" "$PORT" "$SECS" "$OUT"
cat "$OUT"
echo "Saved to $OUT"
