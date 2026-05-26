#!/bin/bash
# PRE Buddy — macOS double-clickable setup launcher.
#
# Drop into ~/Applications/ (or anywhere). Double-click in Finder to
# walk through first-time setup: BLE scan, device pick, autostart prompt.
#
# Make executable once:  chmod +x "PRE Buddy Setup.command"

set -e
cd "$HOME"

# Prefer an explicit pre-buddy on PATH; fall back to `python3 -m`.
if command -v pre-buddy >/dev/null 2>&1; then
    exec pre-buddy setup
elif command -v python3 >/dev/null 2>&1; then
    exec python3 -m pre_buddy.cli setup
else
    osascript -e 'display alert "PRE Buddy not installed" message "Install with: pip install pre_buddy[tray,transport]"'
    exit 1
fi
