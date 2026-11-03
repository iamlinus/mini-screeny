#!/usr/bin/env python3
"""mini screeny companion server: the Mac fetches the data, the board only shows it.

The board has no network. This process holds its USB serial port and sends one line per data
source, "<tag> {json}". Every source runs in its own thread, so a slow fetch never holds up the port.

  cfg  name + page order from config.toml (the board stores them), every 60 s and when the file changes
  vp   Claude quota windows, read on this Mac (tools/quota.py, adapted from VibePulse), every 30 s
  stk  the stock in config.toml, Avanza's public endpoints (tools/stock.py), every 60 s
  fb   the football team in config.toml: table, next match, last result (tools/football.py), at start + hourly

Sources whose page is switched off in config.toml fetch nothing.

Each fresh result is sent once; everything is sent again on (re)connect and when the board prints
"mini-screeny boot", so a rebooted board is filled within a second. A source that fails sends
nothing: the board keeps the last values and turns its dot red once they are 5 min old. A "ping"
every 10 s tells the board the Mac is there; without it the board shows "ø".

  companion.py                    run (normally started at login by tools/companion_launchagent.sh)
  companion.py --once             one round, then exit
  companion.py --vp-fixture F     send a VibePulse-shaped JSON file instead of the live quotas
  companion.py --lag              list the teams that can go in config.toml (or use the settings page)
  companion.py --aktie volvo      search Avanza for a stock (name + id for config.toml)

Settings page: http://localhost:8740 (tools/webconfig.py) edits config.toml and shows the status.

Other tools pause it to use the port (see tools/boardport.py): sim.py does that by itself, and
`tools/sim.py flash` flashes the firmware.
Adding a source: write a fetch function returning a dict (raise or return None = no data) and add it to SOURCES.
"""
import argparse, fcntl, json, os, signal, subprocess, sys, threading, time, urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import serial
import boardport as bp
import quota
import football
import stock
import config
import webconfig

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


def fetch_stock(args):
    c = config.load(log)["aktie"]
    return stock.fetch(c.get("namn") or "Kjell Group", c.get("id"))


def fetch_football(args):
    c = config.load(log)["fotboll"]
    return football.fetch(c.get("lag") or "HIF", c.get("kort") or "")


def fetch_cfg(args):
    c = config.load(log)
    return {"name": str(c.get("namn") or "")[:20], "pages": [config.PAGES[p] for p in c["sidor"]["visa"]]}


class Source:
    def __init__(self, tag, every, fetch, page=None):
        self.tag, self.every, self.fetch, self.page = tag, every, fetch, page
        self.ok_at = 0.0          # last successful fetch (for the settings page)
        self.error = ""
        self.kick = threading.Event()   # set = fetch now (BOOT double press on the board)
        self.line = None        # last good payload, encoded; None = nothing to send
        self.version = 0        # bumped on every fresh line
        self.rounds = 0
        self.ok = None          # last fetch state, for logging changes only

    def start(self, args):
        threading.Thread(target=self._loop, args=(args,), daemon=True, name=f"src-{self.tag}").start()

    def _loop(self, args):
        while True:
            t0 = time.time()
            if self.page and not config.page_enabled(self.page):
                self.line, self.ok = None, None        # page switched off: fetch nothing
                self.rounds += 1
                time.sleep(15)
                continue
            self.refresh(args)
            self.rounds += 1
            # sleep, but wake early when config.toml changes (new team/stock/name takes effect at once)
            seen = config.load(log)
            while time.time() - t0 < self.every:
                if self.kick.wait(min(5.0, max(1.0, self.every - (time.time() - t0)))):
                    break
                if config.load(log) is not seen:
                    break
            self.kick.clear()

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
            self.ok_at, self.error = time.time(), ""
            if self.ok is not True:
                log(f"{self.tag}: data ok")
            self.ok = True
        except Exception as e:
            if self.ok is not False:
                log(f"{self.tag}: no data ({e})")
            self.ok = False
            self.error = str(e)[:80]
            self.line = None    # do not keep feeding old values as if they were fresh


_refreshed_at = 0.0


def refresh_all():
    """BOOT pressed twice on the board: fetch every page's data now (at most every 10 s)."""
    global _refreshed_at
    if time.time() - _refreshed_at < 10:
        return
    _refreshed_at = time.time()
    log("board asked for fresh data")
    _claude.request_refresh()
    for s in SOURCES:
        s.kick.set()


SOURCES = [
    Source("cfg", 60, fetch_cfg),
    Source("vp", 30, fetch_vibepulse, page="claude"),
    Source("stk", 60, fetch_stock, page="aktie"),
    Source("fb", 3600, fetch_football, page="fotboll"),   # at start, then hourly
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

    _opened_at = 0.0

    def _open_settings(self):
        # BOOT pressed three times on the board: open the settings page (at most every 5 s)
        if time.time() - self._opened_at < 5:
            return
        self._opened_at = time.time()
        log("board asked for the settings page")
        try:
            subprocess.Popen(["open", f"http://localhost:{webconfig.PORT}"])
        except OSError as e:
            log(f"cannot open the settings page: {e}")

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
            if ln == "evt settings":
                self._open_settings()
            elif ln == "evt refresh":
                refresh_all()
            elif ln == "mini-screeny boot":
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
    ap.add_argument("--lag", action="store_true", help="list the teams for config.toml")
    ap.add_argument("--aktie", metavar="SÖKORD", help="search Avanza for a stock for config.toml")
    args = ap.parse_args()
    if args.lag:
        for abbrv, name, league in football.teams():
            print(f"{abbrv:7s} {name:22s} {league}")
        return
    if args.aktie:
        for oid, title in stock.search(args.aktie):
            print(f"{oid:>9}  {title}")
        return

    lock_fd = acquire_single_instance()
    log(f"companion started (pid {os.getpid()})")
    for src in SOURCES:
        src.start(args)
    board = Board()

    def status():
        now = time.time()
        labels = {"claude": "Claude-kvoter", "aktie": "Aktien", "fotboll": "Fotboll"}
        return {
            "board": {"connected": board.ser is not None, "port": board.port if board.ser else "",
                      "paused": bool(bp.pause_holder())},
            "sources": [{"label": labels[s.page], "enabled": config.page_enabled(s.page), "ok": s.ok,
                         "age": int(now - s.ok_at) if s.ok_at else None,
                         "error": "ingen data" if s.ok is False and not s.error else s.error}
                        for s in SOURCES if s.page],
        }
    if not args.once:
        webconfig.start(status, log)
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
