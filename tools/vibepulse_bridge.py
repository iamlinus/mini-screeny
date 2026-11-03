#!/usr/bin/env python3
"""Feed the mini screeny VibePulse page over USB, so the board needs no Wi-Fi.

Polls the VibePulse tokenserver on this Mac (http://localhost:8737/api/tokens) and sends
the quota fields to the board as one serial line: "vp {json}".

  vibepulse_bridge.py                         poll every 30 s, port auto-detected
  vibepulse_bridge.py --fixture tokens.json   send a file instead (test without the server)
  vibepulse_bridge.py --once                  send once and exit

The tokenserver itself comes from https://github.com/niclasvestlund-YT/vibepulse
(python3 tools/tokenserver/tokenserver.py). sim.py and this bridge cannot hold the port at the same time.
"""
import argparse, glob, json, sys, threading, time, urllib.request

import serial

KEYS = [
    "claudeSessionPct", "claudeSessionResetMin",
    "claudeWeekPct", "claudeWeekResetMin",
    "claudeModelWeekPct", "claudeModelWeekResetMin", "claudeModelWeekLabel",
    "codexSessionPct", "codexSessionResetMin",
    "codexWeekPct", "codexWeekResetMin",
]
MAX_LINE = 700  # board line buffer is 768 incl. "vp "


def find_port():
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    return ports[0] if ports else None


def fetch(url, fixture):
    if fixture:
        with open(fixture) as f:
            return json.load(f)
    with urllib.request.urlopen(url, timeout=10) as r:
        return json.load(r)


def to_line(payload):
    # Only the fields the page draws. A missing or null value stays null: the board shows dashes, never 0.
    sub = {k: payload.get(k) for k in KEYS}
    lbl = sub.get("claudeModelWeekLabel")
    if isinstance(lbl, str):
        sub["claudeModelWeekLabel"] = lbl[:22]
    body = json.dumps(sub, separators=(",", ":"), ensure_ascii=False)
    if len(body.encode()) > MAX_LINE:
        raise ValueError(f"line too long ({len(body)} bytes)")
    return f"vp {body}\n".encode()


def reader(ser, stop):
    # Keep draining the board's output; an unread USB CDC buffer slows the board's loop down.
    while not stop.is_set():
        try:
            ln = ser.readline().decode(errors="replace").strip()
        except Exception:
            return
        if ln.startswith(("ok vp", "err vp")):
            print("  board:", ln, flush=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--url", default="http://localhost:8737/api/tokens")
    ap.add_argument("--port", help="serial port (default: first /dev/cu.usbmodem*)")
    ap.add_argument("--every", type=float, default=30, help="seconds between polls (default 30)")
    ap.add_argument("--fixture", help="send this JSON file instead of polling the server")
    ap.add_argument("--once", action="store_true")
    a = ap.parse_args()

    ser = None
    stop = threading.Event()
    while True:
        if ser is None:
            port = a.port or find_port()
            if not port:
                print("no board found (/dev/cu.usbmodem*), retrying ...", flush=True)
                time.sleep(5)
                continue
            try:
                ser = serial.Serial(port, 115200, timeout=1)
                stop.clear()
                threading.Thread(target=reader, args=(ser, stop), daemon=True).start()
                print(f"connected to {port}", flush=True)
                time.sleep(0.3)
            except serial.SerialException as e:
                print(f"cannot open {port}: {e}", flush=True)
                ser = None
                time.sleep(5)
                continue

        try:
            line = to_line(fetch(a.url, a.fixture))
        except Exception as e:
            # Send nothing: the board keeps the last good values and turns stale after 120 s.
            print(f"{time.strftime('%H:%M:%S')} no data: {e}", flush=True)
            line = None

        if line:
            try:
                ser.write(line)
                ser.flush()
                print(f"{time.strftime('%H:%M:%S')} sent {len(line)} bytes", flush=True)
            except serial.SerialException as e:
                print(f"board gone: {e}", flush=True)
                stop.set()
                try:
                    ser.close()
                except Exception:
                    pass
                ser = None
                time.sleep(2)
                continue

        if a.once:
            time.sleep(1)  # let the ack arrive
            return
        time.sleep(a.every)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(0)
