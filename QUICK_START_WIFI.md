# Quick Start: WiFi Setup (Fixed)

## 5-Minute Setup

### Paso 1: Instala daemon dependencies (Mac)
```bash
cd Clawdmeter-m5fire.zip/daemon
bash setup-wifi-daemon.sh
```

### Paso 2: Arranca daemon (Mac)
```bash
# Terminal 1: Start daemon
sudo python3 wifi-api-daemon.py --host 192.168.1.105 --port 80

# If port 80 denied, use 8080 (but update firmware too):
python3 wifi-api-daemon.py --host 192.168.1.105 --port 8080
```

### Paso 3: Flash firmware a M5
```bash
cd firmware
pio run -t upload --upload-port /dev/ttyACM0
```

### Paso 4: Monitor logs (to diagnose)
```bash
# Terminal 2: Watch M5 debug logs
screen /dev/tty.usbmodem* 115200

# You should see:
# [15:30:24] DEBUG: Polling usage from API...
# [15:30:25] DEBUG: Success! Session: 45.0%, Weekly: 28.0%
```

### Paso 5: Setup WiFi en M5
1. M5 boots and creates AP: `ClawdMeter-Setup`
2. Conecta desde tu teléfono/Mac
3. Open browser → should auto-open `http://192.168.1.1`
4. Enter your WiFi SSID and password
5. Click "Connect & Save"
6. M5 reconnects to your network

### Paso 6: Verifica en M5 UI
- Press **Button B** until you see "WiFi" screen
- Should show: `Connected | SSID | IP | Signal 85%`
- If shows `Disconnected` → check WiFi logs (see Troubleshooting)

---

## Verification: LEDs + Serial

### Expected behavior (every 30 seconds):
```
Terminal 1 (daemon):
[OK] 200 response to GET /api/usage

Terminal 2 (M5 serial):
DEBUG: Polling usage from API...
DEBUG: Success! Session: 45.0%, Weekly: 28.0%
[M5 display] Usage updated
[M5 LEDs] Orange flash (2 seconds)
```

### If RED LED blinking every 30s:
```
Terminal 2 shows:
DEBUG: Not connected to WiFi, skipping poll
OR
DEBUG: API polling failed - timeout or connection error
```

**→ Check daemon is running (Terminal 1)**
**→ Check IP/port match (Troubleshooting.md)**

---

## If daemon uses different IP

If your Mac IP is NOT `192.168.1.105`:

Edit `firmware/src/main.cpp` line 72:
```cpp
http_client_init("YOUR_MAC_IP_HERE", "80");
```

Then re-flash:
```bash
pio run -t upload --upload-port /dev/ttyACM0
```

---

## If port 80 unavailable

Use port 8080 instead:

**Terminal 1:**
```bash
python3 wifi-api-daemon.py --host 192.168.1.105 --port 8080
```

**firmware/src/main.cpp line 72:**
```cpp
http_client_init("192.168.1.105", "8080");
```

**Re-flash firmware.**

---

## Full troubleshooting
See `TROUBLESHOOTING_WIFI.md`

---

## Still stuck?

Share from both terminals:

**From Mac:**
```bash
# Terminal 1 (daemon) output
ps aux | grep wifi-api
lsof -i :80
curl http://localhost:80/api/usage
```

**From M5:**
```bash
# Terminal 2 (serial) - screenshot of logs
# Plus description of LED behavior and UI state
```
