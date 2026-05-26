"""Persistent configuration for the PRE Buddy GUI/onboarding stack.

A single JSON file holds everything the setup wizard, tray app, and CLI
need to know about a user's local install:

- which BLE peripheral to connect to (address or advertising name)
- whether to launch at login
- which port the viewer listens on
- where PRE itself lives (for the bridge subscriber)
- last successful connect timestamp (purely informational, used by the
  tray status string)

The schema is intentionally tiny and forward-compatible: unknown keys
read from disk are preserved on the next save so a newer release can
add fields without nuking older state.
"""

from __future__ import annotations

import json
import os
import platform
import tempfile
from dataclasses import asdict, dataclass, field, fields
from pathlib import Path
from typing import Any, Optional


# ── path resolution ───────────────────────────────────────────────────


def default_config_dir() -> Path:
    """Return the per-user config directory for PRE Buddy.

    - Linux / macOS: ``$XDG_CONFIG_HOME/pre-buddy`` (falls back to ``~/.config/pre-buddy``)
    - Windows: ``%APPDATA%\\pre-buddy``

    The directory is *not* created here; ``load`` and ``save`` create it
    as needed.
    """
    if platform.system() == "Windows":
        base = os.environ.get("APPDATA")
        if base:
            return Path(base) / "pre-buddy"
        return Path.home() / "AppData" / "Roaming" / "pre-buddy"
    base = os.environ.get("XDG_CONFIG_HOME")
    if base:
        return Path(base) / "pre-buddy"
    return Path.home() / ".config" / "pre-buddy"


def default_config_path() -> Path:
    return default_config_dir() / "config.json"


# ── schema ────────────────────────────────────────────────────────────


SCHEMA_VERSION: int = 1


@dataclass
class Config:
    """Typed view of the on-disk config.

    All fields have safe defaults so the tray app can run before setup
    has ever been completed. The setup wizard fills these in.
    """

    device_address: Optional[str] = None
    """Preferred way to connect — a full BLE address (CoreBluetooth UUID
    on macOS, MAC on Linux/Windows). Wins over device_name if both are set."""

    device_name: Optional[str] = None
    """Fallback: advertising name. The transport will scan + match."""

    character: str = "sage"
    """Which character identity (sage|sprout|sentinel) to render on the
    device. Persisted here so the tray app and setup wizard agree, and
    so the chosen identity survives device power-cycles by being re-sent
    via pre.character.set when the tray connects."""

    autostart: bool = False
    """User's "launch at login" choice. The actual installation is done
    by autostart.py; this field just records the intent."""

    viewer_port: int = 7750
    """Port the local scenario viewer listens on when launched from the tray."""

    pre_ws_url: str = "ws://localhost:7749"
    """PRE WebSocket URL the bridge will subscribe to."""

    last_connected_iso: Optional[str] = None
    """ISO-8601 timestamp of the most recent successful BLE connect.
    Surfaced in the tray status string."""

    schema_version: int = SCHEMA_VERSION
    """Bump when adding required fields with no sensible default."""

    extras: dict[str, Any] = field(default_factory=dict)
    """Forward-compat catch-all: keys we didn't know about at load time
    are stored here and re-emitted on save. Lets older code coexist with
    newer config files without dropping data."""


# ── load / save ───────────────────────────────────────────────────────


_KNOWN_FIELDS = {f.name for f in fields(Config) if f.name != "extras"}


def load(path: Optional[Path] = None) -> Config:
    """Load the config from disk, or return defaults if absent/corrupt.

    A corrupt file is treated as "no config" — we never raise from here
    because the tray app needs to come up even if disk state is bad.
    The user can re-run setup to overwrite it.
    """
    p = path or default_config_path()
    if not p.exists():
        return Config()
    try:
        raw = json.loads(p.read_text(encoding="utf-8"))
        if not isinstance(raw, dict):
            return Config()
    except (json.JSONDecodeError, OSError):
        return Config()

    kwargs: dict[str, Any] = {}
    extras: dict[str, Any] = {}
    for key, value in raw.items():
        if key in _KNOWN_FIELDS:
            kwargs[key] = value
        else:
            extras[key] = value
    cfg = Config(**kwargs)
    cfg.extras = extras
    return cfg


def save(cfg: Config, path: Optional[Path] = None) -> Path:
    """Atomically write the config to disk.

    Returns the path that was written. Creates the parent directory if
    it doesn't exist. The atomic-write pattern (tmp file in same dir +
    os.replace) avoids leaving a half-written file if the process dies
    mid-write — important because this is the source of truth for
    where the user's robot is.
    """
    target = path or default_config_path()
    target.parent.mkdir(parents=True, exist_ok=True)

    payload: dict[str, Any] = asdict(cfg)
    # Merge extras back into the top-level dict so unknown-but-preserved
    # keys round-trip across save/load.
    extras = payload.pop("extras", {}) or {}
    payload.update(extras)

    # Atomic replace: write to a tmp file in the same dir, then rename.
    with tempfile.NamedTemporaryFile(
        mode="w",
        encoding="utf-8",
        dir=str(target.parent),
        delete=False,
        prefix=".config.",
        suffix=".tmp",
    ) as tmp:
        json.dump(payload, tmp, indent=2, sort_keys=True)
        tmp.write("\n")
        tmp_path = Path(tmp.name)
    os.replace(tmp_path, target)
    return target


def update(path: Optional[Path] = None, **changes: Any) -> Config:
    """Load, apply ``changes``, save. Convenience for one-line updates."""
    cfg = load(path)
    for key, value in changes.items():
        if key not in _KNOWN_FIELDS:
            raise KeyError(f"unknown config field: {key!r}")
        setattr(cfg, key, value)
    save(cfg, path)
    return cfg
