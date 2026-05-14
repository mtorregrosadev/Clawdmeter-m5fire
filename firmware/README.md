# Clawdmeter M5Stack Fire

A real-time Claude API usage monitor running on M5Stack Fire hardware. Displays session and weekly usage percentages with animated pixel-art Claude characters and LED status indicators.

## Features

- 📊 Real-time usage tracking from Claude API
- 🎨 Animated pixel-art splash screen with 13 Claude animations
- 🔋 Battery status indicator
- 💡 RGB LED status feedback (orange on data refresh, red blink on error)
- 🎮 Three-button interface (A: splash, B: screens, C: refresh)
- 📱 Persistent Playwright browser session for web scraping
- 🔌 Serial communication (115200 baud) between M5Stack and macOS daemon

## Hardware

**M5Stack Fire** (ESP32-based)
- 480×480 AMOLED display
- Built-in battery
- 3 physical buttons (GPIO 0, 18, AXP PKEY)
- 10× addressable RGB LEDs (WS2812B on GPIO 15)

## Project Structure

```
firmware/
├── src/
│   ├── main.cpp              # Setup & loop, button handling
│   ├── ui.cpp/h              # 3-screen UI (splash, usage, gallery)
│   ├── leds.cpp/h            # RGB LED control (Adafruit NeoPixel)
│   ├── splash.cpp/h          # Pixel-art animation engine
│   ├── imu.cpp/h             # Auto-rotation (accelerometer)
│   ├── power.cpp/h           # Battery & charging status
│   ├── data.h                # UsageData struct
│   └── [...other modules]
├── platformio.ini            # Build configuration
├── claude_usage_daemon_scraper_macos.py  # Daemon: scrapes claude.ai & sends data
└── assets_clawd/             # Animation source files
```

## Installation

### Firmware

Requirements:
- PlatformIO CLI: `pip install platformio`
- ESP32 board support

Build:
```bash
cd firmware
python3 -m platformio run
```

Flash (replace with your USB JTAG port):
```bash
python3 -m platformio run -t upload --upload-port /dev/ttyACM0
```

### Daemon (macOS)

Requirements:
```bash
pip install playwright pyserial
playwright install chromium
```

Run:
```bash
cd firmware
python3 claude_usage_daemon_scraper_macos.py
```

The daemon will:
1. Open a persistent Chromium browser window
2. Navigate to https://claude.ai/settings/usage
3. Scrape usage percentages and reset times every 60 seconds
4. Send JSON via serial to the M5Stack
5. Listen for refresh requests from button C

## Usage

**On the M5Stack:**
- **Button A**: Toggle splash screen (animations)
- **Button B**: Cycle between screens (usage ↔ gallery)
- **Button C** (Usage screen): Immediate refresh from daemon

**Data Display:**
- Current session: `% used` + `Resets in Xh Ym`
- Weekly limits: `% used` + `Resets DayOfWeek HH:MM AM/PM`
- Battery indicator in top-right corner

**LED Status:**
- 🟠 Orange (2s): Data received successfully
- 🔴 Red (blinking): JSON parse error
- ⚫ Off: Idle

## Serial Protocol

**M5Stack → Daemon:**
```json
{"cmd":"refresh"}  // Button C request (immediately triggers scrape)
```

**Daemon → M5Stack:**
```json
{"s":37,"sr":114,"w":8,"wr":8400,"st":"scraper-web","ok":true}
```
- `s`: session percentage (0-100)
- `sr`: session reset time (minutes remaining)
- `w`: weekly percentage (0-100)
- `wr`: weekly reset time (minutes remaining)
- `st`: status string
- `ok`: success flag

## Architecture Notes

- **Persistent Serial**: Daemon keeps the serial connection open to listen for button C refresh requests
- **Reset Times**: Extracted from claude.ai page in format `"Resets in 1 hr 49 min"` (session) and `"Resets Wed 11:00 PM"` (weekly)
- **Weekly Calculations**: Daemon calculates minutes until the specified day/time from current system time
- **LED Timeout**: Orange LED auto-off after 2 seconds; red blink continues until next successful refresh

## Development

### Adding Animations

Edit animations at [claudepix.vercel.app](https://claudepix.vercel.app), then:
```bash
node tools/scrape_claudepix.js
node tools/convert_to_c.js
```

This updates `src/splash_animations.h`.

### UI Iteration

Temporarily change the boot screen in `main.cpp`:
```cpp
ui_show_screen(SCREEN_SPLASH);  // Change to SCREEN_USAGE or SCREEN_GALLERY
```

Screenshot with:
```bash
./screenshot.sh out.png /dev/ttyACM0
```

### Known Hardware Constraints

- **PSRAM**: Requires `board_build.arduino.memory_type = qio_opi` in platformio.ini
- **AMOLED**: CO5300 cannot rotate—rotation done via CPU pixel remapping
- **Touch**: CST9220 must centralize reads; polling from multiple places causes data loss

## Troubleshooting

**M5Stack shows "ready" but display is black:**
- Check PSRAM initialization in build output
- Verify OPI PSRAM memory type in platformio.ini

**LED not lighting:**
- Verify GPIO 15 connection
- Check Adafruit NeoPixel library version (1.15.1+)

**Scraper not updating:**
- Restart daemon (port may be busy)
- Check if logged into claude.ai/settings/usage in browser
- Verify Cloudflare verification passes (headless=False in scraper)

## License

Built with ❤️ for tracking Claude API usage in style.
