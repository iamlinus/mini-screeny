"""Settings page for mini screeny, served by the companion on http://localhost:8740 (this Mac only).

  GET  /              the page (tools/webconfig.html)
  GET  /api/state     current settings + status (board connected?, how fresh each page's data is)
  GET  /api/teams     teams in Allsvenskan/Superettan for the team picker
  GET  /api/stocks?q= Avanza stock search
  POST /api/update    {"action":"check"} look for news on GitHub, otherwise git pull (tools/updater.py)
  POST /api/config    save settings -> config.toml (the companion picks the change up by itself)

Only reachable from this Mac (bound to 127.0.0.1). The Host header must be localhost (no DNS
rebinding), and a save must be a JSON POST from this page's own origin, so another web page open
in the browser cannot change the settings.
"""
import json, os, threading, time, urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import config
import football
import lager
import updater
import stock

PORT = 8740
HERE = os.path.dirname(os.path.abspath(__file__))
PAGE_LABELS = {"gubben": "Gubben", "aktie": "Aktien", "claude": "Claude-kvoter", "fotboll": "Fotboll", "lager": "Lagervärde"}

_teams = {"at": 0, "list": []}


def _teams_cached():
    if time.time() - _teams["at"] > 6 * 3600 or not _teams["list"]:
        _teams["list"] = [{"abbrv": a, "name": n, "league": lg} for a, n, lg in football.teams()]
        _teams["at"] = time.time()
    return _teams["list"]


def make_handler(status_fn, log):
    allowed_hosts = {f"localhost:{PORT}", f"127.0.0.1:{PORT}"}
    allowed_origins = {f"http://{h}" for h in allowed_hosts}

    class H(BaseHTTPRequestHandler):
        def log_message(self, *a):
            pass   # keep the companion log quiet

        def _send(self, code, body, ctype="application/json; charset=utf-8"):
            data = body if isinstance(body, bytes) else json.dumps(body, ensure_ascii=False).encode()
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Frame-Options", "DENY")
            self.end_headers()
            self.wfile.write(data)

        def _host_ok(self):
            if self.headers.get("Host", "") not in allowed_hosts:
                self._send(403, {"error": "fel värd"})
                return False
            return True

        def do_GET(self):
            if not self._host_ok():
                return
            url = urllib.parse.urlparse(self.path)
            try:
                if url.path == "/":
                    with open(os.path.join(HERE, "webconfig.html"), "rb") as f:
                        self._send(200, f.read(), "text/html; charset=utf-8")
                elif url.path == "/api/state":
                    c = config.load(log)
                    self._send(200, {"config": c, "airtable": lager.public_settings(), "update": updater.info(), "pages": [{"id": k, "label": PAGE_LABELS[k]} for k in config.PAGES],
                                     "status": status_fn()})
                elif url.path == "/api/teams":
                    self._send(200, {"teams": _teams_cached()})
                elif url.path == "/api/stocks":
                    q = (urllib.parse.parse_qs(url.query).get("q") or [""])[0].strip()
                    hits = stock.search(q, n=8) if len(q) >= 2 else []
                    self._send(200, {"hits": [{"id": i, "title": t} for i, t in hits]})
                else:
                    self._send(404, {"error": "finns inte"})
            except Exception as e:
                self._send(502, {"error": f"{type(e).__name__}: {e}"})

        def do_POST(self):
            if not self._host_ok():
                return
            if self.path not in ("/api/config", "/api/update"):
                return self._send(404, {"error": "finns inte"})
            if self.headers.get("Origin") not in allowed_origins or \
                    not self.headers.get("Content-Type", "").startswith("application/json"):
                return self._send(403, {"error": "spara bara från inställningssidan"})
            try:
                n = int(self.headers.get("Content-Length", 0))
                if n > 10000:
                    return self._send(413, {"error": "för stort"})
                d = json.loads(self.rfile.read(n) or b"{}")
                if self.path == "/api/update":
                    if d.get("action") == "check":
                        updater.check()
                        return self._send(200, {"ok": True, "update": updater.info()})
                    res = updater.apply(log)
                    return self._send(200 if res["ok"] else 409, res if res["ok"] else {"error": res["error"]} | {"ok": False})
                namn = str(d.get("namn", "")).strip()[:20]
                lag = str(d.get("lag", "")).strip()[:40]
                kort = str(d.get("kort", "")).strip()[:8]
                aktie = str(d.get("aktie", "")).strip()[:40]
                aktie_id = d.get("aktie_id")
                aktie_id = int(aktie_id) if str(aktie_id or "").isdigit() else None
                visa = [p for p in d.get("visa", []) if p in config.PAGES]
                if not visa:
                    return self._send(400, {"error": "Välj minst en sida."})
                if "fotboll" in visa:
                    try:
                        football.resolve(lag)
                    except Exception:
                        return self._send(400, {"error": f"Hittar inget lag som heter \"{lag}\"."})
                if "aktie" in visa and not aktie and not aktie_id:
                    return self._send(400, {"error": "Välj en aktie."})
                at = d.get("airtable") or {}
                link, token = str(at.get("link", "")).strip()[:300], str(at.get("token", "")).strip()[:200]
                if link or token:
                    lager.save_settings(link, token)
                config.save(namn, lag, kort, aktie, aktie_id, visa)
                log(f"settings saved from the web page (name '{namn}', team '{lag}', stock '{aktie}', pages {visa})")
                self._send(200, {"ok": True})
            except (ValueError, json.JSONDecodeError) as e:
                self._send(400, {"error": str(e)})

    return H


def start(status_fn, log, port=PORT):
    try:
        srv = ThreadingHTTPServer(("127.0.0.1", port), make_handler(status_fn, log))
    except OSError as e:
        log(f"settings page: cannot listen on port {port}: {e}")
        return None
    srv.daemon_threads = True
    threading.Thread(target=srv.serve_forever, daemon=True, name="webconfig").start()
    log(f"settings page: http://localhost:{port}")
    return srv
