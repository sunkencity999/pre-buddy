# Clickable launchers

Double-clickable shortcuts for users who'd rather not open a terminal.

There's one pair per platform — one for first-time **Setup** (BLE scan,
device pick, autostart prompt) and one for the everyday **Tray** app
(menu-bar icon with Connect / Disconnect / Open Viewer / Launch at
login).

| Platform | Setup | Tray |
|---|---|---|
| macOS | `PRE Buddy Setup.command` | `PRE Buddy.command` |
| Linux | `pre-buddy-setup.desktop` | `pre-buddy.desktop` |
| Windows | `PRE Buddy Setup.bat` | `PRE Buddy.bat` |

All six launchers locate the `pre-buddy` CLI in the standard places. If
PRE Buddy isn't installed, they show a hint to run `pip install
pre_buddy[tray,transport]`.

## macOS install

1. Install once:
   ```bash
   pip install 'pre_buddy[tray,transport]'
   ```
2. Copy `PRE Buddy Setup.command` and `PRE Buddy.command` to
   `~/Applications/` (or anywhere; the Desktop works fine too).
3. The first time you double-click, macOS Gatekeeper will say "cannot be
   opened because it is from an unidentified developer." Right-click ▸
   **Open** ▸ confirm. After that, double-click works directly.
4. Run **PRE Buddy Setup** once. Then **PRE Buddy** every other day.

## Linux install

1. Install once:
   ```bash
   pip install 'pre_buddy[tray,transport]'
   ```
2. Make the `.desktop` files executable (already done in the repo, but
   re-run if you copied them through file sharing):
   ```bash
   chmod +x launchers/*.desktop
   ```
3. Copy them somewhere your file manager will pick them up:
   ```bash
   cp launchers/pre-buddy-setup.desktop ~/Desktop/
   cp launchers/pre-buddy.desktop ~/.local/share/applications/
   ```
   Files in `~/.local/share/applications/` appear in your distro's app
   launcher (GNOME Activities, KDE krunner, etc.).
4. The first time you launch via GNOME, you may need to right-click ▸
   **Allow launching** to trust the shortcut.

## Windows install

1. Install once. From PowerShell or Command Prompt:
   ```
   pip install pre_buddy[tray,transport]
   ```
2. Copy `PRE Buddy Setup.bat` and `PRE Buddy.bat` to your Desktop or
   anywhere you like. Double-clicking runs the corresponding command.
3. To pin to the Start menu: right-click the `.bat` file ▸ **Pin to
   Start**.
4. To make a nicer-looking shortcut: right-click ▸ **Create shortcut**,
   then **Properties** ▸ **Change Icon...** ▸ pick `tray_icon.png` from
   the `pre_buddy/assets/` site-packages directory.

## What setup does

```
$ pre-buddy setup
PRE Buddy First-Time Setup
===========================

Step 1/3: Pick your robot
  Scanning for BLE devices (5s)...
  Found 1 device(s):
    [1] pre-buddy                AA:BB:CC:DD:EE:FF  (-55 dBm)
    [m] Enter an address manually
    [s] Skip device selection
  Pick a device: 1
  ✓ Selected: pre-buddy (AA:BB:CC:DD:EE:FF)

Step 2/3: Launch at login?
  PRE Buddy can start automatically when you log in. Enable? [y/N]: y
  ✓ LaunchAgent installed
  ✓ Wrote /Users/you/Library/LaunchAgents/com.pre-buddy.tray.plist

Step 3/3: Save configuration
  ✓ Wrote /Users/you/.config/pre-buddy/config.json

Setup complete.
  Start the tray with:  pre-buddy tray
```

The tray app reads the same config file, so once setup finishes, the
clickable tray launcher just works.
