#!/usr/bin/env python3
"""Drive the board over USB serial: send test commands, grab framebuffer dumps as PNG.

  sim.py cmd "stat" "dbg"                 send commands, print replies
  sim.py shot out.png                     dump the current frame
  sim.py scenario NAME                    run a scripted scenario, writes tools/out/NAME_*.png + contact sheet
"""
import sys, time, os
import serial
from PIL import Image, ImageDraw

PORT = "/dev/cu.usbmodem101"
OUT = os.path.join(os.path.dirname(__file__), "out")


def open_port():
    s = serial.Serial(PORT, 115200, timeout=2)
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
        if ln.startswith("ok") or ln.startswith("err") or ln.startswith("heap=") or ln.startswith("pos=") or ln.startswith("up="):
            return ln
    return "(timeout)"


def shot(s, path):
    s.reset_input_buffer()
    s.write(b"dump\n")
    t = time.time()
    while time.time() - t < 5:
        ln = s.readline()
        if ln.startswith(b"FBDUMP"):
            _, w, h = ln.decode().split()
            w, h = int(w), int(h)
            n = w * h * 2
            data = b""
            while len(data) < n and time.time() - t < 10:
                data += s.read(n - len(data))
            if len(data) < n:
                raise RuntimeError(f"short dump {len(data)}/{n}")
            img = Image.new("RGB", (w, h))
            px = img.load()
            for i in range(w * h):
                v = data[2 * i] | (data[2 * i + 1] << 8)  # little-endian RGB565
                r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
                px[i % w, i // w] = (r * 255 // 31, g * 255 // 63, b * 255 // 31)
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


def run_scenario(name):
    os.makedirs(OUT, exist_ok=True)
    s = open_port()
    print(cmd(s, "sim 1"))
    paths, labels = [], []
    print(cmd(s, "reset"))
    sens(s, gy=1)
    step(s, 1)
    for i, (label, sd, frames) in enumerate(SCENARIOS[name]()):
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
        print(shot(s, sys.argv[2]))
    elif what == "scenario":
        for n in sys.argv[2:]:
            run_scenario(n)
    elif what == "list":
        print(" ".join(SCENARIOS))
