"""Setup wizard tests — drive the interactive prompts via StringIO."""

from __future__ import annotations

import io
from pathlib import Path

import pytest

from pre_buddy import autostart, config, setup_wizard
from pre_buddy.setup_wizard import DiscoveredDevice


def _run(
    *,
    answers: list[str],
    config_path: Path,
    discover=None,
    autostart_overrides=None,
):
    inp = io.StringIO("\n".join(answers) + "\n")
    out = io.StringIO()
    result = setup_wizard.run_setup(
        inp=inp,
        out=out,
        discover=discover,
        config_path=config_path,
        autostart_overrides=autostart_overrides
        or {"override_system": "Linux", "override_linux_path": config_path.parent / "fake.desktop"},
    )
    return result, out.getvalue()


# ── device discovery flow ─────────────────────────────────────────────


def test_picks_first_scanned_device_when_user_chooses_1(tmp_path: Path) -> None:
    cfg_path = tmp_path / "config.json"
    discovered = [
        DiscoveredDevice(name="pre-buddy", address="AA:BB:CC:01", rssi=-55),
        DiscoveredDevice(name="someone else", address="AA:BB:CC:02", rssi=-80),
    ]

    result, log = _run(
        answers=["1", "", "n"],
        config_path=cfg_path,
        discover=lambda timeout: discovered,
    )

    assert result.cfg.device_address == "AA:BB:CC:01"
    assert result.cfg.device_name == "pre-buddy"
    assert "Found 2 device(s)" in log


def test_promotes_pre_buddy_candidates_to_top_of_list(tmp_path: Path) -> None:
    cfg_path = tmp_path / "config.json"
    # Order intentionally puts the real one second; the wizard should
    # surface it as [1].
    discovered = [
        DiscoveredDevice(name="someone-else", address="AA:01"),
        DiscoveredDevice(name="PRE-Buddy", address="AA:02"),
    ]
    result, log = _run(
        answers=["1", "", "n"],
        config_path=cfg_path,
        discover=lambda timeout: discovered,
    )
    assert result.cfg.device_address == "AA:02"
    # The log lists PRE-Buddy at index 1.
    assert "[1] PRE-Buddy" in log


def test_manual_entry_overrides_scan(tmp_path: Path) -> None:
    cfg_path = tmp_path / "config.json"
    discovered = [DiscoveredDevice(name="x", address="AA:01")]

    result, log = _run(
        answers=["m", "FF:EE:DD:CC", "my-bot", "", "n"],
        config_path=cfg_path,
        discover=lambda timeout: discovered,
    )
    assert result.cfg.device_address == "FF:EE:DD:CC"
    assert result.cfg.device_name == "my-bot"


def test_skip_keeps_config_device_fields_unset(tmp_path: Path) -> None:
    cfg_path = tmp_path / "config.json"
    discovered = [DiscoveredDevice(name="x", address="AA:01")]
    result, _ = _run(
        answers=["s", "", "n"],
        config_path=cfg_path,
        discover=lambda timeout: discovered,
    )
    assert result.cfg.device_address is None
    assert result.cfg.device_name is None


def test_no_devices_found_falls_through_to_manual(tmp_path: Path) -> None:
    cfg_path = tmp_path / "config.json"
    result, log = _run(
        answers=["AA:11:22:33", "", "", "n"],
        config_path=cfg_path,
        discover=lambda timeout: [],
    )
    assert "No devices found" in log
    assert result.cfg.device_address == "AA:11:22:33"
    # Empty input → default name 'pre-buddy'.
    assert result.cfg.device_name == "pre-buddy"


def test_bleak_missing_falls_through_to_manual(tmp_path: Path) -> None:
    cfg_path = tmp_path / "config.json"

    def raises(_timeout):
        raise RuntimeError("BLE scan requires the 'bleak' package.")

    result, log = _run(
        answers=["", "", "n"],   # blank address → skip manual entry too
        config_path=cfg_path,
        discover=raises,
    )
    assert "Scan failed" in log
    assert result.cfg.device_address is None


def test_invalid_pick_reprompts_until_valid(tmp_path: Path) -> None:
    cfg_path = tmp_path / "config.json"
    discovered = [DiscoveredDevice(name="pre-buddy", address="AA:01")]
    result, log = _run(
        answers=["999", "bogus", "1", "", "n"],
        config_path=cfg_path,
        discover=lambda timeout: discovered,
    )
    assert result.cfg.device_address == "AA:01"
    assert "isn't valid" in log


# ── autostart toggle ──────────────────────────────────────────────────


def test_autostart_yes_installs_via_overrides(tmp_path: Path) -> None:
    cfg_path = tmp_path / "config.json"
    desktop = tmp_path / "fake.desktop"
    overrides = {"override_system": "Linux", "override_linux_path": desktop}
    # No devices found → falls through to manual entry; blank skips it;
    # then blank for default character; then "y" for autostart.
    inp = io.StringIO("\n".join(["", "", "y"]) + "\n")
    out = io.StringIO()
    result = setup_wizard.run_setup(
        inp=inp,
        out=out,
        discover=lambda _: [],
        config_path=cfg_path,
        autostart_overrides=overrides,
    )
    assert result.cfg.autostart is True
    assert desktop.exists()
    assert autostart.is_installed(
        override_system="Linux", override_linux_path=desktop
    )


def test_autostart_no_uninstalls_via_overrides(tmp_path: Path) -> None:
    cfg_path = tmp_path / "config.json"
    desktop = tmp_path / "fake.desktop"
    # Pre-existing entry should be cleaned up when the user says no.
    desktop.parent.mkdir(parents=True, exist_ok=True)
    desktop.write_text("stale", encoding="utf-8")
    overrides = {"override_system": "Linux", "override_linux_path": desktop}
    inp = io.StringIO("\n".join(["", "", "n"]) + "\n")
    out = io.StringIO()
    setup_wizard.run_setup(
        inp=inp,
        out=out,
        discover=lambda _: [],
        config_path=cfg_path,
        autostart_overrides=overrides,
    )
    assert not desktop.exists()


# ── persistence ───────────────────────────────────────────────────────


def test_wizard_writes_config_to_disk(tmp_path: Path) -> None:
    cfg_path = tmp_path / "config.json"
    discovered = [DiscoveredDevice(name="pre-buddy", address="AA:01")]
    _run(
        answers=["1", "", "n"],
        config_path=cfg_path,
        discover=lambda _: discovered,
    )
    assert cfg_path.exists()
    cfg = config.load(cfg_path)
    assert cfg.device_address == "AA:01"
    assert cfg.autostart is False
    assert cfg.character == "sage"  # default kept when user pressed Enter


def test_wizard_runnable_with_no_scan_function(tmp_path: Path) -> None:
    # discover=None goes straight to manual entry — the path users without
    # bleak installed take.
    cfg_path = tmp_path / "config.json"
    overrides = {"override_system": "Linux", "override_linux_path": tmp_path / "x.desktop"}
    inp = io.StringIO("\n".join(["AA:99", "manual-bot", "", "n"]) + "\n")
    out = io.StringIO()
    result = setup_wizard.run_setup(
        inp=inp,
        out=out,
        discover=None,
        config_path=cfg_path,
        autostart_overrides=overrides,
    )
    assert result.cfg.device_address == "AA:99"
    assert result.cfg.device_name == "manual-bot"


# ── character pick ────────────────────────────────────────────────────


def test_wizard_pick_character_by_number(tmp_path: Path) -> None:
    cfg_path = tmp_path / "config.json"
    discovered = [DiscoveredDevice(name="pre-buddy", address="AA:01")]
    # "1" pick device, "2" pick sprout, "n" autostart off.
    result, _ = _run(
        answers=["1", "2", "n"],
        config_path=cfg_path,
        discover=lambda _: discovered,
    )
    assert result.cfg.character == "sprout"


def test_wizard_pick_character_by_name(tmp_path: Path) -> None:
    cfg_path = tmp_path / "config.json"
    discovered = [DiscoveredDevice(name="pre-buddy", address="AA:01")]
    result, _ = _run(
        answers=["1", "sentinel", "n"],
        config_path=cfg_path,
        discover=lambda _: discovered,
    )
    assert result.cfg.character == "sentinel"


def test_wizard_character_blank_keeps_existing_default(tmp_path: Path) -> None:
    cfg_path = tmp_path / "config.json"
    # Pre-seed sprout so we can verify "blank input keeps current."
    config.save(config.Config(character="sprout"), cfg_path)
    discovered = [DiscoveredDevice(name="pre-buddy", address="AA:01")]
    result, _ = _run(
        answers=["1", "", "n"],
        config_path=cfg_path,
        discover=lambda _: discovered,
    )
    assert result.cfg.character == "sprout"


def test_wizard_character_invalid_input_reprompts(tmp_path: Path) -> None:
    cfg_path = tmp_path / "config.json"
    discovered = [DiscoveredDevice(name="pre-buddy", address="AA:01")]
    result, log = _run(
        answers=["1", "wizard", "9", "3", "n"],
        config_path=cfg_path,
        discover=lambda _: discovered,
    )
    assert result.cfg.character == "sentinel"
    assert "isn't valid" in log
