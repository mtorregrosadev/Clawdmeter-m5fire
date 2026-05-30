#!/bin/bash
# Capture the M5Stack Fire display via serial. Size is auto-detected
# from the SCREENSHOT_START header returned by the firmware.
# Usage: ./screenshot.sh [output.png] [port]

OUTPUT="${1:-screenshot.png}"
PORT="${2:-/dev/cu.usbserial-575C0330481}"

TMPRAW=$(mktemp /tmp/screenshot_XXXXXX.raw)
TMPDIM=$(mktemp /tmp/screenshot_dim_XXXXXX)
trap "rm -f '$TMPRAW' '$TMPDIM'" EXIT

echo "Taking screenshot from $PORT..."

python3 - "$PORT" "$TMPRAW" "$TMPDIM" << 'PYEOF'
import serial, sys

port_path, raw_path, dim_path = sys.argv[1], sys.argv[2], sys.argv[3]

port = serial.Serial(port_path, 115200, timeout=10)
port.reset_input_buffer()
port.write(b"screenshot\n")
port.flush()

w = h = raw_size = 0
while True:
    line = port.readline().decode("utf-8", errors="replace").strip()
    if line.startswith("SCREENSHOT_START"):
        parts = line.split()
        w, h, raw_size = int(parts[1]), int(parts[2]), int(parts[3])
        break
    if line == "SCREENSHOT_ERR":
        print("Device reported screenshot error", file=sys.stderr)
        sys.exit(1)

data = b""
while len(data) < raw_size:
    chunk = port.read(min(4096, raw_size - len(data)))
    if not chunk:
        print(f"Timeout: got {len(data)} of {raw_size} bytes", file=sys.stderr)
        sys.exit(1)
    data += chunk

with open(raw_path, "wb") as f:
    f.write(data)
with open(dim_path, "w") as f:
    f.write(f"{w}x{h}")

for _ in range(10):
    line = port.readline().decode("utf-8", errors="replace").strip()
    if line == "SCREENSHOT_END":
        break

port.close()
print(f"Captured {w}x{h} ({len(data)} bytes)")
PYEOF

if [ $? -ne 0 ]; then
    echo "Screenshot capture failed"
    exit 1
fi

DIM=$(cat "$TMPDIM")

ffmpeg -y -f rawvideo -pixel_format rgb24 -video_size "$DIM" \
    -i "$TMPRAW" -update 1 -frames:v 1 "$OUTPUT" 2>/dev/null || true

if [ -f "$OUTPUT" ]; then
    echo "Saved: $OUTPUT ($DIM)"
else
    echo "Error: conversion failed"
    exit 1
fi
