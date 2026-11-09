"""Lagervärde page: total stock value from Airtable, split Brand Group Code E / the rest, vs yesterday.

Read-only against the Airtable REST API with a personal access token (data.records:read). The token and
the base/table/view ids live in airtable.toml in the repo root (git-ignored) or in $AIRTABLE_TOKEN etc.

Airtable keeps no history, so every fetch is written to tools/out/lager_history.json (one entry per
local date, the latest reading of that day). "Yesterday" is the newest date before today.
"""
import datetime, json, os, time, tomllib, urllib.error, urllib.parse, urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(ROOT, "airtable.toml")
HISTORY = os.path.join(ROOT, "tools", "out", "lager_history.json")
KEEP_DAYS = 120

F_VALUE = "Lagervärde (from Stock value (SKU))"     # lookup, one number per linked Stock Value record
F_LINK = "Stock value (SKU)"                        # the linked Stock Value records
F_BRAND = "Brand Group Code"                        # lookup of text
FIELDS = [F_VALUE, F_LINK, F_BRAND]
GROUP_E = "E"


class NoKey(Exception):
    pass


def settings():
    s = {}
    try:
        with open(PATH, "rb") as f:
            s = tomllib.load(f).get("airtable", {})
    except FileNotFoundError:
        pass
    env = {"token": "AIRTABLE_TOKEN", "base": "AIRTABLE_BASE", "table": "AIRTABLE_TABLE", "view": "AIRTABLE_VIEW"}
    for k, e in env.items():
        if os.environ.get(e):
            s[k] = os.environ[e]
    if not s.get("token") or not s.get("base") or not s.get("table"):
        raise NoKey("ingen Airtable-nyckel: skapa airtable.toml (se airtable.example.toml)")
    return s


def _get(s, params, timeout=20):
    url = f"https://api.airtable.com/v0/{s['base']}/{s['table']}?" + urllib.parse.urlencode(params, doseq=True)
    req = urllib.request.Request(url, headers={"Authorization": f"Bearer {s['token']}", "User-Agent": "mini-screeny-companion/1"})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return json.load(r)
    except urllib.error.HTTPError as e:
        why = {401: "nyckeln gäller inte (utgången eller fel)", 403: "nyckeln saknar åtkomst till basen/tabellen",
               404: "hittar inte basen, tabellen eller vyn", 429: "Airtable bromsar (för många anrop)"}.get(e.code, f"HTTP {e.code}")
        raise ValueError(f"Airtable: {why}") from None


def _numbers(v):
    """The real numbers in a lookup cell (an error comes back as an object, an empty cell as nothing)."""
    if isinstance(v, (int, float)) and not isinstance(v, bool):
        return [float(v)]
    if isinstance(v, list):
        return [float(x) for x in v if isinstance(x, (int, float)) and not isinstance(x, bool)]
    return []


def classify(records):
    """-> (E kr, other kr, rows, double-linked stock records skipped). Stock value counted once per record."""
    tot = {"e": 0.0, "o": 0.0}
    seen, dupes = set(), 0
    for r in records:
        f = r.get("fields") or {}
        vals = _numbers(f.get(F_VALUE))
        links = f.get(F_LINK) if isinstance(f.get(F_LINK), list) else []
        if len(links) == len(vals):
            keep = []
            for lid, v in zip(links, vals):
                if lid in seen:
                    dupes += 1
                else:
                    seen.add(lid)
                    keep.append(v)
            vals = keep
        brand = f.get(F_BRAND)
        is_e = GROUP_E in brand if isinstance(brand, list) else brand == GROUP_E
        tot["e" if is_e else "o"] += sum(vals)
    return tot["e"], tot["o"], len(records), dupes


def fetch_records(s):
    params = {"fields[]": FIELDS, "pageSize": 100}
    if s.get("view"):
        params["view"] = s["view"]
    out, offset = [], None
    while True:
        p = dict(params, **({"offset": offset} if offset else {}))
        d = _get(s, p)
        out += d.get("records") or []
        offset = d.get("offset")
        if not offset:
            return out
        time.sleep(0.25)    # Airtable allows 5 requests per second per base


def load_history():
    try:
        with open(HISTORY) as f:
            h = json.load(f)
        return h if isinstance(h, dict) else {}
    except (OSError, ValueError):
        return {}


def save_history(h):
    os.makedirs(os.path.dirname(HISTORY), exist_ok=True)
    keep = sorted(h)[-KEEP_DAYS:]
    tmp = HISTORY + ".tmp"
    with open(tmp, "w") as f:
        json.dump({k: h[k] for k in keep}, f)
    os.replace(tmp, HISTORY)


def with_baseline(hist, today, e, o):
    """The payload for the board; today's reading is recorded in hist (the caller saves it)."""
    hist[today] = {"e": round(e), "o": round(o)}
    before = [d for d in hist if d < today]
    out = {"e": round(e), "o": round(o), "t": round(e + o)}
    if before:
        d = max(before)
        out.update(pe=hist[d]["e"], po=hist[d]["o"], pd=d[8:10].lstrip("0") + "/" + d[5:7].lstrip("0"))
    return out


def fetch():
    s = settings()
    e, o, rows, dupes = classify(fetch_records(s))
    if rows == 0:
        raise ValueError("Airtable gav inga rader")
    hist = load_history()
    out = with_baseline(hist, datetime.date.today().isoformat(), e, o)
    save_history(hist)
    return out


if __name__ == "__main__":
    s = settings()
    recs = fetch_records(s)
    e, o, rows, dupes = classify(recs)
    print(f"{rows} rader, {dupes} dubbellänkade lagerposter hoppade över")
    print(f"E {e:,.0f} kr   Övriga {o:,.0f} kr   Totalt {e + o:,.0f} kr")
