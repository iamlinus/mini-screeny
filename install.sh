#!/bin/sh
# Installerar companion-servern för mini screeny på den här Macen (kör igen för att uppdatera).
#
#   ./install.sh
#
# 1. Python 3.12 + pyserial i .venv (via uv; macOS egen Python är för gammal)
# 2. companion-servern startar vid inloggning (LaunchAgent), loggen hamnar i ~/Library/Logs
# 3. inställningssidan öppnas: http://localhost:8740
set -eu
cd "$(dirname "$0")"
ROOT="$(pwd)"

[ "$(uname)" = "Darwin" ] || { echo "mini screeny companion fungerar bara på macOS."; exit 1; }

say() { printf '\n==> %s\n' "$1"; }

# --- Python (>= 3.11) ---
have_python() {
  [ -x "$ROOT/.venv/bin/python" ] && "$ROOT/.venv/bin/python" -c 'import sys, serial, tomllib; sys.exit(sys.version_info < (3, 11))' 2>/dev/null
}
if have_python; then
  say "Python-miljön finns redan (.venv)"
else
  if ! command -v uv >/dev/null 2>&1; then
    say "Installerar uv (hämtar Python åt oss)"
    if command -v brew >/dev/null 2>&1; then
      brew install uv
    else
      curl -LsSf https://astral.sh/uv/install.sh | sh
      PATH="$HOME/.local/bin:$PATH"; export PATH
    fi
  fi
  say "Skapar Python-miljön (.venv)"
  uv venv --python 3.12 "$ROOT/.venv"
  uv pip install --python "$ROOT/.venv/bin/python" -r "$ROOT/tools/requirements.txt"
fi

# --- autostart ---
say "Startar companion-servern (och vid varje inloggning)"
"$ROOT/tools/companion_launchagent.sh" install

# --- check ---
say "Kollar att allt svarar"
ok=""
i=0
while [ $i -lt 20 ]; do
  if curl -fs http://localhost:8740/api/state >/dev/null 2>&1; then ok=1; break; fi
  sleep 1; i=$((i + 1))
done
if [ -z "$ok" ]; then
  echo "Inställningssidan svarar inte. Se loggen: ~/Library/Logs/mini-screeny-companion.log"
  exit 1
fi
curl -fs http://localhost:8740/api/state | "$ROOT/.venv/bin/python" -c '
import json, sys
s = json.load(sys.stdin)["status"]
print("Skärmen:", "ansluten" if s["board"]["connected"] else "INTE ansluten (koppla in den med USB)")
for src in s["sources"]:
    print(src["label"] + ":", "av" if not src["enabled"] else ("ok" if src["ok"] else "väntar"))
'
open http://localhost:8740
say "Klart! Inställningarna är öppna i webbläsaren."
echo "Frågar macOS om Nyckelringen (\"Claude Code-credentials\"): välj Tillåt alltid, det är så Claude-kvoterna läses."
