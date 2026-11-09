#!/usr/bin/env python3
"""Drive the board over USB serial: send test commands, grab framebuffer dumps as PNG.

  sim.py cmd "stat" "dbg"                 send commands, print replies
  sim.py shot out.png [raw]               dump the current frame as seen in the mount (raw = unrotated panel)
  sim.py scenario NAME                    run a scripted scenario, writes tools/out/NAME_*.png + contact sheet
  sim.py pulse                            VibePulse page with test payloads -> tools/out/pulse_sheet.png
  sim.py fb                               football page with test payloads -> tools/out/fb_sheet.png
  sim.py flash                            build + flash the firmware (pauses the companion meanwhile)

If tools/companion.py is running it is paused automatically while sim.py holds the port.

The firmware draws on a canvas rotated by MOUNT_ROT (board.h); the dump is the raw panel framebuffer
plus that rotation, and shot() turns it back so the PNG looks like the mounted screen.
"""
import sys, time, os, atexit, subprocess
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import boardport as bp
from PIL import Image, ImageDraw

# The board's usbmodem name depends on the USB port/hub (laptop: usbmodem101, Dell monitor hub:
# usbmodem111201); boardport finds it by its Espressif USB id. Override with SIM_PORT.
OUT = bp.OUT
_pause = None


def pause_companion():
    """Make tools/companion.py let go of the port until this process exits."""
    global _pause
    if _pause is None:
        _pause = bp.CompanionPaused().__enter__()
        atexit.register(_pause.__exit__, None, None, None)


def port_or_die():
    port = bp.find_port()
    if not port:
        sys.exit("no board found (is it plugged in?)")
    return port


def open_port():
    pause_companion()
    s = bp.open_serial(port_or_die(), timeout=2)
    time.sleep(0.2)
    s.reset_input_buffer()
    return s


def cmd(s, c, wait_ok=True, timeout=6.0):
    s.write((c + "\n").encode())
    if not wait_ok:
        return ""
    t = time.time()
    while time.time() - t < timeout:
        ln = s.readline().decode(errors="replace").strip()
        if ln.startswith(("ok", "err", "heap=", "pos=", "up=", "type=", "vp any=", "stk any=", "lag any=")):
            return ln
    return "(timeout)"


def fb_index(x, y, rot, pw, ph):
    """Panel framebuffer index for logical pixel (x, y); mirrors fbIndex() in board.h."""
    lw, lh = (ph, pw) if rot & 1 else (pw, ph)
    if rot == 1: return x * pw + (lh - 1 - y)
    if rot == 2: return (lh - 1 - y) * pw + (lw - 1 - x)
    if rot == 3: return (lw - 1 - x) * pw + y
    return y * pw + x


def shot(s, path, raw=False):
    s.reset_input_buffer()
    s.write(b"dump\n")
    t = time.time()
    while time.time() - t < 5:
        ln = s.readline()
        if ln.startswith(b"FBDUMP"):
            parts = ln.decode().split()
            w, h = int(parts[1]), int(parts[2])
            rot = 0 if raw or len(parts) < 4 else int(parts[3])
            n = w * h * 2
            data = b""
            while len(data) < n and time.time() - t < 10:
                data += s.read(n - len(data))
            if len(data) < n:
                raise RuntimeError(f"short dump {len(data)}/{n}")
            lw, lh = (h, w) if rot & 1 else (w, h)
            img = Image.new("RGB", (lw, lh))
            px = img.load()
            for y in range(lh):
                for x in range(lw):
                    i = fb_index(x, y, rot, w, h)
                    v = data[2 * i] | (data[2 * i + 1] << 8)  # little-endian RGB565
                    r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
                    px[x, y] = (r * 255 // 31, g * 255 // 63, b * 255 // 31)
            img.save(path)
            return path
    raise RuntimeError("no FBDUMP header")


def sens(s, gx=0, gy=1, gz=0, wz=0, wmag=0, jerk=0, jx=0, jy=0, ff=0, still=0):
    return cmd(s, f"sens {gx} {gy} {gz} {wz} {wmag} {jerk} {jx} {jy} {ff} {still}")


def step(s, n):
    return cmd(s, f"step {n}")


def contact(paths, labels, out, cols=4):
    ims = [Image.open(p) for p in paths]
    w, h = ims[0].size
    rows = (len(ims) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * (w + 8) + 8, rows * (h + 26) + 8), (40, 40, 40))
    d = ImageDraw.Draw(sheet)
    for i, (im, lb) in enumerate(zip(ims, labels)):
        x, y = 8 + (i % cols) * (w + 8), 8 + (i // cols) * (h + 26)
        sheet.paste(im, (x, y + 18))
        d.text((x, y + 2), lb, fill=(230, 230, 230))
    sheet.save(out)
    return out


# ---- scenarios: list of (label, sensors dict, frames) ; a frame is captured after each entry ----
import math

def sc_tilt():
    seq = [("upright", dict(gy=1), 45)]
    for deg in (20, 40, 60, 90, 135, 180):
        a = math.radians(deg)
        seq.append((f"tilt {deg}", dict(gx=-math.sin(a), gy=math.cos(a)), 20))
    seq.append(("180 settle", dict(gx=0, gy=-1), 40))
    return seq

def sc_flip():
    return [("upright", dict(gy=1), 40), ("flip 3f", dict(gy=-1), 3), ("flip 8f", dict(gy=-1), 5),
            ("flip 14f", dict(gy=-1), 6), ("flip 22f", dict(gy=-1), 8), ("flip 40f", dict(gy=-1), 18),
            ("back", dict(gy=1), 10), ("back settle", dict(gy=1), 40)]

def sc_spin():
    seq = [("upright", dict(gy=1), 30)]
    ang = 0
    for i in range(8):
        frames = 15
        for f in range(frames):
            ang += math.radians(400 / 30)
        seq.append((f"spin {i}", dict(gx=-math.sin(ang), gy=math.cos(ang), wz=400, wmag=420), frames))
    seq += [("stop", dict(gy=1), 20), ("recover 2s", dict(gy=1), 60), ("recover 6s", dict(gy=1), 120)]
    return seq

def sc_shake():
    seq = [("calm", dict(gy=1), 30)]
    for i in range(10):
        sgn = 1 if i % 2 else -1
        seq.append((f"shake {i}", dict(gy=1, jerk=2.2, jx=1.8 * sgn, jy=0.6), 4))
    seq += [("after", dict(gy=1), 15), ("after 3s", dict(gy=1), 90)]
    return seq

def sc_angry():
    seq = [("calm", dict(gy=1), 20)]
    for i in range(40):
        sgn = 1 if i % 2 else -1
        seq.append((f"rage {i}", dict(gy=1, jerk=2.8, jx=2.0 * sgn, jy=1.0 * -sgn), 3))
    seq += [("cooldown", dict(gy=1), 30)]
    return seq

def sc_tap():
    return [("calm", dict(gy=1), 30), ("tap", dict(gy=1, jerk=1.2, jx=0, jy=0.3), 1), ("t+3", dict(gy=1), 3),
            ("t+8", dict(gy=1), 5), ("t+15", dict(gy=1), 7), ("t+30", dict(gy=1), 15), ("t+60", dict(gy=1), 30)]

def sc_fall():
    return [("calm", dict(gy=1), 30), ("freefall", dict(gx=0, gy=0, ff=150), 10), ("freefall 2", dict(gx=0, gy=0, ff=400), 10),
            ("caught", dict(gy=1, jerk=2.0, jy=-1.5), 1), ("after", dict(gy=1), 20), ("after2", dict(gy=1), 40)]

def sc_dark():
    return [("calm", dict(gy=1), 20), ("facedown", dict(gx=0, gy=0.1, gz=-1), 40), ("back up", dict(gy=0.6, gz=0.8), 10),
            ("later", dict(gy=0.6, gz=0.8), 30)]

def sc_sleep():
    return [("calm", dict(gy=1, still=1), 30), ("still 20s", dict(gy=1, still=1), 570), ("yawn", dict(gy=1, still=20), 90),
            ("asleep", dict(gy=1, still=23), 150), ("asleep2", dict(gy=1, still=28), 40), ("wake", dict(gy=1, jerk=0.3, jx=0.2), 1),
            ("woken", dict(gy=1), 20)]

def sc_flat():
    return [("flat", dict(gx=0, gy=0, gz=1), 40), ("flat tilt L", dict(gx=-0.3, gy=0, gz=0.95), 30),
            ("flat tilt R", dict(gx=0.3, gy=0, gz=0.95), 40), ("flat tilt fwd", dict(gx=0, gy=-0.3, gz=0.95), 40)]

def sc_dance():
    return [("calm", dict(gy=1), 30), ("tap1", dict(gy=1, jerk=1.2, jy=0.2), 1), ("gap", dict(gy=1), 9),
            ("tap2", dict(gy=1, jerk=1.2, jy=0.2), 1), ("d+0.3s", dict(gy=1), 9), ("d+1s", dict(gy=1), 20),
            ("d+2s", dict(gy=1), 30), ("d+3s", dict(gy=1), 30), ("after", dict(gy=1), 60)]

def sc_rock():
    seq = [("calm", dict(gy=1), 30)]
    for i in range(12):
        a = math.radians(25 * (1 if i % 2 else -1))
        seq.append((f"rock {i}", dict(gx=-math.sin(a), gy=math.cos(a)), 12))
    seq.append(("after", dict(gy=1), 30))
    return seq

def sc_blood():
    return [("calm", dict(gy=1), 30), ("flip", dict(gy=-1), 45), ("hang 3s", dict(gy=-1), 90), ("hang 6s", dict(gy=-1), 90),
            ("hang 8s", dict(gy=-1), 60), ("back", dict(gy=1), 45), ("later", dict(gy=1), 60)]

def sc_idle():
    return [(f"idle {i}", dict(gy=1), 60) for i in range(8)]

SCENARIOS = {k[3:]: v for k, v in globals().items() if k.startswith("sc_")}


# VibePulse page (page 2): fixed payloads in the /api/tokens v2 shape, no tokenserver needed.
PULSE_CASES = [
    ("no model", {"claudeSessionPct": 14.0, "claudeSessionResetMin": 123, "claudeWeekPct": 10.0, "claudeWeekResetMin": 6853}),
    ("model", {"claudeSessionPct": 21.0, "claudeSessionResetMin": 256, "claudeWeekPct": 47.0, "claudeWeekResetMin": 9120,
               "claudeModelWeekPct": 73.0, "claudeModelWeekResetMin": 3120, "claudeModelWeekLabel": "FABLE · VECKA"}),
    ("high", {"claudeSessionPct": 94.0, "claudeSessionResetMin": 38, "claudeWeekPct": 81.0, "claudeWeekResetMin": 1500,
              "claudeModelWeekPct": 99.6, "claudeModelWeekResetMin": 700, "claudeModelWeekLabel": "FABLE · VECKA"}),
    ("missing", {"claudeSessionPct": None, "claudeWeekPct": None, "claudeModelWeekPct": None}),
]


# Football page (page 3)
FB_CASES = [
    ("borta-förlust", {"me": "HIF", "table": {"pos": 11, "of": 16, "league": "Superettan", "pts": 32, "gp": 29},
                       "last": {"opp": "Öster", "home": False, "gf": 0, "ga": 3, "date": "2 nov"},
                       "next": {"opp": "Falkenberg", "home": True, "days": 4, "when": "lör 7 nov 15:00"}}),
    ("idag-vinst", {"me": "HIF", "table": {"pos": 3, "of": 16, "league": "Allsvenskan", "pts": 51, "gp": 25},
                    "last": {"opp": "Malmö", "home": True, "gf": 2, "ga": 1, "date": "28 sep"},
                    "next": {"opp": "Djurgården", "home": False, "days": 0, "when": "sön 5 okt 17:30"}}),
    ("imorgon-oavgjort", {"me": "HIF", "table": {"pos": 1, "of": 16, "league": "Superettan", "pts": 65, "gp": 29},
                          "last": {"opp": "Nordic United", "home": False, "gf": 1, "ga": 1, "date": "12 okt"},
                          "next": {"opp": "Ljungskile", "home": True, "days": 1, "when": "lör 18 okt 13:00"}}),
    ("säsong slut", {"me": "HIF", "table": {"pos": 11, "of": 16, "league": "Superettan", "pts": 35, "gp": 30},
                     "last": {"opp": "Falkenberg", "home": True, "gf": 2, "ga": 0, "date": "7 nov"}}),
]


def run_fb():
    import json
    os.makedirs(OUT, exist_ok=True)
    s = open_port()
    print(cmd(s, "page 3"))
    paths, labels = [], []
    for name, payload in FB_CASES:
        print(name, cmd(s, "fb " + json.dumps(payload, separators=(",", ":"), ensure_ascii=False)))
        time.sleep(0.4)
        p = os.path.join(OUT, f"fb_{name}.png")
        shot(s, p)
        paths.append(p); labels.append(name)
    out = contact(paths, labels, os.path.join(OUT, "fb_sheet.png"), cols=2)
    print("sheet:", out)


# Invented numbers (never real stock values: the repo is public). e/o = kr now, pe/po = kr at the baseline.
LAGER_CASES = [
    ("ned", {"e": 4200000, "o": 7650000, "t": 11850000, "pe": 4310000, "po": 7700000, "pd": "8/11"}),
    ("upp", {"e": 4400000, "o": 7800000, "t": 12200000, "pe": 4310000, "po": 7700000, "pd": "6/11"}),
    ("lika", {"e": 4310000, "o": 7700000, "t": 12010000, "pe": 4310000, "po": 7700000, "pd": "8/11"}),
    ("första-dagen", {"e": 4310000, "o": 7700000, "t": 12010000}),
    ("under-10-Mkr", {"e": 830000, "o": 6200000, "t": 7030000, "pe": 900000, "po": 6100000, "pd": "8/11"}),
]


def run_lager():
    import json
    os.makedirs(OUT, exist_ok=True)
    s = open_port()
    print(cmd(s, "page 4"))
    paths, labels = [], []
    for name, payload in LAGER_CASES:
        print(name, cmd(s, "lag " + json.dumps(payload, separators=(",", ":"), ensure_ascii=False)))
        time.sleep(0.4)
        p = os.path.join(OUT, f"lager_{name}.png")
        shot(s, p)
        paths.append(p); labels.append(name)
    out = contact(paths, labels, os.path.join(OUT, "lager_sheet.png"), cols=3)
    print("sheet:", out)


def run_pulse():
    import json
    os.makedirs(OUT, exist_ok=True)
    s = open_port()
    print(cmd(s, "page 2"))
    paths, labels = [], []
    for name, payload in PULSE_CASES:
        print(name, cmd(s, "vp " + json.dumps(payload, separators=(",", ":"), ensure_ascii=False)))
        time.sleep(1.3)  # the page redraws at most once a second
        p = os.path.join(OUT, f"pulse_{name}.png")
        shot(s, p)
        paths.append(p); labels.append(name)
    out = contact(paths, labels, os.path.join(OUT, "pulse_sheet.png"), cols=4)
    print("sheet:", out)


def run_scenario(name):
    os.makedirs(OUT, exist_ok=True)
    s = open_port()
    print(cmd(s, "page 0"))   # buddy scenarios; do not depend on which page was left open
    print(cmd(s, "sim 1"))
    paths, labels = [], []
    print(cmd(s, "reset"))
    sens(s, gy=1)
    step(s, 1)
    for i, (label, sd, frames) in enumerate(SCENARIOS[name]()):
        if label.startswith("@"):
            print(label, cmd(s, label[1:]))
            continue
        sens(s, **sd)
        r = step(s, frames)
        p = os.path.join(OUT, f"{name}_{i:02d}.png")
        shot(s, p)
        paths.append(p); labels.append(label)
        print(f"{label:14s} {r}")
    print(cmd(s, "sim 0"))
    out = contact(paths, labels, os.path.join(OUT, f"{name}_sheet.png"))
    print("sheet:", out)


if __name__ == "__main__":
    what = sys.argv[1]
    if what == "cmd":
        s = open_port()
        for c in sys.argv[2:]:
            print(c, "->", cmd(s, c))
    elif what == "shot":
        s = open_port()
        print(shot(s, sys.argv[2], raw=len(sys.argv) > 3 and sys.argv[3] == "raw"))
    elif what == "scenario":
        for n in sys.argv[2:]:
            run_scenario(n)
    elif what == "pulse":
        run_pulse()
    elif what == "fb":
        run_fb()
    elif what == "lager":
        run_lager()
    elif what == "flash":
        pause_companion()
        root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        sys.exit(subprocess.call([os.path.join(root, ".venv/bin/pio"), "run", "-e", "lcd169", "-t", "upload",
                                  "--upload-port", port_or_die()], cwd=root))
    elif what == "list":
        print(" ".join(SCENARIOS))
