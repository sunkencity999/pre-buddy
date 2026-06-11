"""PRE Buddy settings: translate GUI/agent `buddy_control` into device lines.

PRE's GUI (and the `buddy_control` agent tool) send a single control message:

    {"type":"buddy_control","settings":{"led_color":"cyan","volume":80, ...}}

The bridge turns the ``settings`` dict into the JSON lines the firmware parses:

    - led_color / led_brightness / volume / idle_anim / thinking_anim /
      boot_chime  → one ``pre.buddy.config`` event
    - character ("sage"|"sprout"|"sentinel")          → a ``pre.character.set`` event

These are outbound-only (the device never echoes them back), so we build the
lines directly rather than through the typed Event model — keeping the
all-optional shape simple. Validation here means a bad value is dropped, never
forwarded as garbage the device would skip anyway.
"""

from __future__ import annotations

import json
from typing import Any, Optional

LED_COLORS = frozenset(
    {"off", "blue", "green", "amber", "red", "white", "cyan", "purple", "yellow", "auto"}
)
CHARACTERS = frozenset({"sage", "sprout", "sentinel"})
_BOOL_KEYS = ("idle_anim", "thinking_anim", "boot_chime")


def _clampi(v: Any, lo: int, hi: int) -> Optional[int]:
    try:
        n = int(v)
    except (TypeError, ValueError):
        return None
    return max(lo, min(hi, n))


def buddy_config_payload(settings: dict) -> Optional[dict]:
    """Build the ``data`` dict for a pre.buddy.config event from ``settings``.

    Returns None if no recognized device-config field is present (e.g. the
    message carried only ``character``, which is a separate event).
    """
    data: dict[str, Any] = {}
    color = settings.get("led_color")
    if isinstance(color, str) and color.lower() in LED_COLORS:
        data["led_color"] = color.lower()
    b = _clampi(settings.get("led_brightness"), 0, 255)
    if b is not None and "led_brightness" in settings:
        data["led_brightness"] = b
    v = _clampi(settings.get("volume"), 0, 100)
    if v is not None and "volume" in settings:
        data["volume"] = v
    for k in _BOOL_KEYS:
        if isinstance(settings.get(k), bool):
            data[k] = settings[k]
    return data or None


def device_lines(settings: dict) -> list[str]:
    """Translate a ``buddy_control`` settings dict into device JSON lines.

    May return zero, one (config or character), or two (both) lines.
    """
    lines: list[str] = []
    payload = buddy_config_payload(settings)
    if payload is not None:
        lines.append(json.dumps(
            {"event": "pre.buddy.config", "data": payload},
            separators=(",", ":"), sort_keys=True))
    character = settings.get("character")
    if isinstance(character, str) and character.lower() in CHARACTERS:
        lines.append(json.dumps(
            {"event": "pre.character.set", "data": {"character": character.lower()}},
            separators=(",", ":"), sort_keys=True))
    return lines
