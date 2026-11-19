"""Agent sessions on this Mac (Claude Code) -> what the board shows.

Claude Code hooks send their event JSON to the companion (POST /api/agent in tools/webconfig.py; the
README has the hook setup). One state is kept per session:

  think   a prompt is being worked on
  wait    Claude asked for something (a permission, an answer) and is waiting for the user
  idle    nothing going on

Two kinds of lines go to the board:

  cc  {"n": sessions thinking, "w": sessions waiting, "t": project of the one that has waited longest}
      the state: sent when it changes and again on reconnect, like every data source
  cce {"e": "done" | "ask" | "fail", "t": project, "s": seconds the turn took}
      something happened just now: sent once, dropped if the board was not there to show it

A turn shorter than MIN_DONE_S ends without "done": the user is sitting right there.
"""
import json, os, threading, time

MIN_DONE_S = 15             # shorter turns end silently
STALE_THINK_S = 15 * 60     # "thinking" with no event for this long: the turn was interrupted (Esc sends no hook)
STALE_WAIT_S = 12 * 3600    # a prompt nobody answered: the terminal is probably gone
FORGET_S = 6 * 3600         # idle sessions are forgotten (SessionEnd normally removes them first)
EVENT_MAX_AGE_S = 30        # a "done" the board could not get in time is not worth showing later
TITLE_MAX = 16              # "Väntar: <title>" must fit the speech bubble

# Notification types that mean "Claude needs the user"
ASK_TYPES = {"permission_prompt", "elicitation_dialog", "elicitation_url_dialog", "agent_needs_input",
             "quota_auto_resume_stale"}
# Hook events that only happen while Claude is working
WORK_EVENTS = {"PreToolUse", "PostToolUse", "PostToolUseFailure", "PostToolBatch", "SubagentStart", "SubagentStop"}
# Background tasks that wake the session again when they finish: a Stop with one of these running is a pause
BUSY_TASKS = {"subagent", "workflow", "teammate"}


def title_of(cwd):
    """The folder Claude is working in, cut to fit. The board's fonts are Latin-1: anything else becomes '?'."""
    name = os.path.basename(str(cwd or "").rstrip("/"))
    name = "".join(ch if 32 <= ord(ch) < 127 or 160 <= ord(ch) < 256 else "?" for ch in name)
    return name if len(name) <= TITLE_MAX else name[:TITLE_MAX - 2].rstrip() + ".."


class Agents:
    def __init__(self, log=print):
        self.log = log
        self._lock = threading.Lock()
        self._s = {}            # session_id -> {"state", "since" (turn start), "seen" (last event), "title", "asked"}
        self._events = []       # (time, encoded line), waiting for the main loop to send them
        self.version = 1        # bumped whenever the state line changes
        self.line = self._encode(0, 0, "")

    @staticmethod
    def _encode(n, w, title):
        d = {"n": n, "w": w}
        if w and title:
            d["t"] = title
        return ("cc " + json.dumps(d, separators=(",", ":"), ensure_ascii=False) + "\n").encode()

    def _emit(self, kind, title, took, now):
        d = {"e": kind, "t": title}
        if took:
            d["s"] = int(took)
        self._events.append((now, ("cce " + json.dumps(d, separators=(",", ":"), ensure_ascii=False) + "\n").encode()))
        del self._events[:-20]
        self.log(f"agents: {kind} '{title}'" + (f" after {int(took)} s" if took else ""))

    def _refresh(self):
        think = [s for s in self._s.values() if s["state"] == "think"]
        wait = sorted((s for s in self._s.values() if s["state"] == "wait"), key=lambda s: s["asked"])
        line = self._encode(len(think), len(wait), wait[0]["title"] if wait else "")
        if line != self.line:
            self.line = line
            self.version += 1

    def event(self, d, now=None):
        """One hook event, as Claude Code sends it. False if it is not one."""
        now = now or time.time()
        sid, name = d.get("session_id"), d.get("hook_event_name")
        if not isinstance(sid, str) or not sid or not isinstance(name, str):
            return False
        with self._lock:
            if name == "SessionEnd":
                self._s.pop(sid, None)
                self._refresh()
                return True
            s = self._s.setdefault(sid, {"state": "idle", "since": now, "seen": now, "title": ""})
            s["seen"] = now
            s["title"] = title_of(d.get("cwd")) or s["title"]
            was = s["state"]
            if name == "UserPromptSubmit" or name in WORK_EVENTS:
                # A finished tool call also ends a wait: the permission was given. (A subagent's tool call
                # does too, even if the prompt belonged to another one: the two cannot be told apart.)
                if was == "idle":
                    s["since"] = now
                s["state"] = "think"
            elif name == "Stop" and not d.get("agent_id"):
                tasks = d.get("background_tasks") or []
                if any(isinstance(t, dict) and t.get("type") in BUSY_TASKS for t in tasks):
                    s["state"] = "think"        # paused: the background work will wake it again
                else:
                    s["state"] = "idle"
                    took = now - s["since"]
                    if was != "idle" and took >= MIN_DONE_S:
                        self._emit("done", s["title"], took, now)
            elif name == "StopFailure":
                s["state"] = "idle"
                if was != "idle":
                    self._emit("fail", s["title"], 0, now)
            elif name == "Notification":
                kind = d.get("notification_type")
                if kind in ASK_TYPES:
                    if was != "wait":
                        if was == "idle":
                            s["since"] = now
                        s["state"], s["asked"] = "wait", now
                        self._emit("ask", s["title"], 0, now)
                elif kind == "idle_prompt":
                    s["state"] = "idle"         # a minute with nothing running: an interrupted turn ends here
                elif kind == "agent_completed":
                    self._emit("done", s["title"], 0, now)
            self._refresh()
        return True

    def tick(self, now=None):
        """Drop what has gone stale. Called from the main loop."""
        now = now or time.time()
        with self._lock:
            for sid, s in list(self._s.items()):
                age = now - s["seen"]
                if s["state"] == "think" and age > STALE_THINK_S or s["state"] == "wait" and age > STALE_WAIT_S:
                    s["state"] = "idle"
                elif s["state"] == "idle" and age > FORGET_S:
                    del self._s[sid]
            self._refresh()

    def take_events(self, now=None):
        """The events not sent yet, oldest first, without the ones that are too old to show."""
        now = now or time.time()
        with self._lock:
            ev, self._events = self._events, []
        return [line for t, line in ev if now - t <= EVENT_MAX_AGE_S]

    def summary(self):
        with self._lock:
            now = time.time()
            return {"thinking": sum(s["state"] == "think" for s in self._s.values()),
                    "waiting": sum(s["state"] == "wait" for s in self._s.values()),
                    "sessions": [{"title": s["title"], "state": s["state"], "seen": int(now - s["seen"])}
                                 for s in self._s.values()]}
