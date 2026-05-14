# ClawdMeter WiFi Setup

**ESP32-S3 desk-side Claude Code usage monitor** with WiFi connectivity.

## Hardware

- **M5Stack Fire AMOLED** (480×480 touch display)
- **WiFi**: Connects to local network, polls daemon via HTTP
- **Battery**: ~8h with screen on, days with screen off

## Quick Start (5 minutes)

### 1. Prerequisites

```bash
# Mac: Install Python dependencies
cd daemon
pip3 install requests playwright
playwright install
```

### 2. Start API Daemon (Mac)

```bash
cd daemon
sudo python3 wifi-api-daemon.py --host 192.168.1.105 --port 80
```

The daemon will:
- Open a browser window
- Wait for you to log into claude.ai/settings/usage (once only)
- Scrape usage every 60 seconds
- Serve JSON at `http://192.168.1.105:80/api/usage`

### 3. Flash Firmware (M5Fire)

```bash
cd firmware
pio run -t upload --upload-port /dev/ttyACM0
```

### 4. Setup WiFi on M5Fire

1. M5 boots and creates AP: `ClawdMeter-Setup`
2. Connect from your phone/Mac
3. Password: `12345678`
4. Browser auto-opens config page (or manually go to `http://192.168.1.1`)
5. Enter your WiFi SSID and password
6. Click "Connect"
7. M5 reconnects to your network

### 5. Verify

- M5 should show `Connected | SSID | IP | Signal 100%` on WiFi screen
- Press **Button B** to cycle screens
- Press **Button C** on Usage screen to force immediate refresh

---

## How It Works

```
M5Fire (WiFi)  →  (polls every 30s)  →  Mac Daemon (HTTP)
   Button C press                →  /api/refresh (scrape now)
   
LED Feedback:
  🟠 Orange (2s flash) = Success
  🔴 Red blinking = Error
```

---

## Changing WiFi Network

### Option A: Use Captive Portal (Easy)

1. Hold **Button A** to show splash screen
2. Long press **Button A** again to access AP mode
3. Or power-cycle the M5 without saved credentials

### Option B: Clear EEPROM

Compile firmware with this in `main.cpp` setup:

```cpp
// EEPROM.begin(256);
// for (int i = 0; i < 96; i++) EEPROM.write(i, 0);
// EEPROM.commit();
// EEPROM.end();
```

Uncomment, flash once, then comment out again and reflash.

---

## Configuration

### Daemon IP/Port

1. Copy the template to create your config:

```bash
cp firmware/include/config.h.example firmware/include/config.h
```

2. Edit `firmware/include/config.h` and set your Mac's IP and port:

```cpp
#define DAEMON_IP "192.168.1.XXX"  // Your Mac's local IP
#define DAEMON_PORT "80"           // Or "8080" if port 80 unavailable
```

Find your Mac's IP:
```bash
ifconfig | grep "inet " | grep -v 127.0.0.1
```

3. Rebuild and flash firmware.

### Daemon Port

If port 80 unavailable:

```bash
python3 wifi-api-daemon.py --host 192.168.1.105 --port 8080
```

Then update firmware IP config to port `8080`.

---

## Troubleshooting

### "Not connected to WiFi"

- M5 hasn't connected to your network yet
- Check WiFi credentials were saved correctly
- Try power-cycling M5

### "API polling failed - timeout"

- Daemon not running on Mac
- Check: `ps aux | grep wifi-api`
- Check: `curl http://localhost:80/api/usage`
- Mac IP might be wrong (check with `ifconfig | grep "inet "`)

### "Reconnecting" + timeouts

- Weak WiFi signal
- Daemon unresponsive
- Check serial logs: `screen /dev/tty.usbmodem* 115200`

### Button C refresh doesn't show new data immediately

- Daemon scraper takes 2-3 seconds to complete
- M5 polls after 2.5 seconds, should see data within 5 seconds total

---

## Features

✅ **Captive portal** — Setup WiFi without code changes  
✅ **EEPROM storage** — Credentials persist across restarts  
✅ **Auto-reconnect** — Rejoins WiFi automatically  
✅ **HTTP polling** — 30-second interval, configurable  
✅ **Button refresh** — Button C triggers immediate update  
✅ **LED feedback** — Orange = success, Red = error  
✅ **WiFi status screen** — Shows SSID, IP, signal strength  
✅ **Battery powered** — Works without USB after setup  

---

## Limitations

❌ **Public WiFi** — eduroam (802.1X) not supported  
❌ **Remote access** — Requires local network only  
❌ **Multiple networks** — Can only store one SSID  

---

## Files

```
firmware/
  src/
    main.cpp           — Loop, button handling, polling
    wifi.cpp/h         — WiFi state machine, AP mode
    http_client.cpp/h  — HTTP GET/POST, refresh API
    ui.cpp/h           — Display screens
    leds.cpp/h         — LED control
    
daemon/
  wifi-api-daemon.py   — HTTP server + Playwright scraper
```

---

## License

MIT
