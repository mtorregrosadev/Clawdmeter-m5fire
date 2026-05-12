#!/usr/bin/env python3
import argparse, json, os, ssl, sys, time, urllib.request
from pathlib import Path

CREDS = Path.home()/'.claude'/'.credentials.json'
OUT = Path.home()/'.clawdmeter-fire-usage.json'
URL = 'https://api.anthropic.com/v1/messages'


def read_token():
    data = json.loads(CREDS.read_text())
    return data.get('accessToken') or data.get('access_token')


def poll(token):
    body = {
        'model': 'claude-haiku-4-5-20251001',
        'max_tokens': 1,
        'messages': [{'role': 'user', 'content': 'hi'}],
    }
    req = urllib.request.Request(URL, data=json.dumps(body).encode(), method='POST')
    req.add_header('Authorization', f'Bearer {token}')
    req.add_header('anthropic-version', '2023-06-01')
    req.add_header('anthropic-beta', 'oauth-2025-04-20')
    req.add_header('Content-Type', 'application/json')
    req.add_header('User-Agent', 'claude-code/2.1.5')
    with urllib.request.urlopen(req, context=ssl.create_default_context()) as r:
        h = r.headers
        now = int(time.time())
        def f(name, default='0'):
            return h.get(name, default)
        s5 = float(f('anthropic-ratelimit-unified-5h-utilization', '0'))
        r5 = int(float(f('anthropic-ratelimit-unified-5h-reset', str(now))))
        s7 = float(f('anthropic-ratelimit-unified-7d-utilization', '0'))
        r7 = int(float(f('anthropic-ratelimit-unified-7d-reset', str(now))))
        st = f('anthropic-ratelimit-unified-5h-status', 'unknown')
        payload = {
            's': round(s5 * 100),
            'sr': max(0, round((r5 - now) / 60)),
            'w': round(s7 * 100),
            'wr': max(0, round((r7 - now) / 60)),
            'st': st,
            'ok': True,
        }
        return payload


def write_serial(port, line):
    with open(port, 'wb', buffering=0) as f:
        f.write((line + '\n').encode())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', help='serial device like /dev/cu.usbserial-xxxx')
    args = ap.parse_args()

    if not CREDS.exists():
        raise SystemExit(f'Missing credentials: {CREDS}')
    token = read_token()
    if not token:
        raise SystemExit('No accessToken found in Claude credentials')

    payload = poll(token)
    line = json.dumps(payload, separators=(',', ':'))
    OUT.write_text(line + '\n')
    print(line)
    if args.port:
        write_serial(args.port, line)

if __name__ == '__main__':
    main()
