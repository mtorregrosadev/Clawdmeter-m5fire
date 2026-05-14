#!/usr/bin/env python3
import argparse, json, time, os
from collections import defaultdict
from pathlib import Path

try:
    import serial
except ImportError:
    serial = None

OUT = Path.home()/'.clawdmeter-fire-usage.json'
CLAUDE_PROJECTS = Path.home()/'.claude'/'projects'
FIVE_HOURS = 5 * 60 * 60
SEVEN_DAYS = 7 * 24 * 60 * 60
SESSION_LIMIT_TOKENS = 214002  # Actual observed: ~160k tokens = 75% → limit = 214k
WEEKLY_LIMIT_TOKENS = 16051700 # Actual observed: ~160k tokens = 1% → limit = 16M

def find_m5_port():
    import glob
    patterns = ['/dev/tty.usbmodem*', '/dev/cu.usbmodem*', '/dev/tty.usbserial*', '/dev/cu.usbserial*']
    for pattern in patterns:
        ports = glob.glob(pattern)
        if ports:
            return sorted(ports)[-1]
    return None

def iter_usage_entries(base: Path):
    if not base.exists():
        return
    for path in sorted(base.rglob('*.jsonl')):
        try:
            for line in path.read_text(errors='ignore').splitlines():
                line = line.strip()
                if not line:
                    continue
                try:
                    obj = json.loads(line)
                except Exception:
                    continue

                if obj.get('type') != 'assistant':
                    continue

                msg = obj.get('message') or {}
                usage = msg.get('usage') or obj.get('usage') or {}
                ts = obj.get('timestamp')
                if not ts:
                    continue

                input_tokens = int(usage.get('input_tokens') or usage.get('inputTokens') or 0)
                output_tokens = int(usage.get('output_tokens') or usage.get('outputTokens') or 0)

                if input_tokens > 0 or output_tokens > 0:
                    yield {
                        'timestamp': ts,
                        'input_tokens': input_tokens,
                        'output_tokens': output_tokens,
                    }
        except Exception:
            continue

def parse_ts(ts: str) -> int:
    return int(time.mktime(time.strptime(ts[:19], '%Y-%m-%dT%H:%M:%S')))

def build_payload(base: Path):
    now = int(time.time())
    weekly_total = 0
    session_total = 0

    for e in iter_usage_entries(base):
        t = parse_ts(e['timestamp'])
        total = e['input_tokens'] + e['output_tokens']
        if now - t <= SEVEN_DAYS:
            weekly_total += total
        if now - t <= FIVE_HOURS:
            session_total += total

    session_pct = round((session_total / SESSION_LIMIT_TOKENS) * 100) if SESSION_LIMIT_TOKENS else 0
    weekly_pct = round((weekly_total / WEEKLY_LIMIT_TOKENS) * 100) if WEEKLY_LIMIT_TOKENS else 0

    session_reset = FIVE_HOURS // 60
    weekly_reset = SEVEN_DAYS // 60

    return {
        's': max(0, min(100, session_pct)),
        'sr': session_reset,
        'w': max(0, min(100, weekly_pct)),
        'wr': weekly_reset,
        'st': 'local-jsonl',
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
    ap.add_argument('--show-source', action='store_true', help='print where usage was found')
    args = ap.parse_args()

    if not CLAUDE_PROJECTS.exists():
        raise SystemExit(f'Missing Claude projects folder: {CLAUDE_PROJECTS}')

    payload = build_payload(CLAUDE_PROJECTS)
    line = json.dumps(payload, separators=(',', ':'))
    OUT.write_text(line + '\n')
    if args.show_source:
        print(f'# source={CLAUDE_PROJECTS}')
    print(line)

    port = args.port or find_m5_port()
    if port:
        write_serial(port, line)

if __name__ == '__main__':
    main()
