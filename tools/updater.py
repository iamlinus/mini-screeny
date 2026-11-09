"""Update the companion from GitHub (git pull) from the settings page.

Nothing happens by itself: a background check (git fetch) only tells whether newer commits exist; the pull runs
when the user presses the button. Fast-forward only, so it never merges; config.toml (edited by the settings page)
is kept as it is. Firmware changes cannot be applied from here: the board has to be flashed with a USB cable.
"""
import os, shutil, signal, subprocess, threading, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LABEL = "se.mini-screeny.companion"
GIT = shutil.which("git") or "/usr/bin/git"
CHECK_EVERY_S = 6 * 3600

_lock = threading.Lock()
_state = {"checked_at": 0, "behind": 0, "commits": [], "firmware": False, "deps": False, "error": "", "busy": False}


def _git(*args, timeout=40):
    return subprocess.run([GIT, "-C", ROOT, *args], capture_output=True, text=True, timeout=timeout,
                          env={**os.environ, "GIT_TERMINAL_PROMPT": "0"})


def _last_line(r):
    lines = [l for l in (r.stderr or r.stdout or "").strip().splitlines() if l.strip()]
    return lines[-1][:160] if lines else f"git avslutade med kod {r.returncode}"


def managed():
    """True when launchd started us, so that exiting makes it start the new code."""
    return os.environ.get("XPC_SERVICE_NAME") == LABEL


def check():
    with _lock:
        s = _state
        try:
            if not os.path.isdir(os.path.join(ROOT, ".git")):
                raise RuntimeError("installationen är inte en git-klon")
            r = _git("fetch", "--quiet", "origin", "main")
            if r.returncode:
                raise RuntimeError("kunde inte nå GitHub: " + _last_line(r))
            behind = int(_git("rev-list", "--count", "HEAD..origin/main").stdout.strip() or 0)
            commits = _git("log", "--format=%s", "-n", "8", "HEAD..origin/main").stdout.strip().splitlines() if behind else []
            files = _git("diff", "--name-only", "HEAD", "origin/main").stdout.split() if behind else []
            s.update(behind=behind, commits=commits, error="",
                     firmware=any(f.startswith("src/") or f == "platformio.ini" for f in files),
                     deps="tools/requirements.txt" in files)
        except Exception as e:
            s["error"] = str(e)[:160]
        s["checked_at"] = time.time()
        return dict(s)


def info():
    s = dict(_state)
    s["managed"] = managed()
    s["checked_ago"] = int(time.time() - s["checked_at"]) if s["checked_at"] else None
    return s


def apply(log):
    """git pull --ff-only; returns {"ok", "restart", "firmware", "deps"} or {"ok": False, "error"}."""
    if _state["busy"]:
        return {"ok": False, "error": "En uppdatering pågår redan."}
    _state["busy"] = True
    try:
        st = check()
        if st["error"]:
            return {"ok": False, "error": st["error"]}
        if not st["behind"]:
            return {"ok": False, "error": "Det finns inget nytt att hämta."}
        dirty = [l[3:] for l in _git("status", "--porcelain", "--untracked-files=no").stdout.splitlines()
                 if l[3:] != "config.toml"]
        if dirty:
            return {"ok": False, "error": "Lokala ändringar hindrar uppdateringen: " + ", ".join(dirty[:4])}
        cfg_path = os.path.join(ROOT, "config.toml")
        try:
            with open(cfg_path, "rb") as f:
                cfg = f.read()
        except OSError:
            cfg = None
        _git("checkout", "--", "config.toml")
        try:
            r = _git("pull", "--ff-only", "--quiet", "origin", "main", timeout=90)
        finally:
            if cfg is not None:       # the person's own settings always win over the repo's config.toml
                with open(cfg_path, "wb") as f:
                    f.write(cfg)
        if r.returncode:
            return {"ok": False, "error": "git pull misslyckades: " + _last_line(r)}
        log(f"updated from GitHub ({st['behind']} new commits)")
        res = {"ok": True, "restart": managed(), "firmware": st["firmware"], "deps": st["deps"]}
        _state.update(behind=0, commits=[], firmware=False, deps=False)
        if res["restart"]:
            # launchd (KeepAlive) starts the new code as soon as we exit; SIGTERM makes main() clean up first
            threading.Timer(1.5, os.kill, (os.getpid(), signal.SIGTERM)).start()
        return res
    finally:
        _state["busy"] = False


def start(log):
    def loop():
        time.sleep(45)
        while True:
            check()
            time.sleep(CHECK_EVERY_S)
    threading.Thread(target=loop, daemon=True, name="updater").start()
