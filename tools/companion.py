#!/usr/bin/env python3
"""mini screeny companion server: the Mac fetches the data, the board only shows it.

The board has no network. This process holds its USB serial port and sends one line per data
source, "<tag> {json}". Every source runs in its own thread, so a slow fetch never holds up the port.

  vp   Claude quota windows, read on this Mac (tools/quota.py, adapted from VibePulse), every 30 s
  stk  Kjell Group quote from Avanza's public quote endpoint, every 60 s
  hif  Helsingborgs IF table position, next match, last result (tools/football.py), at start + hourly

Each fresh result is sent once; everything is sent again on (re)connect and when the board prints
"mini-screeny boot", so a rebooted board is filled within a second. A source that fails sends
nothing: the board keeps the last values and turns its dot red once they are 5 min old. A "ping"
every 10 s tells the board the Mac is there; without it the board shows "ø".

  companion.py                    run (normally started at login by tools/companion_launchagent.sh)
  companion.py --once             one round, then exit
  companion.py --vp-fixture F     send a VibePulse-shaped JSON file instead of the live quotas

Other tools pause it to use the port (see tools/boardport.py): sim.py does that by itself, and
`tools/sim.py flash` flashes the firmware.
Adding a source: write a fetch function returning a dict (raise or return None = no data) and add it to SOURCES.
"""
import argparse, fcntl, json, os, signal, sys, threading, time, urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import serial
import boardport as bp
import quota
import football

MAX_LINE = 700        # the board's line buffer is 768 bytes including the tag
PING_EVERY_S = 10     # keep-alive: the board shows "ø" after 25 s without any line from us
UA = "mini-screeny-companion/1"


def log(msg):
    print(f"{time.strftime('%Y-%m-%d %H:%M:%S')} {msg}", flush=True)


def get_json(url, timeout=8, headers=None):
    req = urllib.request.Request(url, headers={"User-Agent": UA, **(headers or {})})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.load(r)


# ---------------- sources ----------------
VP_KEYS = [
    "claudeSessionPct", "claudeSessionResetMin",
    "claudeWeekPct", "claudeWeekResetMin",
    "claudeModelWeekPct", "claudeModelWeekResetMin", "claudeModelWeekLabel",
    "claudeAgeSec",
]
_claude = quota.ClaudeUsage(os.path.join(bp.OUT, ".claude_probe_state.json"), log)


def fetch_vibepulse(args):
    if args.vp_fixture:
        with open(args.vp_fixture) as f:
            payload = json.load(f)
    else:
        _claude.poll()      # rate-limited inside: the usage endpoint is asked every 4 min at most
        payload = _claude.values()
        if not payload:
            return None
    # Only the fields the page draws. Missing stays null: the board shows dashes, never 0.
    sub = {k: payload.get(k) for k in VP_KEYS}
    if isinstance(sub.get("claudeModelWeekLabel"), str):
        sub["claudeModelWeekLabel"] = sub["claudeModelWeekLabel"][:22]
    return sub


STOCK_KEYS = ["last", "change", "changePercent", "highest", "lowest", "totalVolumeTraded", "buy", "sell"]


def fetch_stock(args):
    # Avanza's public quote for Kjell Group (orderbook 1276742); it wants a browser-like User-Agent.
    q = get_json("https://www.avanza.se/_api/market-guide/stock/1276742/quote", headers={"User-Agent": "Mozilla/5.0"})
    if not isinstance(q.get("last"), (int, float)):
        raise ValueError("no 'last' in quote")
    return {k: q.get(k) for k in STOCK_KEYS}


class Source:
    def __init__(self, tag, every, fetch):
        self.tag, self.every, self.fetch = tag, every, fetch
        self.line = None        # last good payload, encoded; None = nothing to send
        self.version = 0        # bumped on every fresh line
        self.rounds = 0
        self.ok = None          # last fetch state, for logging changes only

    def start(self, args):
        threading.Thread(target=self._loop, args=(args,), daemon=True, name=f"src-{self.tag}").start()

    def _loop(self, args):
        while True:
            t0 = time.time()
            self.refresh(args)
            self.rounds += 1
            time.sleep(max(1.0, self.every - (time.time() - t0)))

    def refresh(self, args):
        try:
            data = self.fetch(args)
            if data is None:
                raise ValueError("nothing available")
            body = json.dumps(data, separators=(",", ":"), ensure_ascii=False)
            if len(body.encode()) + len(self.tag) + 2 > MAX_LINE:
                raise ValueError(f"line too long ({len(body)} bytes)")
            self.line = f"{self.tag} {body}\n".encode()
            self.version += 1
            if self.ok is not True:
                log(f"{self.tag}: data ok")
            self.ok = True
        except Exception as e:
            if self.ok is not False:
                log(f"{self.tag}: no data ({e})")
            self.ok = False
            self.line = None    # do not keep feeding old values as if they were fresh


SOURCES = [
    Source("vp", 30, fetch_vibepulse),
    Source("stk", 60, fetch_stock),
    Source("hif", 3600, lambda args: football.fetch()),   # at start, then hourly
]


# ---------------- board link ----------------
class Board:
    def __init__(self):
        self.ser = None
        self.port = None
        self.lost = threading.Event()
        self.booted = threading.Event()

    def connect(self):
        port = bp.find_port()
        if not port:
            return False
        try:
            self.ser = bp.open_serial(port, timeout=1)
        except (serial.SerialException, OSError) as e:
            log(f"cannot open {port}: {e}")
            return False
        self.port = port
        self.lost.clear()
        threading.Thread(target=self._reader, args=(self.ser,), daemon=True).start()
        log(f"connected to {port}")
        self.send(b"hello\n")
        return True

    def close(self):
        if self.ser:
            try:
                self.ser.close()
            except Exception:
                pass
            log(f"released {self.port}")
        self.ser = None

    def send(self, line):
        try:
            self.ser.write(line)
            self.ser.flush()
            return True
        except (serial.SerialException, OSError) as e:
            log(f"board gone: {e}")
            self.lost.set()
            return False

    def _reader(self, ser):
        # Keep draining the board's output; an unread USB CDC buffer slows the board's loop down.
        while True:
            try:
                ln = ser.readline().decode(errors="replace").strip()
            except Exception:
                if ser is self.ser:
                    self.lost.set()
                return
            if not ln:
                continue
            if ln == "mini-screeny boot":
                log("board booted, resending everything")
                self.booted.set()
            elif ln.startswith(("ok hello", "err", "wifi credentials")):
                log(f"board: {ln}")


def acquire_single_instance():
    os.makedirs(bp.OUT, exist_ok=True)
    fd = os.open(bp.LOCK, os.O_RDWR | os.O_CREAT, 0o644)
    try:
        fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        log("another companion is running, waiting for it to stop")
        fcntl.flock(fd, fcntl.LOCK_EX)
    os.ftruncate(fd, 0)
    os.write(fd, str(os.getpid()).encode())
    return fd   # keep it open for the life of the process


def set_ack(on):
    try:
        if on:
            with open(bp.ACK, "w") as f:
                f.write(str(os.getpid()))
        elif os.path.exists(bp.ACK):
            os.remove(bp.ACK)
    except OSError:
        pass


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--once", action="store_true", help="one round, then exit")
    ap.add_argument("--vp-fixture", help="VibePulse-shaped JSON file to send instead of the live quotas")
    args = ap.parse_args()

    lock_fd = acquire_single_instance()
    log(f"companion started (pid {os.getpid()})")
    for src in SOURCES:
        src.start(args)
    board = Board()
    sent = {}                   # tag -> version last sent on this connection
    last_tx = 0.0
    waiting_logged = False
    try:
        while True:
            # someone else (sim.py, flashing) needs the port
            if bp.pause_holder():
                if board.ser:
                    board.close()
                set_ack(True)
                time.sleep(0.2)
                continue
            if os.path.exists(bp.PAUSE):   # left behind by a process that died
                try:
                    os.remove(bp.PAUSE)
                except OSError:
                    pass
            set_ack(False)

            if board.ser and board.lost.is_set():
                board.close()
                time.sleep(1)
            if not board.ser:
                if not board.connect():
                    if not waiting_logged:
                        log("waiting for the board ...")
                        waiting_logged = True
                    time.sleep(2)
                    continue
                waiting_logged = False
                sent.clear()            # fresh connection: send everything we have

            if board.booted.is_set():
                board.booted.clear()
                sent.clear()
            for src in SOURCES:
                ver, line = src.version, src.line    # version first: a racing update is just resent
                if line and sent.get(src.tag) != ver:
                    if board.send(line):
                        sent[src.tag] = ver
                        last_tx = time.time()
            if time.time() - last_tx >= PING_EVERY_S and board.send(b"ping\n"):
                last_tx = time.time()

            if args.once and all(src.rounds for src in SOURCES):
                time.sleep(1.5)         # let the acks arrive
                return
            time.sleep(0.25)
    finally:
        board.close()
        set_ack(False)
        os.close(lock_fd)


if __name__ == "__main__":
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))   # launchd stops us with SIGTERM: clean up via finally
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(0)
