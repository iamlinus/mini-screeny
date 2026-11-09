"""config.toml in the project root: who the screen belongs to, which team and stock, which pages.

Re-read whenever the file changes, so edits take effect without restarting the companion.
"""
import os, threading, tomllib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(ROOT, "config.toml")

# page names in config.toml -> page numbers in the firmware (enum Page in src/main.cpp)
PAGES = {"gubben": 0, "aktie": 1, "claude": 2, "fotboll": 3, "lager": 4}
DEFAULT_ON = ["gubben", "aktie", "claude", "fotboll"]   # lager needs an Airtable key: off until asked for

DEFAULTS = {
    "namn": "",
    "fotboll": {"lag": "Helsingborgs IF", "kort": ""},
    "aktie": {"namn": "Kjell Group", "id": None},
    "sidor": {"visa": list(DEFAULT_ON)},
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
        pages = [p for p in cfg["sidor"].get("visa", []) if p in PAGES] or list(DEFAULT_ON)
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


def _q(s):
    """A TOML basic string (JSON string escapes are valid TOML)."""
    import json
    return json.dumps(str(s), ensure_ascii=False)


def save(namn, lag, kort, aktie, aktie_id, visa):
    """Write config.toml (the same layout and comments as the hand-written file)."""
    visa = [p for p in visa if p in PAGES] or list(DEFAULT_ON)
    id_line = f"id = {int(aktie_id)}" if aktie_id else "# id = 1276742"
    text = f'''# mini screeny – inställningar
# Ändra här eller på http://localhost:8740 (companion-servern). Ändringar slår igenom inom en minut.

# Vem skärmen tillhör. Gubben säger "Hej <namn>!". Lämna tomt för bara "Hej!".
namn = {_q(namn)}

[fotboll]
# Laget som visas på fotbollssidan: namn eller förkortning från Allsvenskan eller Superettan,
# t.ex. "Helsingborgs IF", "HIF", "Malmö FF", "AIK". Lista alla: .venv/bin/python -I tools/companion.py --lag
lag = {_q(lag)}
# Kort namn för laget i matchresultatet (valfritt; annars ligans förkortning)
kort = {_q(kort)}

[aktie]
# Aktien som visas på aktiesidan: ett sökord hos Avanza, t.ex. "Kjell Group", "Volvo B", "Ericsson B".
# Sök: .venv/bin/python -I tools/companion.py --aktie "volvo"
namn = {_q(aktie)}
# Avanzas id för aktien, om sökordet hittar fel aktie (siffran i adressen på avanza.se). Valfritt.
{id_line}

[sidor]
# Vilka sidor som visas och i vilken ordning (PWR bläddrar). Möjliga: "gubben", "aktie", "claude", "fotboll", "lager" (Lagervärde, kräver airtable.toml)
visa = [{", ".join(_q(p) for p in visa)}]
'''
    tmp = PATH + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        f.write(text)
    os.replace(tmp, PATH)   # atomic: the companion never reads half a file
