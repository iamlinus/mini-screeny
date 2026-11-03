"""config.toml in the project root: who the screen belongs to, which team and stock, which pages.

Re-read whenever the file changes, so edits take effect without restarting the companion.
"""
import os, threading, tomllib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(ROOT, "config.toml")

# page names in config.toml -> page numbers in the firmware (enum Page in src/main.cpp)
PAGES = {"gubben": 0, "aktie": 1, "claude": 2, "fotboll": 3}

DEFAULTS = {
    "namn": "",
    "fotboll": {"lag": "Helsingborgs IF", "kort": ""},
    "aktie": {"namn": "Kjell Group", "id": None},
    "sidor": {"visa": list(PAGES)},
}

_cache = {"mtime": None, "cfg": None, "error": None}
_lock = threading.Lock()   # the source threads all call load()


def load(log=print):
    """The current config (defaults filled in). A broken file keeps the last good one and logs why."""
    with _lock:
        return _load(log)


def _load(log):
    try:
        mtime = os.path.getmtime(PATH)
    except OSError:
        mtime = 0
    if _cache["cfg"] is not None and mtime == _cache["mtime"]:
        return _cache["cfg"]
    cfg = {k: (dict(v) if isinstance(v, dict) else v) for k, v in DEFAULTS.items()}
    try:
        if mtime:
            with open(PATH, "rb") as f:
                user = tomllib.load(f)
            for k, v in user.items():
                if isinstance(v, dict) and isinstance(cfg.get(k), dict):
                    cfg[k].update(v)
                else:
                    cfg[k] = v
        pages = [p for p in cfg["sidor"].get("visa", []) if p in PAGES] or list(PAGES)
        cfg["sidor"]["visa"] = pages
        if _cache["cfg"] is not None or _cache["error"]:
            log(f"config: loaded {PATH}")
        _cache.update(mtime=mtime, cfg=cfg, error=None)
    except (OSError, tomllib.TOMLDecodeError) as e:
        if str(e) != _cache["error"]:
            log(f"config: cannot read {PATH}: {e} (keeping the previous settings)")
        _cache.update(mtime=mtime, error=str(e))
        if _cache["cfg"] is None:
            _cache["cfg"] = cfg
    return _cache["cfg"]


def page_enabled(name, cfg=None):
    return name in (cfg or load(lambda m: None))["sidor"]["visa"]
