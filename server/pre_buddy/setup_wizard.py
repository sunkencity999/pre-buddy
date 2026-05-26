"""First-time setup wizard for PRE Buddy.

Walks a (probably non-technical) user through three steps:

1. Pick a BLE device — by scanning, or by entering an address manually.
2. Decide whether to launch the tray at login.
3. Confirm the resulting config.

The CLI wrapper lives in ``cli.py``. Everything here is interactive but
deterministic when fed scripted input/output streams, which is how the
tests exercise the prompts.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Optional, Sequence, TextIO

from . import autostart, config


# ── data shape ────────────────────────────────────────────────────────


@dataclass
class DiscoveredDevice:
    """A BLE peripheral we'd consider connecting to. Mirrors what ``bleak``
    yields, but stays a plain dataclass so tests don't need bleak."""

    name: str
    address: str
    rssi: Optional[int] = None


DiscoverFn = Callable[[float], Sequence[DiscoveredDevice]]
"""Discover BLE peripherals. Takes a timeout in seconds, returns devices."""


@dataclass
class SetupResult:
    config_path: Path
    cfg: config.Config
    autostart_path: Optional[Path] = None


# ── helpers ───────────────────────────────────────────────────────────


def discover_via_bleak(timeout_s: float) -> list[DiscoveredDevice]:
    """Real device discovery. Imports ``bleak`` lazily."""
    try:
        from bleak import BleakScanner  # type: ignore[import-not-found]
    except ImportError as exc:
        raise RuntimeError(
            "BLE scan requires the 'bleak' package. "
            "Install with: pip install 'pre-buddy[transport]'"
        ) from exc

    import asyncio

    async def _scan() -> list[DiscoveredDevice]:
        devices = await BleakScanner.discover(timeout=timeout_s, return_adv=True)
        out: list[DiscoveredDevice] = []
        # bleak's return-type differs between 0.21 and 0.22+: a dict in
        # one, a list of tuples in the other. Be defensive.
        if isinstance(devices, dict):
            items = devices.values()
        else:
            items = devices  # type: ignore[assignment]
        for item in items:
            try:
                device, adv = item  # type: ignore[misc]
            except (TypeError, ValueError):
                device = item  # type: ignore[assignment]
                adv = None
            name = (getattr(device, "name", None)
                    or getattr(adv, "local_name", None)
                    or "(unnamed)")
            address = getattr(device, "address", "") or ""
            rssi = getattr(adv, "rssi", None) if adv else None
            out.append(DiscoveredDevice(name=str(name), address=str(address), rssi=rssi))
        return out

    return asyncio.run(_scan())


def _filter_pre_buddy_candidates(devices: Sequence[DiscoveredDevice]) -> list[DiscoveredDevice]:
    """Bring devices that look like a PRE Buddy to the top of the list.

    Anything advertising "pre-buddy" (case-insensitive) wins; the rest
    follow in original order. We don't drop unrelated devices because a
    user with a renamed device should still see it.
    """
    likely, others = [], []
    for d in devices:
        if "pre-buddy" in (d.name or "").lower():
            likely.append(d)
        else:
            others.append(d)
    return likely + others


# ── interactive flow ──────────────────────────────────────────────────


def run_setup(
    *,
    inp: TextIO,
    out: TextIO,
    discover: Optional[DiscoverFn] = discover_via_bleak,
    config_path: Optional[Path] = None,
    autostart_overrides: Optional[dict] = None,
    scan_timeout_s: float = 5.0,
) -> SetupResult:
    """Drive the full wizard, reading prompts from ``inp`` and writing
    output to ``out``.

    ``discover`` can be set to ``None`` to skip BLE entirely (manual
    entry only) — useful when the user just wants to set a known address
    without a radio scan. The tests exploit this heavily.
    """
    target = config_path or config.default_config_path()
    cfg = config.load(target)

    out.write("PRE Buddy First-Time Setup\n")
    out.write("===========================\n\n")

    # ── step 1: pick the device ───────────────────────────────────────
    out.write("Step 1/5: Pick your robot\n")
    device = _pick_device(
        inp=inp, out=out, discover=discover, scan_timeout_s=scan_timeout_s
    )
    if device is not None:
        cfg.device_address = device.address or None
        cfg.device_name = device.name or cfg.device_name
        out.write(f"  ✓ Selected: {device.name} ({device.address or 'address unknown'})\n\n")
    else:
        out.write("  (no device selected — you can re-run setup later)\n\n")

    # ── step 2: character identity ────────────────────────────────────
    out.write("Step 2/5: Pick a character\n")
    cfg.character = _pick_character(inp, out, default=cfg.character or "sage")
    out.write(f"  ✓ Character: {cfg.character}\n\n")

    # ── step 3: wake word ─────────────────────────────────────────────
    out.write("Step 3/5: Wake word\n")
    cfg.wake_word = _pick_wake_word(inp, out, default=cfg.wake_word or "hey buddy")
    out.write(f"  ✓ Wake word: {cfg.wake_word!r}\n\n")

    # ── step 4: autostart ─────────────────────────────────────────────
    out.write("Step 4/5: Launch at login?\n")
    cfg.autostart = _prompt_yes_no(
        inp,
        out,
        "  PRE Buddy can start automatically when you log in. Enable?",
        default_yes=False,
    )

    autostart_path: Optional[Path] = None
    overrides = autostart_overrides or {}
    if cfg.autostart:
        result = autostart.install(**overrides)
        autostart_path = result.path
        out.write(f"  ✓ {result.note}\n")
        if result.path:
            out.write(f"  ✓ Wrote {result.path}\n")
    else:
        result = autostart.uninstall(**overrides)
        out.write(f"  (skipped — {result.note})\n")

    # ── step 5: save ──────────────────────────────────────────────────
    out.write("\nStep 5/5: Save configuration\n")
    written = config.save(cfg, target)
    out.write(f"  ✓ Wrote {written}\n\n")

    out.write("Setup complete.\n")
    out.write("  Start the tray with:  pre-buddy tray\n")
    if cfg.device_address or cfg.device_name:
        out.write(f"  Robot:  {cfg.device_name or '?'}  {cfg.device_address or ''}\n")

    return SetupResult(config_path=written, cfg=cfg, autostart_path=autostart_path)


# ── step 1 helpers ────────────────────────────────────────────────────


def _pick_device(
    *,
    inp: TextIO,
    out: TextIO,
    discover: Optional[DiscoverFn],
    scan_timeout_s: float,
) -> Optional[DiscoveredDevice]:
    if discover is None:
        return _prompt_manual_device(inp, out)

    out.write(f"  Scanning for BLE devices ({scan_timeout_s:.0f}s)...\n")
    try:
        devices = list(discover(scan_timeout_s))
    except RuntimeError as exc:
        out.write(f"  ⚠ Scan failed: {exc}\n")
        return _prompt_manual_device(inp, out)

    candidates = _filter_pre_buddy_candidates(devices)
    if not candidates:
        out.write("  No devices found.\n")
        return _prompt_manual_device(inp, out)

    out.write(f"  Found {len(candidates)} device(s):\n")
    for idx, dev in enumerate(candidates, start=1):
        rssi = f"  ({dev.rssi} dBm)" if dev.rssi is not None else ""
        out.write(f"    [{idx}] {dev.name:<24s} {dev.address}{rssi}\n")
    out.write("    [m] Enter an address manually\n")
    out.write("    [s] Skip device selection\n")

    while True:
        out.write("  Pick a device: ")
        out.flush()
        raw = inp.readline().strip().lower()
        if not raw or raw == "s":
            return None
        if raw == "m":
            return _prompt_manual_device(inp, out)
        if raw.isdigit():
            idx = int(raw)
            if 1 <= idx <= len(candidates):
                return candidates[idx - 1]
        out.write(f"  '{raw}' isn't valid — pick a number, 'm', or 's'.\n")


def _prompt_manual_device(inp: TextIO, out: TextIO) -> Optional[DiscoveredDevice]:
    out.write("  Enter the device address (or blank to skip): ")
    out.flush()
    address = inp.readline().strip()
    if not address:
        return None
    out.write("  Enter the device advertising name (or blank for 'pre-buddy'): ")
    out.flush()
    name = inp.readline().strip() or "pre-buddy"
    return DiscoveredDevice(name=name, address=address)


# ── step 2 helpers ────────────────────────────────────────────────────


VALID_CHARACTERS: tuple[str, ...] = ("sage", "sprout", "sentinel")

# Short blurbs surfaced during setup. Stays here (not in character.py)
# because it's a UX string set, not part of the protocol contract.
_CHARACTER_BLURBS: dict[str, str] = {
    "sage": "calm, deliberate, blue idle. Slower reactions, longer blinks.",
    "sprout": "curious, snappy, green idle. Quick reactions, frequent blinks.",
    "sentinel": "watchful, steady, white idle. Returns to centre between tasks.",
}


def _pick_character(inp: TextIO, out: TextIO, *, default: str) -> str:
    out.write("  Which identity should the robot wear?\n")
    for idx, name in enumerate(VALID_CHARACTERS, start=1):
        marker = " (current)" if name == default else ""
        out.write(f"    [{idx}] {name:<9s} — {_CHARACTER_BLURBS[name]}{marker}\n")

    while True:
        out.write(f"  Pick a character [1-{len(VALID_CHARACTERS)}] (default {default}): ")
        out.flush()
        raw = inp.readline().strip().lower()
        if not raw:
            return default
        if raw in VALID_CHARACTERS:
            return raw
        if raw.isdigit():
            idx = int(raw)
            if 1 <= idx <= len(VALID_CHARACTERS):
                return VALID_CHARACTERS[idx - 1]
        out.write(f"  '{raw}' isn't valid — pick a number or one of {VALID_CHARACTERS}.\n")


def _pick_wake_word(inp: TextIO, out: TextIO, *, default: str) -> str:
    out.write(
        "  The robot wakes up when it hears this phrase.\n"
        "  Default is 'hey buddy' — the only one ESP-SR ships out of the\n"
        "  box. Custom phrases need a fine-tuned model, but the choice\n"
        "  is recorded so a future firmware build can pick it up.\n"
    )
    out.write(f"  Wake word (default '{default}'): ")
    out.flush()
    raw = inp.readline().strip()
    if not raw:
        return default
    return raw.lower()


def _prompt_yes_no(inp: TextIO, out: TextIO, message: str, *, default_yes: bool) -> bool:
    suffix = " [Y/n]" if default_yes else " [y/N]"
    while True:
        out.write(message + suffix + ": ")
        out.flush()
        raw = inp.readline().strip().lower()
        if not raw:
            return default_yes
        if raw in {"y", "yes"}:
            return True
        if raw in {"n", "no"}:
            return False
        out.write("  Please answer yes or no.\n")
