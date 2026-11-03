"""Stock page: the share in config.toml, from Avanza's public (keyless) endpoints.

config 'namn' is searched once (or 'id' is used as is); name, ticker, market and currency come
with the instrument, so the page needs nothing else from the config.
"""
import json, urllib.request

UA = {"User-Agent": "Mozilla/5.0"}   # Avanza wants a browser-like User-Agent


def _get(url, timeout=8):
    with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=timeout) as r:
        return json.load(r)


def search(query, n=8):
    """[(orderbook id, title)] for stocks matching the query."""
    body = json.dumps({"query": query, "searchFilter": {"types": ["STOCK"]}, "pagination": {"from": 0, "size": n}}).encode()
    req = urllib.request.Request("https://www.avanza.se/_api/search/filtered-search", data=body,
                                 headers={**UA, "Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=8) as r:
        hits = json.load(r).get("hits") or []
    return [(int(h["orderBookId"]), f'{h.get("title")} · {h.get("marketPlaceName") or ""}') for h in hits if h.get("orderBookId")]


_info = {}   # (query, id) -> instrument info


def _instrument(query, oid):
    key = (query, oid)
    if key not in _info:
        if not oid:
            hits = search(query, n=1)
            if not hits:
                raise ValueError(f"Avanza finds no stock '{query}' (search: companion.py --aktie ...)")
            oid = hits[0][0]
        d = _get(f"https://www.avanza.se/_api/market-guide/stock/{oid}")
        lst = d.get("listing") or {}
        _info[key] = {"id": int(oid), "name": (d.get("name") or query).upper()[:22],
                      "ticker": lst.get("tickerSymbol") or "", "market": lst.get("marketPlaceName") or "",
                      "cur": lst.get("currency") or ""}
    return _info[key]


QUOTE_KEYS = ["last", "change", "changePercent", "highest", "lowest", "totalVolumeTraded", "buy", "sell"]


def fetch(query, oid=None):
    ins = _instrument(query, oid)
    q = _get(f"https://www.avanza.se/_api/market-guide/stock/{ins['id']}/quote")
    if not isinstance(q.get("last"), (int, float)):
        raise ValueError("no 'last' in quote")
    out = {k: q.get(k) for k in QUOTE_KEYS}
    out["name"] = ins["name"]
    out["sub"] = " · ".join(x for x in (ins["ticker"], ins["market"]) if x)[:34]
    out["cur"] = ins["cur"][:4]
    return out
