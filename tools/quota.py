"""Claude quota windows, read on this Mac: the data behind the VibePulse page.

Adapted from VibePulse's tokenserver (https://github.com/niclasvestlund-YT/vibepulse,
tools/tokenserver/tokenserver.py at 468f952).
MIT License, Copyright (c) 2026 Niclas Vestlund; see tools/LICENSE.vibepulse.
Only the quota part is taken: no HTTP server, no history, no header probe (that one sends a real
model request and costs tokens).

Claude: the OAuth access token Claude Code keeps (Claude Desktop's bundled Claude Code process
first, then the macOS keychain entry "Claude Code-credentials") is sent to the read-only
endpoint https://api.anthropic.com/api/oauth/usage. That bucket is shared with Claude Code itself,
so the probe runs every 4 min at most, backs off on failures, rests >= 10 min after a 429 (also
across restarts) and never resends a token that got 401/403. Tokens are never logged.

Values carry their absolute reset time, so "resets in" is recomputed whenever a line is sent.
"""
import json, math, re, subprocess, time, urllib.error, urllib.request
from datetime import datetime
from pathlib import Path

# ---------------- Claude ----------------
USAGE_URL = "https://api.anthropic.com/api/oauth/usage"
LIMITS_EVERY_S = 240          # VibePulse's cadence: ~15 calls/h on a bucket shared with Claude Code
AUTH_RECHECK_S = 15           # no usable token: only reread the local stores, no upstream call
REST_429_S = 600
CLAUDE_MAX_AGE_S = 30 * 60    # older than this: show dashes rather than old numbers
MODEL_LABELS = {"fable": "FABLE · VECKA", "opus": "OPUS · VECKA", "sonnet": "SONNET · VECKA"}

_DESKTOP_PROCESS = re.compile(
    r"^/Users/[^/\s]+/Library/Application Support/Claude/claude-code/[^/\s]+/claude\.app/Contents/MacOS/claude(?:\s|$)")
_PROCESS_TOKEN = re.compile(r"(?:^|\s)CLAUDE_CODE_OAUTH_TOKEN=([^\s]+)")


def _run(cmd, timeout):
    return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)


def _desktop_process_token():
    """Claude Desktop refreshes OAuth itself and injects the token into its bundled Claude Code."""
    try:
        pids = _run(["pgrep", "-f", "/Library/Application Support/Claude/claude-code/.*/claude.app/Contents/MacOS/claude"], 5).stdout
    except Exception:
        return None
    for pid in pids.split():
        if not pid.isdigit():
            continue
        try:
            command = _run(["ps", "eww", "-p", pid, "-o", "command="], 5).stdout.strip()
        except Exception:
            continue
        if _DESKTOP_PROCESS.match(command):
            m = _PROCESS_TOKEN.search(command)
            if m:
                return m.group(1)
    return None


def _keychain_token():
    """(token, expires_at_ms, reason). The first read makes macOS ask; answer "Always Allow"."""
    try:
        done = _run(["security", "find-generic-password", "-s", "Claude Code-credentials", "-w"], 30)
    except FileNotFoundError:
        return None, None, "keychain: no 'security' tool"
    except subprocess.TimeoutExpired:
        return None, None, "keychain: prompt not answered"
    if done.returncode == 44:
        return None, None, "keychain: no Claude Code login on this Mac"
    if done.returncode != 0:
        return None, None, f"keychain: denied or locked (exit {done.returncode})"
    try:
        oauth = json.loads(done.stdout.strip()).get("claudeAiOauth") or {}
    except (ValueError, AttributeError):
        return None, None, "keychain: unreadable entry"
    tok = oauth.get("accessToken")
    return (tok, oauth.get("expiresAt"), None) if tok else (None, None, "keychain: entry without token")


def _parse_reset_at(value, now_ts):
    """Epoch, seconds-remaining or ISO time -> epoch seconds."""
    try:
        n = float(value)
        if not math.isfinite(n) or n < 0:
            return None
        return int(n if n > 1e9 else now_ts + n)
    except (TypeError, ValueError, OverflowError):
        pass
    try:
        return int(datetime.fromisoformat(str(value).replace("Z", "+00:00")).timestamp())
    except (TypeError, ValueError, OverflowError):
        return None


def parse_usage_limits(body, now_ts):
    """The usage endpoint's "limits" list -> {session|week|model: (pct, reset_at)}, modelLabel."""
    found = {}
    if not isinstance(body, dict) or not isinstance(body.get("limits"), list):
        return found
    for lim in body["limits"]:
        if not isinstance(lim, dict):
            continue
        kind, pct = lim.get("kind"), lim.get("percent")
        reset_at = _parse_reset_at(lim.get("resets_at"), now_ts)
        if not isinstance(pct, (int, float)) or isinstance(pct, bool) or not math.isfinite(pct) or not 0 <= pct <= 100:
            continue
        if reset_at is None or reset_at <= now_ts:
            # Between windows (the old one ended, no new one started yet) the session is simply 0 %
            # with no reset time. Dropping it would show dashes as if the data were missing.
            if kind != "session":
                continue
            pct, reset_at = 0, None
        if kind == "session":
            key = "session"
        elif kind == "weekly_all":
            key = "week"
        elif kind == "weekly_scoped" and (lim.get("is_active") is True or pct > 0):
            # is_active marks the binding limit right now, not "the pool exists": a used model pool
            # is shown even when inactive; an untouched one (0 % and inactive) is left out.
            scope = lim.get("scope")
            model = scope.get("model") if isinstance(scope, dict) else None
            name = model.get("display_name") if isinstance(model, dict) else None
            name = name.strip().lower() if isinstance(name, str) else ""
            if name not in MODEL_LABELS:
                continue
            key = "model"
            found["modelLabel"] = MODEL_LABELS[name]
        else:
            continue
        found[key] = (round(float(pct), 1), reset_at)
    return found


def hint_for(status):
    """ClaudeUsage.status -> what the owner should do, in Swedish, for the settings page (<= 80 chars)."""
    s = status or ""
    if "no Claude Code login" in s:
        return "Logga in i Claude Code: kör claude i Terminalen"
    if "keychain:" in s:
        return "Nyckelringen nekade: tillåt Claude Code-credentials (Tillåt alltid)"
    if s.startswith(("token expired", "token rejected")):
        return "Claude-inloggningen har gått ut: kör claude i Terminalen en gång"
    if s.startswith("HTTP 429"):
        return "Anthropic bromsar, försöker igen " + s.rsplit(" ", 1)[-1]
    if s.startswith(("request failed", "HTTP ")):
        return "Når inte Anthropic (" + s + ")"
    return s or "väntar på första hämtningen"


class ClaudeUsage:
    def __init__(self, state_path, log):
        self.state_path = Path(state_path)
        self.log = log
        self.found = {}
        self.ok_at = 0.0
        self.next_at = 0.0
        self.fail_streak = 0
        self.dead = {}            # token value -> reason; never sent again
        self.status = None
        self.probed_at = 0.0
        self.cooldown_until = self._load_cooldown()

    def _load_cooldown(self):
        try:
            until = float(json.loads(self.state_path.read_text()).get("cooldown_until", 0))
            return until if math.isfinite(until) else 0.0
        except (OSError, ValueError, TypeError, AttributeError):
            return 0.0

    def _save_cooldown(self, until):
        try:
            self.state_path.parent.mkdir(parents=True, exist_ok=True)
            self.state_path.write_text(json.dumps({"cooldown_until": until}))
        except OSError:
            pass

    def _set_status(self, s):
        if s != self.status:
            self.log(f"claude quota: {s}")
            self.status = s

    def poll(self):
        now = time.time()
        if now < max(self.next_at, self.cooldown_until):
            return
        self.probed_at = time.time()
        status, wait = self._cycle()
        self._set_status(status)
        self.next_at = time.time() + wait

    def hint(self):
        return hint_for(self.status)

    def request_refresh(self):
        """Manual refresh (BOOT double press): probe soon, but at most once a minute; a 429 rest still holds."""
        self.next_at = min(self.next_at, max(time.time(), self.probed_at + 60))

    def _cycle(self):
        cands, reason = [], None
        p = _desktop_process_token()
        if p:
            cands.append((p, None))
        k, exp, reason = _keychain_token()
        if k and k != p:
            cands.append((k, exp))
        if not cands:
            return f"no token ({reason})", AUTH_RECHECK_S
        status = "no usable token"
        for tok, exp in cands:
            if tok in self.dead:
                status = "token rejected, waiting for Claude Code to refresh it"
                continue
            if exp and exp / 1000 < time.time():
                status = "token expired, waiting for Claude Code to refresh it"
                continue
            req = urllib.request.Request(USAGE_URL, headers={
                "Authorization": f"Bearer {tok}",
                "anthropic-beta": "oauth-2025-04-20",
                "User-Agent": "claude-cli/2.1.227 (external, cli)",   # as VibePulse sends it
            })
            try:
                with urllib.request.urlopen(req, timeout=15) as r:
                    body = json.load(r)
            except urllib.error.HTTPError as e:
                if e.code == 429:
                    try:
                        retry = int((e.headers or {}).get("Retry-After", 0))
                    except (TypeError, ValueError):
                        retry = 0
                    self.cooldown_until = time.time() + max(retry, REST_429_S)
                    self._save_cooldown(self.cooldown_until)
                    return f"HTTP 429, resting until {datetime.fromtimestamp(self.cooldown_until):%H:%M}", 0
                if e.code in (401, 403):
                    self.dead[tok] = e.code
                    while len(self.dead) > 8:
                        self.dead.pop(next(iter(self.dead)))
                    status = f"token rejected (HTTP {e.code})"
                    continue
                return self._failed(f"HTTP {e.code}")
            except Exception as e:
                return self._failed(f"request failed: {type(e).__name__}")
            found = parse_usage_limits(body, time.time())
            if not found:
                return self._failed("answer without known limits")
            self.found, self.ok_at, self.fail_streak = found, time.time(), 0
            return "ok", LIMITS_EVERY_S
        return status, AUTH_RECHECK_S

    def _failed(self, status):
        self.fail_streak += 1
        return status, LIMITS_EVERY_S * (2 ** min(self.fail_streak, 2))   # 240 -> 480 -> 960 s

    def values(self):
        now = time.time()
        out = {}
        if not self.found or now - self.ok_at > CLAUDE_MAX_AGE_S:
            return out
        for key, prefix in (("session", "claudeSession"), ("week", "claudeWeek"), ("model", "claudeModelWeek")):
            v = self.found.get(key)
            if not v:
                continue
            if v[1] is not None and v[1] > now:
                out[f"{prefix}Pct"] = v[0]
                out[f"{prefix}ResetMin"] = max(0, int(round((v[1] - now) / 60)))
            elif key == "session":
                out[f"{prefix}Pct"] = 0.0     # window ended since the last probe: nothing used yet
        if "claudeModelWeekPct" in out:
            out["claudeModelWeekLabel"] = self.found.get("modelLabel")
        if out:
            out["claudeAgeSec"] = int(now - self.ok_at)   # the board turns its dot red past 5 min
        return out
