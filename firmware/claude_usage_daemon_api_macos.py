#!/usr/bin/env python3
import argparse, json, time, os, subprocess
from pathlib import Path

try:
    import serial
except ImportError:
    serial = None

OUT = Path.home()/'.clawdmeter-fire-usage.json'

def find_m5_port():
    import glob
    patterns = ['/dev/tty.usbmodem*', '/dev/cu.usbmodem*', '/dev/tty.usbserial*', '/dev/cu.usbserial*']
    for pattern in patterns:
        ports = glob.glob(pattern)
        if ports:
            return sorted(ports)[-1]
    return None

def read_token():
    creds_path = Path.home() / '.claude' / '.credentials.json'
    if not creds_path.exists():
        raise SystemExit(f'Missing Claude credentials: {creds_path}')
    try:
        with open(creds_path) as f:
            data = json.load(f)
            return data.get('accessToken')
    except Exception as e:
        raise SystemExit(f'Error reading token: {e}')

def poll_api(token):
    """Call Anthropic API to get ratelimit headers."""
    cmd = [
        'curl', '-s', '-D', '-', '-o', '/dev/null',
        'https://api.anthropic.com/v1/messages',
        '-H', f'Authorization: Bearer {token}',
        '-H', 'anthropic-version: 2023-06-01',
        '-H', 'anthropic-beta: oauth-2025-04-20',
        '-H', 'Content-Type: application/json',
        '-H', 'User-Agent: claude-code/2.1.5',
        '-d', '{"model":"claude-haiku-4-5-20251001","max_tokens":1,"messages":[{"role":"user","content":"hi"}]}'
    ]

    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=10)
        headers = result.stderr + result.stdout

        # Extract ratelimit headers
        s5h_util = extract_header(headers, 'anthropic-ratelimit-unified-5h-utilization')
        s5h_reset = extract_header(headers, 'anthropic-ratelimit-unified-5h-reset')
        s7d_util = extract_header(headers, 'anthropic-ratelimit-unified-7d-utilization')
        s7d_reset = extract_header(headers, 'anthropic-ratelimit-unified-7d-reset')
        status = extract_header(headers, 'anthropic-ratelimit-unified-5h-status')

        return {
            's5h_util': float(s5h_util or 0),
            's5h_reset': int(s5h_reset or 0),
            's7d_util': float(s7d_util or 0),
            's7d_reset': int(s7d_reset or 0),
            'status': status or 'unknown'
        }
    except Exception as e:
        print(f'Error calling API: {e}')
        return None

def extract_header(headers, name):
    """Extract header value from curl -D output."""
    for line in headers.split('\n'):
        if name.lower() in line.lower():
            parts = line.split(':')
            if len(parts) >= 2:
                return parts[1].strip()
    return None

def build_payload(token):
    """Poll API and build JSON payload for M5Stack."""
    data = poll_api(token)
    if not data:
        return {'ok': False, 'st': 'api-error'}

    now = int(time.time())

    s5h_pct = round(data['s5h_util'] * 100)
    s5h_reset_mins = max(0, (data['s5h_reset'] - now) // 60) if data['s5h_reset'] > 0 else 0

    s7d_pct = round(data['s7d_util'] * 100)
    s7d_reset_mins = max(0, (data['s7d_reset'] - now) // 60) if data['s7d_reset'] > 0 else 0

    return {
        's': max(0, min(100, s5h_pct)),
        'sr': s5h_reset_mins,
        'w': max(0, min(100, s7d_pct)),
        'wr': s7d_reset_mins,
        'st': 'api',
        'ok': True,
    }

def write_serial(port, line):
    if not serial:
        return False

    try:
        ser = serial.Serial(port, 115200, timeout=1)
        time.sleep(0.1)
        ser.write((line + '\n').encode())
        time.sleep(0.1)
        ser.close()
        return True
    except Exception as e:
        return False

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', help='serial device like /dev/cu.usbserial-xxxx (auto-detect if omitted)')
    ap.add_argument('--show-source', action='store_true', help='print data source')
    args = ap.parse_args()

    token = read_token()
    payload = build_payload(token)
    line = json.dumps(payload, separators=(',', ':'))
    OUT.write_text(line + '\n')

    if args.show_source:
        print(f'# source=anthropic-api')
    print(line)

    port = args.port or find_m5_port()
    if port:
        write_serial(port, line)

if __name__ == '__main__':
    main()
