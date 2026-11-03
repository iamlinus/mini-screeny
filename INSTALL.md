# Installing the mini screeny companion (for Claude Code)

The user has a mini screeny (a small USB screen, already flashed) and wants it to work on this Mac.
The screen gets all its data from a small Python program on the Mac, the *companion*, over USB.
Your job is to install that program. Do **not** flash or build the firmware; the board already has it.
Talk to the user in Swedish if they write Swedish.

## Steps
1. Check this is macOS and that the screen is plugged in:
   `ls /dev/cu.usbmodem*` should list a port (the board is an Espressif USB serial device, VID 0x303A).
   No port: ask the user to plug the screen into a USB port (a data cable, not a charge-only one).
2. Get the code (skip if it is already there; then `git -C ~/mini-screeny pull`):
   `git clone https://github.com/iamlinus/mini-screeny.git ~/mini-screeny`
   (If `git` is missing, macOS offers to install the Command Line Tools; let the user accept.)
3. Run the installer: `~/mini-screeny/install.sh`
   It creates `.venv` with Python 3.12 (via `uv`, installing `uv` with Homebrew or its official
   installer if needed), installs a LaunchAgent (`se.mini-screeny.companion`) that starts the companion
   now and at every login, and opens the settings page http://localhost:8740. No sudo is needed.
4. Check the Claude Code login: `security find-generic-password -s "Claude Code-credentials" >/dev/null; echo $?`
   (no `-w`, so no keychain prompt). `44` means Claude Code is **not logged in in the Terminal**, and the
   Claude quota page cannot work. This is so even when you yourself run inside Claude Desktop, which keeps
   its own login. Ask the user to run `claude` in Terminal and log in (`/login`); the quotas then appear by
   themselves within a minute. The access token lasts hours and is renewed only when `claude` runs, so tell
   them the quotas can drop out ("inloggningen har gått ut" on the settings page) until they run `claude` again.
5. Tell the user: if macOS asks whether `security` may use the keychain item "Claude Code-credentials",
   choose **Always Allow**. That is how the Claude quota page reads the Claude Code login (read-only,
   the token never leaves the Mac except to api.anthropic.com).
6. Verify: `~/mini-screeny/tools/companion_launchagent.sh status` shows `state = running`, and
   `curl -s localhost:8740/api/state` shows `"connected": true` and the sources becoming `"ok": true`
   within a minute. The screen's data pages then show data and a green dot (top right).
7. Settings (name, football team, stock, which pages): the user picks them on http://localhost:8740,
   or edit `~/mini-screeny/config.toml`. Changes reach the screen within a minute.

## Troubleshooting
- Log: `~/Library/Logs/mini-screeny-companion.log`.
- "cannot open /dev/cu...": another program holds the port (e.g. a serial monitor). Close it.
- Claude quota page says "Väntar på datorn ...": the settings page (Claude-kvoter row) says why.
  "Logga in i Claude Code" = no Terminal login (step 4). "har gått ut" = the token expired; running
  `claude` once renews it. "Nyckelringen nekade" = the keychain prompt was denied: run
  `security find-generic-password -s "Claude Code-credentials" -w >/dev/null` and choose Always Allow.
- Restart after changing files: `~/mini-screeny/tools/companion_launchagent.sh restart`.
- Remove everything: `~/mini-screeny/tools/companion_launchagent.sh uninstall` and delete `~/mini-screeny`.
