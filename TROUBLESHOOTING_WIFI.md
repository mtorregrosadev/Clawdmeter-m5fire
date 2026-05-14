# Troubleshooting: WiFi Refresh Loop

Si la M5 es queda en "refresh loop" (LED parpellant contínuament, pantalla congelada), segueix aquests passos:

## 1. Diagnosticar per Serial

```bash
# Obrir monitor serial (115200 baud)
screen /dev/tty.usbmodem* 115200
```

Veuràs logs com:
```
DEBUG: Polling usage from API...
DEBUG: Success! Session: 45.0%, Weekly: 28.0%
DEBUG: Polling usage from API...
...
```

O errors:
```
DEBUG: Not connected to WiFi, skipping poll
DEBUG: API polling failed - timeout or connection error
```

## 2. Verificar WiFi Setup

### WiFi conectat?
```
DEBUG: Not connected to WiFi, skipping poll
```

**Solució:**
1. M5 connectada a `ClawdMeter-Setup`?
2. Captive portal obrit automàticament?
3. Credencials SSID/password correctes?
4. Prova manualment: Settings WiFi → teu SSID → entra password

### API timeout/connection error?
```
DEBUG: API polling failed - timeout or connection error
```

**Solució:**

a) **Verifica que el daemon está corrente:**
```bash
# Check process
ps aux | grep wifi-api

# Should see:
# python3 wifi-api-daemon.py --host 192.168.1.105 --port 80
```

b) **Verifica que listening al port correcte:**
```bash
# Check port 80 binding
lsof -i :80

# Should see daemon listening
```

c) **Test API directa desde Mac:**
```bash
curl -v http://localhost:80/api/usage

# Should return JSON like:
# {"s": 45.5, "sr": 165, "w": 28.0, "wr": 7320, "st": "allowed", "ok": true}
```

d) **Test desde M5 (si pots SSH o similar):**
```bash
# From M5, ping Mac IP
ping 192.168.1.105

# Should respond
```

## 3. IP Mismatch Problem

**Si la M5 no pot arribar a 192.168.1.105:**

El firmware està hardcoded per connectar a `192.168.1.105:80`. Si el daemon corre en altra IP:

### Opció A: Canvia daemon IP
```bash
# Si el daemon corre a localhost (127.0.0.1):
# Needs port forwarding o actualización
# NO recomanat
```

### Opció B: Canvia firmware IP
Edita `firmware/src/main.cpp` línia 72:

```cpp
// Canvia aquesta IP a on corre el daemon
http_client_init("192.168.1.105", "80");

// P.ex, si daemon corre a 192.168.1.100:
http_client_init("192.168.1.100", "80");
```

Després recompila i flasheja.

## 4. LEDs Diagnosis

- 🟠 **Orange** (2s): Success! Poll worked, data received
- 🔴 **Red blinking**: Error - timeout, connection refused, or parse error
- 🟢 **Off**: Idle or disabled

Si veus **RED blinking contínuament** cada 30s:
→ Daemon no respón o IP is wrong

## 5. Daemon Startup Issues

Errors comuns quan arrancant el daemon:

### "No such file or directory: firefox"
```
playwright install  # Install browsers
```

### "Permission denied" port 80
```bash
sudo python3 wifi-api-daemon.py --host 192.168.1.105 --port 80
```

### "Connection refused" cuando M5 tries to connect
```bash
# Daemon not running
sudo python3 wifi-api-daemon.py --host 192.168.1.105 --port 80
```

### "Credentials not found"
```bash
# Daemon can't find Claude credentials
# Make sure ~/.claude/.credentials.json exists
ls ~/.claude/.credentials.json
```

## 6. M5 State Machine

La M5 WiFi té aquests estados:

```
IDLE (AP mode)
  ↓ setup WiFi credencials
CONNECTING
  ↓ timeout (15s)
CONNECTED ↔ FAILED (switch every 2s)
  ↓ si credencials guardades, retry
IDLE (AP mode again)
```

Si es queda en **FAILED loop**:
- Check WiFi credencials són correctes
- Check SSID i password guardat a M5 EEPROM (restart M5 si necesari)

## 7. Quick Checklist

- [ ] Daemon running: `ps aux | grep wifi-api`
- [ ] Daemon listening: `lsof -i :80`
- [ ] API responds: `curl http://localhost:80/api/usage`
- [ ] M5 WiFi connected: Check UI "WiFi Status" screen
- [ ] IP match: Daemon IP = `192.168.1.105` (o update firmware)
- [ ] Port match: Daemon port `80` = firmware port
- [ ] Serial logs: `screen /dev/tty.usbmodem* 115200`

## Still not working?

Envia output de:
```bash
# From Mac
ps aux | grep wifi
lsof -i :80
curl http://localhost:80/api/usage

# From M5 serial
DEBUG logs from screen
```

I descriure exactament quina és la conducta (LED color, time between blinks, UI state, etc.)
