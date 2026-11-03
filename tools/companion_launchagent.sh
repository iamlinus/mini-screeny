#!/bin/sh
# Start tools/companion.py at login (macOS LaunchAgent), restart it if it stops.
#
#   tools/companion_launchagent.sh install     install + start (run again after moving the repo)
#   tools/companion_launchagent.sh uninstall   stop + remove
#   tools/companion_launchagent.sh status      is it running? + last log lines
#   tools/companion_launchagent.sh restart     after changing companion.py
#
# Log: ~/Library/Logs/mini-screeny-companion.log
set -eu

LABEL="se.mini-screeny.companion"
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"
LOG="$HOME/Library/Logs/mini-screeny-companion.log"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PY="$ROOT/.venv/bin/python"
DOMAIN="gui/$(id -u)"

case "${1:-status}" in
  install)
    [ -x "$PY" ] || { echo "missing $PY (create the .venv first)"; exit 1; }
    mkdir -p "$HOME/Library/LaunchAgents" "$HOME/Library/Logs"
    cat > "$PLIST" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key><string>$LABEL</string>
  <key>ProgramArguments</key>
  <array>
    <string>$PY</string>
    <string>-I</string>
    <string>$ROOT/tools/companion.py</string>
  </array>
  <key>WorkingDirectory</key><string>$ROOT</string>
  <key>EnvironmentVariables</key>
  <dict>
    <key>PATH</key><string>$PATH</string>
  </dict>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><true/>
  <key>ThrottleInterval</key><integer>10</integer>
  <key>ProcessType</key><string>Background</string>
  <key>StandardOutPath</key><string>$LOG</string>
  <key>StandardErrorPath</key><string>$LOG</string>
</dict>
</plist>
EOF
    launchctl bootout "$DOMAIN/$LABEL" 2>/dev/null || true
    launchctl bootstrap "$DOMAIN" "$PLIST"
    echo "installed and started: $LABEL"
    echo "settings: http://localhost:8740"
    echo "log: $LOG"
    ;;
  uninstall)
    launchctl bootout "$DOMAIN/$LABEL" 2>/dev/null || true
    rm -f "$PLIST"
    echo "removed $LABEL"
    ;;
  restart)
    launchctl kickstart -k "$DOMAIN/$LABEL"
    echo "restarted $LABEL"
    ;;
  status)
    launchctl print "$DOMAIN/$LABEL" 2>/dev/null | grep -E "^\s*(state|pid|last exit code) =" || echo "not installed"
    [ -f "$LOG" ] && tail -n 8 "$LOG" || true
    ;;
  *)
    echo "usage: $0 install|uninstall|status|restart"; exit 2 ;;
esac
