"""Claude and Codex quota windows, read on this Mac: the data behind the VibePulse page.

Adapted from VibePulse's tokenserver (https://github.com/niclasvestlund-YT/vibepulse,
tools/tokenserver/tokenserver.py, codex_rollout.py, codex_command.py at 468f952).
MIT License, Copyright (c) 2026 Niclas Vestlund; see tools/LICENSE.vibepulse.
Only the quota part is taken: no HTTP server, no history, no header probe (that one sends a real
model request and costs tokens).

Claude: the OAuth access token Claude Code keeps (Claude Desktop's bundled Claude Code process
first, then the macOS keychain entry "Claude Code-credentials") is sent to the read-only
endpoint https://api.anthropic.com/api/oauth/usage. That bucket is shared with Claude Code itself,
so the probe runs every 4 min at most, backs off on failures, rests >= 10 min after a 429 (also
across restarts) and never resends a token that got 401/403. Tokens are never logged.

Codex: `codex app-server` answers account/rateLimits/read (the same numbers the Codex UI shows);
if that fails, the newest rate_limits event in ~/.codex/sessions rollouts is used.

Values carry their absolute reset time, so "resets in" is recomputed whenever a line is sent.
"""
import json, math, os, queue, re, shutil, subprocess, threading, time, urllib.error, urllib.request
from datetime import datetime
from pathlib import Path

# ---------------- Claude ----------------
USAGE_URL = "https://api.anthropic.com/api/oauth/usage"
LIMITS_EVERY_S = 240          # VibePulse's cadence: ~15 calls/h on a bucket shared with Claude Code
AUTH_RECHECK_S = 15           # no usable token: only reread the local stores, no upstream call
REST_429_S = 600
CLAUDE_MAX_AGE_S = 30 * 60    # older than this: show dashes rather than old numbers
MODEL_LABELS = {"fable": "FABLE · WEEK", "opus": "OPUS · WEEK", "sonnet": "SONNET · WEEK"}

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
        if (not isinstance(pct, (int, float)) or isinstance(pct, bool) or not math.isfinite(pct)
                or not 0 <= pct <= 100 or reset_at is None or reset_at <= now_ts):
            continue
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
        status, wait = self._cycle()
        self._set_status(status)
        self.next_at = time.time() + wait

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
            if v and v[1] > now:
                out[f"{prefix}Pct"] = v[0]
                out[f"{prefix}ResetMin"] = max(0, int(round((v[1] - now) / 60)))
        if "claudeModelWeekPct" in out:
            out["claudeModelWeekLabel"] = self.found.get("modelLabel")
        return out


# ---------------- Codex ----------------
CODEX_EVERY_S = 60
CODEX_WEEK_MINUTES = 10080
CODEX_SCAN_BYTES = 1024 * 1024


def codex_executable():
    exe = shutil.which("codex") or shutil.which("codex", path=os.pathsep.join(
        ["/opt/homebrew/bin", "/usr/local/bin", os.path.expanduser("~/.local/bin")]))
    if exe:
        return exe
    for c in ("/opt/homebrew/bin/codex", "/usr/local/bin/codex",
              "/Applications/ChatGPT.app/Contents/Resources/codex", "/Applications/Codex.app/Contents/Resources/codex"):
        if os.path.isfile(c) and os.access(c, os.X_OK):
            return c
    return None


def _codex_window(win, now_ts):
    """{used_percent, window_minutes, resets_at} -> (pct, reset_at, window_minutes) or None."""
    if not isinstance(win, dict):
        return None
    pct, reset_at, minutes = win.get("used_percent"), win.get("resets_at"), win.get("window_minutes")
    for v in (pct, reset_at, minutes):
        if not isinstance(v, (int, float)) or isinstance(v, bool) or not math.isfinite(v):
            return None
    if not 0 <= pct <= 100 or reset_at <= now_ts:
        return None
    return round(float(pct), 1), int(reset_at), minutes


def codex_from_rate_limits(rl, now_ts):
    """A rate_limits snapshot -> {"week": (pct, reset_at), "session": (pct, reset_at)} (either may be missing)."""
    out = {}
    if not isinstance(rl, dict):
        return out
    name = rl.get("limit_name")
    general = name is None or name == ""          # a named limit is a different pool, not the general week
    for key in ("primary", "secondary"):
        w = _codex_window(rl.get(key), now_ts)
        if not w:
            continue
        if w[2] == CODEX_WEEK_MINUTES and general and "week" not in out:
            out["week"] = (w[0], w[1])
        elif w[2] <= 600 and "session" not in out:
            out["session"] = (w[0], w[1])
    return out


def _camel(win):
    if not isinstance(win, dict):
        return None
    return {"used_percent": win.get("usedPercent"), "window_minutes": win.get("windowDurationMins"),
            "resets_at": win.get("resetsAt")}


def codex_app_server_limits(timeout_s=15):
    """Ask `codex app-server` for account/rateLimits/read (JSON-RPC over stdio)."""
    exe = codex_executable()
    if not exe:
        return {}
    proc = None
    try:
        # `codex` is often a Node script (#!/usr/bin/env node); under launchd PATH lacks Homebrew/nvm.
        env = dict(os.environ)
        env["PATH"] = os.pathsep.join([os.path.dirname(os.path.realpath(exe)), os.path.dirname(exe),
                                       "/opt/homebrew/bin", "/usr/local/bin", env.get("PATH", "/usr/bin:/bin")])
        proc = subprocess.Popen([exe, "app-server", "--listen", "stdio://"], stdin=subprocess.PIPE, env=env,
                                stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, bufsize=1)
        lines = queue.Queue()

        def pump():
            try:
                for ln in proc.stdout:
                    lines.put(ln)
            except (OSError, ValueError):
                pass
            finally:
                lines.put(None)
        threading.Thread(target=pump, daemon=True).start()

        def send(msg):
            proc.stdin.write(json.dumps(msg) + "\n")
            proc.stdin.flush()

        send({"id": 1, "method": "initialize", "params": {"clientInfo": {"name": "mini-screeny", "version": "1"}, "capabilities": {}}})
        deadline, asked = time.monotonic() + timeout_s, False
        while time.monotonic() < deadline:
            try:
                ln = lines.get(timeout=0.25)
            except queue.Empty:
                if proc.poll() is not None:
                    break
                continue
            if ln is None:
                break
            try:
                msg = json.loads(ln)
            except json.JSONDecodeError:
                continue
            if msg.get("id") == 1 and not asked:
                send({"method": "initialized", "params": {}})
                send({"id": 2, "method": "account/rateLimits/read"})
                asked = True
            elif msg.get("id") == 2:
                body = msg.get("result") or {}
                buckets = body.get("rateLimitsByLimitId")
                rl = buckets.get("codex") if isinstance(buckets, dict) else None
                if not isinstance(rl, dict):
                    rl = body.get("rateLimits")
                if not isinstance(rl, dict):
                    return {}
                return codex_from_rate_limits({"limit_name": rl.get("limitName"), "primary": _camel(rl.get("primary")),
                                               "secondary": _camel(rl.get("secondary"))}, time.time())
    except (OSError, ValueError):
        return {}
    finally:
        if proc is not None:
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=1)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait(timeout=1)
            for pipe in (proc.stdin, proc.stdout):
                try:
                    pipe.close()
                except (OSError, AttributeError):
                    pass
    return {}


def _rollout_rate_limits(obj):
    """Only Codex's own token_count event envelope counts (not quoted or nested copies)."""
    if not isinstance(obj, dict) or obj.get("type") != "event_msg":
        return None
    p = obj.get("payload")
    if not isinstance(p, dict) or p.get("type") != "token_count":
        return None
    rl = p.get("rate_limits")
    return rl if isinstance(rl, dict) else None


def _observed_at(value):
    try:
        return int(datetime.fromisoformat(str(value).replace("Z", "+00:00")).timestamp())
    except (TypeError, ValueError, OverflowError):
        return None


def codex_scan_rollouts(now_ts=None):
    """Fallback: the newest rate_limits event in the 20 most recent rollout files (tail only)."""
    now_ts = now_ts or time.time()
    home = os.environ.get("CODEX_HOME")
    sessions = (Path(home).expanduser() if home else Path.home() / ".codex") / "sessions"
    try:
        files = sorted(sessions.glob("**/rollout-*.jsonl"), key=lambda p: p.stat().st_mtime, reverse=True)[:20]
    except OSError:
        return {}
    best = {}   # key -> (observed_at, value)
    for path in files:
        try:
            with path.open("rb") as f:
                f.seek(0, os.SEEK_END)
                size = f.tell()
                f.seek(max(0, size - CODEX_SCAN_BYTES))
                tail = f.read().split(b"\n")
        except OSError:
            continue
        for raw in reversed(tail):
            if b'"rate_limits"' not in raw:
                continue
            try:
                ev = json.loads(raw)
            except (json.JSONDecodeError, UnicodeDecodeError):
                continue
            rl, at = _rollout_rate_limits(ev), _observed_at(ev.get("timestamp") if isinstance(ev, dict) else None)
            if rl is None or at is None:
                continue
            for k, v in codex_from_rate_limits(rl, now_ts).items():
                if k not in best or at > best[k][0]:
                    best[k] = (at, v)
            break   # newest event in this file is enough
    return {k: v for k, (_at, v) in best.items()}


class CodexUsage:
    def __init__(self, log):
        self.log = log
        self.found = {}
        self.next_at = 0.0
        self.status = None

    def poll(self):
        if time.time() < self.next_at:
            return
        self.next_at = time.time() + CODEX_EVERY_S
        found, src = codex_app_server_limits(), "codex app-server"
        if not found:
            found, src = codex_scan_rollouts(), "codex session logs"
        status = f"ok via {src}" if found else ("no codex installed" if not codex_executable() else "no data")
        if found:
            self.found = found
        if status != self.status:
            self.log(f"codex quota: {status}")
            self.status = status

    def values(self):
        now = time.time()
        out = {}
        for key, prefix in (("session", "codexSession"), ("week", "codexWeek")):
            v = self.found.get(key)
            if v and v[1] > now:
                out[f"{prefix}Pct"] = v[0]
                out[f"{prefix}ResetMin"] = max(0, int(round((v[1] - now) / 60)))
        return out
