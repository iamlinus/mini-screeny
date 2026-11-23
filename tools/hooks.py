"""The Claude Code hooks that send session events to the companion (see agents.py), set up from the settings page.

They live in ~/.claude/settings.json, the user's own Claude Code settings, so they apply to every Claude Code on
this Mac: the terminal, editors, Claude Desktop's Code mode, apps built on it. Only our own entries are touched:
other hooks, and every other setting, stay as they are. A file that is not valid JSON is never written.

A hook counts as ours when its command posts to the companion: the curl line below, or a script that does
(a hand-made setup, e.g. AgentTerminal's hooks/agent-event.sh).
"""
import json, os, tempfile

EVENTS = ["UserPromptSubmit", "PostToolBatch", "Notification", "Stop", "StopFailure", "SessionEnd"]
ENDPOINT = "127.0.0.1:8740/api/agent"
COMMAND = ("curl -s -m 2 -o /dev/null -H 'Content-Type: application/json' --data-binary @- "
           f"http://{ENDPOINT} 2>/dev/null; exit 0")


def settings_path():
    return os.path.expanduser("~/.claude/settings.json")


def _ours(command):
    if not isinstance(command, str):
        return False
    if ENDPOINT in command:
        return True
    path = os.path.expanduser(command.strip().split(" ")[0])
    try:
        if os.path.isfile(path) and os.path.getsize(path) < 100_000:
            with open(path, encoding="utf-8", errors="replace") as f:
                return ENDPOINT in f.read()
    except OSError:
        pass
    return False


def _load(path):
    """The settings as a dict ({} when there is no file), or raises ValueError with a reason."""
    if not os.path.exists(path):
        return {}
    with open(path, encoding="utf-8") as f:
        text = f.read()
    if not text.strip():
        return {}
    try:
        d = json.loads(text)
    except json.JSONDecodeError as e:
        raise ValueError(f"{path} går inte att läsa (rad {e.lineno}): rättar du den först lägger sidan in dem")
    if not isinstance(d, dict) or not isinstance(d.get("hooks", {}), dict):
        raise ValueError(f"{path} har en form sidan inte känner igen")
    return d


def _covered(d):
    hooks = d.get("hooks") or {}
    return {ev for ev in EVENTS for group in (hooks.get(ev) or []) if isinstance(group, dict)
            for h in (group.get("hooks") or []) if isinstance(h, dict) and _ours(h.get("command"))}


def _write(path, d):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    fd, tmp = tempfile.mkstemp(dir=os.path.dirname(path), prefix=".settings-", suffix=".json")
    with os.fdopen(fd, "w", encoding="utf-8") as f:
        json.dump(d, f, indent=2, ensure_ascii=False)
        f.write("\n")
    if os.path.exists(path):
        os.chmod(tmp, os.stat(path).st_mode & 0o777)
    os.replace(tmp, path)


def status(path=None):
    path = path or settings_path()
    try:
        covered = _covered(_load(path))
    except (OSError, ValueError) as e:
        return {"installed": False, "missing": EVENTS, "error": str(e)}
    missing = [ev for ev in EVENTS if ev not in covered]
    return {"installed": not missing, "partly": bool(covered) and bool(missing), "missing": missing, "error": ""}


def install(path=None):
    """Adds our hook to every event that does not have it yet."""
    path = path or settings_path()
    d = _load(path)
    covered = _covered(d)
    hooks = d.setdefault("hooks", {})
    for ev in EVENTS:
        if ev not in covered:
            hooks.setdefault(ev, []).append({"hooks": [{"type": "command", "command": COMMAND}]})
    _write(path, d)
    return status(path)


def remove(path=None):
    """Takes our hooks out again; groups and events left empty go too."""
    path = path or settings_path()
    d = _load(path)
    hooks = d.get("hooks") or {}
    for ev in list(hooks):
        groups = []
        for group in hooks[ev] or []:
            if isinstance(group, dict) and isinstance(group.get("hooks"), list):
                group["hooks"] = [h for h in group["hooks"] if not (isinstance(h, dict) and _ours(h.get("command")))]
                if not group["hooks"]:
                    continue
            groups.append(group)
        if groups:
            hooks[ev] = groups
        else:
            del hooks[ev]
    if "hooks" in d and not hooks:
        del d["hooks"]
    _write(path, d)
    return status(path)
