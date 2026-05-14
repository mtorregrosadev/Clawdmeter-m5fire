# ClawdMeter M5Fire

ESP32-S3 desk-side monitor for Claude Code usage tracking via **WiFi**.

## Quick Start

👉 **[WiFi Setup Guide →](README_WIFI.md)**  
Complete setup, config, troubleshooting

---

## What is ClawdMeter?

Real-time monitor showing Claude usage:
- **Session usage** — % of 5-hour limit used
- **Weekly usage** — % of 7-day limit used
- **Reset timers** — Minutes until limits reset
- **WiFi status** — Connected network, signal strength, IP
- **Pixel animations** — Clawd splash screen with usage-rate feedback

## Features

✅ **WiFi-based** — No Bluetooth pairing, auto-reconnect  
✅ **Captive portal** — Setup WiFi without code changes  
✅ **Battery powered** — Works 8+ hours without USB  
✅ **Auto-refresh** — Polls daemon every 30 seconds  
✅ **Button refresh** — Force immediate update on-demand  
✅ **LED feedback** — Orange (success) / Red (error)  
✅ **Pixel animations** — Clawd sprites on splash screen  

## Hardware

- **[Waveshare ESP32-S3-Touch-AMOLED-2.16](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-2.16)** 
  - 480×480 AMOLED display
  - 3 physical buttons + capacitive touch
  - Internal Li-Po battery
  - WiFi 802.11b/g/n

## Usage

**Buttons**:
- **A** — Show/hide splash animation
- **B** — Cycle screens (Usage ↔ WiFi Status)
- **C** — Force refresh (Usage screen)

**LED feedback**:
- 🟠 Orange flash (2s) = API polling succeeded
- 🔴 Red blinking = API error or disconnected

## Setup (5 minutes)

1. **Install daemon dependencies**:
   ```bash
   pip3 install requests playwright
   playwright install
   ```

2. **Start API daemon on Mac**:
   ```bash
   cd daemon
   sudo python3 wifi-api-daemon.py --host 192.168.1.105 --port 80
   ```

3. **Configure daemon IP** (firmware only):
   ```bash
   cd firmware
   cp include/config.h.example include/config.h
   # Edit include/config.h and set your Mac's IP:
   #   #define DAEMON_IP "192.168.1.XXX"
   ```

4. **Flash firmware to M5**:
   ```bash
   cd firmware
   pio run -t upload --upload-port /dev/ttyACM0
   ```

5. **Configure WiFi**:
   - M5 boots → creates `ClawdMeter-Setup` AP
   - Connect from phone (password: `12345678`)
   - Browser opens config page at `http://192.168.1.1`
   - Enter your WiFi SSID and password
   - M5 reconnects to your network

6. **Done!** M5 now polls daemon automatically every 30s.

For detailed instructions → **[README_WIFI.md](README_WIFI.md)**

---

## Architecture

```
M5Fire (WiFi)          Mac Daemon (HTTP)         Claude API
     |                      |                         |
     +---(polls 30s)------->+                         |
                            +---(scrapes 60s)------->+
                            |
     +<---- JSON data ------+
     |
  [display]
  [LED]
```

## Files

```
firmware/          — ESP32-S3 firmware (C++)
├─ src/
│  ├─ main.cpp          — Main loop, buttons
│  ├─ wifi.cpp          — WiFi + captive portal
│  ├─ http_client.cpp   — HTTP polling
│  ├─ ui.cpp            — Display
│  ├─ leds.cpp          — LED control
│  └─ splash.cpp        — Pixel animations
└─ platformio.ini

daemon/            — Mac daemon (Python)
└─ wifi-api-daemon.py   — Scrapes usage, serves HTTP
```

## Documentation

- **[README_WIFI.md](README_WIFI.md)** — Complete setup + troubleshooting
- **[TROUBLESHOOTING_WIFI.md](TROUBLESHOOTING_WIFI.md)** — Common issues
- **[QUICK_START_WIFI.md](QUICK_START_WIFI.md)** — Quick reference
- **[CLAUDE.md](CLAUDE.md)** — Development notes

## Limitations

- ❌ **Public WiFi** — eduroam (802.1X auth) not supported
- ❌ **Remote access** — Requires local network only
- ❌ **Multiple networks** — One SSID stored (clear EEPROM to change)
- ⚠️ **Offline** — Shows cached data if daemon unreachable

## Build

```bash
cd firmware
pio run -d firmware                                      # Compile
pio run -d firmware -t upload --upload-port /dev/ttyACM0 # Flash
```

## Troubleshooting

**"Not connected to WiFi"** — Check WiFi credentials, try power-cycle

**"API polling failed"** — Daemon not running, check: `ps aux | grep wifi-api`

**Refresh slow** — Daemon scraper takes ~3 seconds, normal

See **[TROUBLESHOOTING_WIFI.md](TROUBLESHOOTING_WIFI.md)** for more.

---

## License

MIT

## Bluetooth pairing

After flashing, the device advertises as "Claude Controller". Pair it once:

```bash
# Scan for the device
bluetoothctl scan le

# When "Claude Controller" appears, pair and trust it
bluetoothctl pair F4:12:FA:C0:8F:E5    # use your device's MAC
bluetoothctl trust F4:12:FA:C0:8F:E5
```

The MAC address is shown on the Bluetooth screen — press the middle (PWR) button to cycle to it.

## Install the daemon

The daemon polls your Claude usage every 60 seconds and sends it to the display over BLE.

```bash
./install.sh
systemctl --user start claude-usage-daemon
```

Check status: `systemctl --user status claude-usage-daemon`

View logs: `journalctl --user -u claude-usage-daemon -f`

## How it works

1. The daemon reads your Claude Code OAuth token from `~/.claude/.credentials.json`.
2. It makes a minimal API call to `api.anthropic.com/v1/messages` — one token of Haiku, basically free.
3. The usage numbers come straight out of the response headers (`anthropic-ratelimit-unified-5h-utilization` and friends).
4. The daemon connects to the ESP32 over BLE and writes a JSON payload to the GATT RX characteristic.
5. The firmware parses it and updates the LVGL dashboard.
6. The firmware also tracks the rate of change of session % over a 5-minute window and picks splash animations from the matching mood group.
7. The two side buttons are independent of all of this — they send Space and Shift+Tab as BLE HID keyboard input to the paired host directly.

## Physical buttons

The board has three side buttons. Left and right do the same thing on every screen; the middle button is screen-aware.

| Button           | GPIO         | Function                                                       |
| ---------------- | ------------ | -------------------------------------------------------------- |
| **Left**         | GPIO 0       | Hold to send Space (Claude Code voice-mode push-to-talk)       |
| **Middle** (PWR) | AXP2101 PKEY | Cycle screens (Usage ↔ Bluetooth); on splash, cycle animations |
| **Right**        | GPIO 18      | Press to send Shift+Tab (Claude Code mode toggle)              |

Space and Shift+Tab go out as standard BLE HID keyboard reports, so they trigger in whatever window has focus on the paired host — not just Claude Code.

## BLE protocol

The device advertises a custom GATT service alongside the standard HID keyboard service:

|                            | UUID                                   |
| -------------------------- | -------------------------------------- |
| **Data Service**           | `4c41555a-4465-7669-6365-000000000001` |
| RX Characteristic (write)  | `4c41555a-4465-7669-6365-000000000002` |
| TX Characteristic (notify) | `4c41555a-4465-7669-6365-000000000003` |
| **HID Service**            | `00001812-0000-1000-8000-00805f9b34fb` |

JSON payload format (written to RX):

```json
{ "s": 45, "sr": 120, "w": 28, "wr": 7200, "st": "allowed", "ok": true }
```

Fields: `s` = session %, `sr` = session reset (minutes), `w` = weekly %, `wr` = weekly reset (minutes), `st` = status, `ok` = success flag.

## Recompiling fonts

The `firmware/src/font_*.c` files are pre-compiled LVGL bitmap fonts. Sizes
are roughly 1.9× larger than the Panlee 165 PPI panel this project started on,
to match the 314 PPI of the 2.16" AMOLED.

```bash
npm install -g lv_font_conv
```

Generate each one (one at a time — `lv_font_conv` doesn't like loop-driven invocations) with `--no-compress` (required for LVGL 9):

```bash
# Tiempos Text (titles, 56px)
lv_font_conv --font assets/TiemposText-400-Regular.otf -r 0x20-0x7E \
  --size 56 --format lvgl --bpp 4 --no-compress \
  -o firmware/src/font_tiempos_56.c --lv-include "lvgl.h"

# Styrene B (large numbers 48, panel labels 28, small text 24, minimal 20)
for size in 48 28 24 20; do
  lv_font_conv --font assets/StyreneB-Regular.otf -r 0x20-0x7E \
    --size $size --format lvgl --bpp 4 --no-compress \
    -o firmware/src/font_styrene_${size}.c --lv-include "lvgl.h"
done

# DejaVu Sans Mono (32px, with spinner Unicode chars)
lv_font_conv --font assets/DejaVuSansMono.ttf \
  -r 0x20-0x7E,0xB7,0x2026,0x2722,0x2733,0x2736,0x273B,0x273D \
  --size 32 --format lvgl --bpp 4 --no-compress \
  -o firmware/src/font_mono_32.c --lv-include "lvgl.h"
```

**Important:** `lv_font_conv` v1.5.3 outputs LVGL 8 format. Each generated file must be patched for LVGL 9 compatibility:

1. Remove `#if LVGL_VERSION_MAJOR >= 8` guards around `font_dsc` and the font struct
2. Remove the `.cache` field from `font_dsc`
3. Add `.release_glyph = NULL`, `.kerning = 0`, `.static_bitmap = 0` to the font struct
4. Add `.fallback = NULL`, `.user_data = NULL` to the font struct

Without these patches, fonts compile but render as invisible.

## Converting Lucide icons

The UI uses a small set of [Lucide](https://lucide.dev) icons (bluetooth + battery states) converted to RGB565 / RGB565A8 C arrays for LVGL.

```bash
node tools/png_to_lvgl.js assets/icon_bluetooth_48.png icon_bluetooth_data ICON_BLUETOOTH_WIDTH ICON_BLUETOOTH_HEIGHT
```

Default tint is white (`0xFFFFFF`); Lucide PNGs ship as black-on-transparent and would render invisible against the dark UI without it. Pass `--no-tint` for pre-coloured artwork like the logo. Battery icons use RGB565A8 (alpha plane) so they blend cleanly over the splash; the rest are baked RGB565 over the panel colour. Paste the converter output into `firmware/src/icons.h`.

## Splash animations

The animations come from [claudepix.vercel.app](https://claudepix.vercel.app),
a library of Clawd sprites. `tools/scrape_claudepix.js` evaluates the
site's JavaScript in a Node VM to pull out frame data and palettes, then
`tools/convert_to_c.js` turns everything into RGB565 C arrays and writes
`firmware/src/splash_animations.h`.

To re-pull (e.g. when the source library updates):

```bash
node tools/scrape_claudepix.js
node tools/convert_to_c.js
pio run -d firmware -t upload
```

See `tools/README.md` for details.

## Credits

- Pixel-art Clawd animation by [@amaanbuilds](https://x.com/amaanbuilds), sourced from [claudepix.vercel.app](https://claudepix.vercel.app). Frame data and palettes scraped + converted by the tooling in `tools/`.
- Lucide icon set ([lucide.dev](https://lucide.dev), MIT) for bluetooth and battery UI glyphs.
- Anthropic brand fonts (Tiempos Text, Styrene B) — see licensing warning below.

## Licensing gray area warning

The software in this repository uses and adheres to the Anthropic brand guidelines and uses the same proprietary fonts that Anthropic has a licnese for but this software uses without permission as well as using assets from Anthropic such as the copyrighted Clawd mascot so even though the code in this repo is non-proprietary I will not license it myself under a copyleft license since this repo includes proprietary fonts and copyrighted assets. Please be aware of this if you fork or copy the code from this repo. **You have been warned!**
