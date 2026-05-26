#!/bin/bash
# PRE Buddy — macOS double-clickable tray launcher.
#
# Drop into ~/Applications/ (or anywhere). Double-click in Finder to
# bring up the menu-bar icon. Right-click the icon for Connect /
# Disconnect / Open Viewer / Quit.
#
# Make executable once:  chmod +x "PRE Buddy.command"

set -e
cd "$HOME"

if command -v pre-buddy >/dev/null 2>&1; then
    exec pre-buddy tray
elif command -v python3 >/dev/null 2>&1; then
    exec python3 -m pre_buddy.cli tray
else
    osascript -e 'display alert "PRE Buddy not installed" message "Install with: pip install pre_buddy[tray,transport]"'
    exit 1
fi
