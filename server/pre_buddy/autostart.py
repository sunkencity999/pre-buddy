"""Cross-platform "launch at login" installer/uninstaller.

The three platforms each have their own conventions. We hide them
behind a tiny ``install`` / ``uninstall`` / ``is_installed`` API so the
setup wizard and tray app don't have to know which OS they're on.

- **macOS:** A LaunchAgent plist under
  ``~/Library/LaunchAgents/com.pre-buddy.tray.plist``. ``launchd`` picks
  it up at the next login.
- **Linux:** A ``.desktop`` file under ``~/.config/autostart/``. Honoured
  by all freedesktop-compliant desktops (GNOME, KDE, XFCE, Cinnamon).
- **Windows:** An entry in
  ``HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run`` via the
  stdlib ``winreg`` module. Per-user, no admin needed.

All three paths are overridable via the ``override_*`` kwargs so the
tests can run against a tmp dir on any host (e.g. macOS CI exercising
the Linux path).
"""

from __future__ import annotations

import os
import platform
import shutil
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Optional


# Canonical identifiers shared across platforms.
APP_NAME: str = "PRE Buddy"
APP_ID: str = "com.pre-buddy.tray"


def _resolve_command() -> str:
    """Return the command line that an autostart entry should launch.

    Prefers ``pre-buddy tray`` if the script is on PATH; falls back to
    ``<python> -m pre_buddy.cli tray`` so a venv install without an
    activated PATH still works.
    """
    on_path = shutil.which("pre-buddy")
    if on_path:
        return f'"{on_path}" tray'
    return f'"{sys.executable}" -m pre_buddy.cli tray'


# ── platform helpers ──────────────────────────────────────────────────


def _macos_plist_path() -> Path:
    return Path.home() / "Library" / "LaunchAgents" / f"{APP_ID}.plist"


def _linux_desktop_path() -> Path:
    base = os.environ.get("XDG_CONFIG_HOME")
    root = Path(base) if base else Path.home() / ".config"
    return root / "autostart" / "pre-buddy.desktop"


def _macos_plist_contents(command: str) -> str:
    # We split the command into ProgramArguments so launchd doesn't
    # invoke /bin/sh — fewer surprises, no shell quoting.
    parts = _split_command(command)
    args = "\n".join(f"        <string>{p}</string>" for p in parts)
    return (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" '
        '"http://www.apple.com/DTDs/PropertyList-1.0.dtd">\n'
        '<plist version="1.0">\n'
        "<dict>\n"
        f"    <key>Label</key>\n    <string>{APP_ID}</string>\n"
        "    <key>ProgramArguments</key>\n    <array>\n"
        f"{args}\n"
        "    </array>\n"
        "    <key>RunAtLoad</key>\n    <true/>\n"
        "    <key>ProcessType</key>\n    <string>Interactive</string>\n"
        "</dict>\n"
        "</plist>\n"
    )


def _linux_desktop_contents(command: str) -> str:
    return (
        "[Desktop Entry]\n"
        "Type=Application\n"
        f"Name={APP_NAME}\n"
        f"Exec={command}\n"
        "Terminal=false\n"
        "X-GNOME-Autostart-enabled=true\n"
        f"Comment={APP_NAME} system tray\n"
    )


def _split_command(command: str) -> list[str]:
    """Tokenise a quoted command line into argv-style pieces.

    Plist ``ProgramArguments`` needs the executable + args separated.
    Our ``_resolve_command`` only ever quotes the first token, so a
    naive shlex.split with posix=True is enough here.
    """
    import shlex

    return shlex.split(command, posix=True)


# ── primary API ──────────────────────────────────────────────────────


@dataclass
class AutostartResult:
    installed: bool
    path: Optional[Path] = None
    note: str = ""


def install(
    *,
    override_system: Optional[str] = None,
    override_macos_path: Optional[Path] = None,
    override_linux_path: Optional[Path] = None,
    override_command: Optional[str] = None,
    override_windows_registry: Optional[dict[str, str]] = None,
) -> AutostartResult:
    """Install the platform-appropriate autostart entry.

    Idempotent: re-running on an already-installed system overwrites the
    existing entry with current content.
    """
    system = override_system or platform.system()
    command = override_command or _resolve_command()

    if system == "Darwin":
        path = override_macos_path or _macos_plist_path()
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(_macos_plist_contents(command), encoding="utf-8")
        return AutostartResult(installed=True, path=path, note="LaunchAgent installed")

    if system == "Linux":
        path = override_linux_path or _linux_desktop_path()
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(_linux_desktop_contents(command), encoding="utf-8")
        return AutostartResult(installed=True, path=path, note=".desktop installed")

    if system == "Windows":
        # The registry path is platform-specific; tests inject a dict to
        # avoid touching the real registry.
        if override_windows_registry is not None:
            override_windows_registry[APP_NAME] = command
            return AutostartResult(
                installed=True, path=None, note="HKCU\\Run set (test override)"
            )
        try:
            import winreg  # type: ignore[import-not-found]
        except ImportError as exc:  # pragma: no cover - not on Windows
            return AutostartResult(
                installed=False, note=f"winreg unavailable: {exc}"
            )
        with winreg.OpenKey(
            winreg.HKEY_CURRENT_USER,
            r"Software\Microsoft\Windows\CurrentVersion\Run",
            0,
            winreg.KEY_SET_VALUE,
        ) as key:
            winreg.SetValueEx(key, APP_NAME, 0, winreg.REG_SZ, command)
        return AutostartResult(installed=True, path=None, note="HKCU\\Run set")

    return AutostartResult(installed=False, note=f"unsupported platform: {system}")


def uninstall(
    *,
    override_system: Optional[str] = None,
    override_macos_path: Optional[Path] = None,
    override_linux_path: Optional[Path] = None,
    override_windows_registry: Optional[dict[str, str]] = None,
) -> AutostartResult:
    """Remove the autostart entry. Idempotent (missing = success)."""
    system = override_system or platform.system()

    if system == "Darwin":
        path = override_macos_path or _macos_plist_path()
        if path.exists():
            path.unlink()
            return AutostartResult(installed=False, path=path, note="LaunchAgent removed")
        return AutostartResult(installed=False, path=path, note="LaunchAgent already absent")

    if system == "Linux":
        path = override_linux_path or _linux_desktop_path()
        if path.exists():
            path.unlink()
            return AutostartResult(installed=False, path=path, note=".desktop removed")
        return AutostartResult(installed=False, path=path, note=".desktop already absent")

    if system == "Windows":
        if override_windows_registry is not None:
            override_windows_registry.pop(APP_NAME, None)
            return AutostartResult(
                installed=False, note="HKCU\\Run cleared (test override)"
            )
        try:
            import winreg  # type: ignore[import-not-found]
        except ImportError as exc:  # pragma: no cover - not on Windows
            return AutostartResult(
                installed=False, note=f"winreg unavailable: {exc}"
            )
        try:
            with winreg.OpenKey(
                winreg.HKEY_CURRENT_USER,
                r"Software\Microsoft\Windows\CurrentVersion\Run",
                0,
                winreg.KEY_SET_VALUE,
            ) as key:
                winreg.DeleteValue(key, APP_NAME)
        except FileNotFoundError:
            pass
        return AutostartResult(installed=False, note="HKCU\\Run cleared")

    return AutostartResult(installed=False, note=f"unsupported platform: {system}")


def is_installed(
    *,
    override_system: Optional[str] = None,
    override_macos_path: Optional[Path] = None,
    override_linux_path: Optional[Path] = None,
    override_windows_registry: Optional[dict[str, str]] = None,
) -> bool:
    """Return True if an autostart entry currently exists for this user."""
    system = override_system or platform.system()
    if system == "Darwin":
        return (override_macos_path or _macos_plist_path()).exists()
    if system == "Linux":
        return (override_linux_path or _linux_desktop_path()).exists()
    if system == "Windows":
        if override_windows_registry is not None:
            return APP_NAME in override_windows_registry
        try:
            import winreg  # type: ignore[import-not-found]
        except ImportError:  # pragma: no cover - not on Windows
            return False
        try:
            with winreg.OpenKey(
                winreg.HKEY_CURRENT_USER,
                r"Software\Microsoft\Windows\CurrentVersion\Run",
                0,
                winreg.KEY_QUERY_VALUE,
            ) as key:
                winreg.QueryValueEx(key, APP_NAME)
            return True
        except FileNotFoundError:
            return False
    return False
