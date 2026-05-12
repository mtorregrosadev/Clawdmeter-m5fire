#!/usr/bin/env python3
import argparse, json, time
from collections import defaultdict
from pathlib import Path

OUT = Path.home()/'.clawdmeter-fire-usage.json'
CLAUDE_PROJECTS = Path.home()/'.claude'/'projects'
FIVE_HOURS = 5 * 60 * 60
SEVEN_DAYS = 7 * 24 * 60 * 60
SESSION_LIMIT_TOKENS = 44000
WEEKLY_LIMIT_TOKENS = 220000


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
                usage = msg.get('usage') or {}
                ts = obj.get('timestamp')
                if not ts:
                    continue
                yield {
                    'timestamp': ts,
                    'session_id': obj.get('sessionId') or obj.get('session_id') or path.stem,
                    'input_tokens': int(usage.get('input_tokens') or 0),
                    'output_tokens': int(usage.get('output_tokens') or 0),
                    'cache_creation_tokens': int(usage.get('cache_creation_input_tokens') or 0),
                    'cache_read_tokens': int(usage.get('cache_read_input_tokens') or 0),
                }
        except Exception:
            continue


def parse_ts(ts: str) -> int:
    return int(time.mktime(time.strptime(ts[:19], '%Y-%m-%dT%H:%M:%S')))


def build_payload(base: Path):
    now = int(time.time())
    weekly_total = 0
    session_totals = defaultdict(int)
    session_last_seen = {}

    for e in iter_usage_entries(base):
        t = parse_ts(e['timestamp'])
        total = e['input_tokens'] + e['output_tokens'] + e['cache_creation_tokens'] + e['cache_read_tokens']
        if now - t <= SEVEN_DAYS:
            weekly_total += total
        if now - t <= FIVE_HOURS:
            session_totals[e['session_id']] += total
            session_last_seen[e['session_id']] = max(session_last_seen.get(e['session_id'], 0), t)

    current_session = None
    if session_last_seen:
        current_session = max(session_last_seen, key=session_last_seen.get)

    session_total = session_totals.get(current_session, 0) if current_session else 0
    session_pct = round((session_total / SESSION_LIMIT_TOKENS) * 100) if SESSION_LIMIT_TOKENS else 0
    weekly_pct = round((weekly_total / WEEKLY_LIMIT_TOKENS) * 100) if WEEKLY_LIMIT_TOKENS else 0

    session_reset = 0
    if current_session:
        elapsed = now - session_last_seen[current_session]
        session_reset = max(0, round((FIVE_HOURS - elapsed) / 60))

    weekly_reset = max(0, round((SEVEN_DAYS) / 60))

    return {
        's': max(0, min(100, session_pct)),
        'sr': session_reset,
        'w': max(0, min(100, weekly_pct)),
        'wr': weekly_reset,
        'st': 'local-jsonl',
        'ok': True,
    }


def write_serial(port, line):
    with open(port, 'wb', buffering=0) as f:
        f.write((line + '\n').encode())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', help='serial device like /dev/cu.usbserial-xxxx')
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
    if args.port:
        write_serial(args.port, line)

if __name__ == '__main__':
    main()
