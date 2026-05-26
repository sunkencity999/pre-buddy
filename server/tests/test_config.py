"""Config storage round-trip + forward-compat tests."""

from __future__ import annotations

import json
import os
from pathlib import Path

import pytest

from pre_buddy import config


def test_default_returns_safe_values_when_no_file(tmp_path: Path) -> None:
    cfg = config.load(tmp_path / "missing.json")
    assert cfg.device_address is None
    assert cfg.device_name is None
    assert cfg.autostart is False
    assert cfg.viewer_port == 7750
    assert cfg.pre_ws_url == "ws://localhost:7749"
    assert cfg.schema_version == config.SCHEMA_VERSION


def test_save_and_load_round_trip(tmp_path: Path) -> None:
    path = tmp_path / "config.json"
    cfg = config.Config(
        device_address="AA:BB:CC:DD:EE:FF",
        device_name="pre-buddy",
        autostart=True,
        viewer_port=8080,
        last_connected_iso="2026-05-26T19:00:00Z",
    )
    config.save(cfg, path)

    loaded = config.load(path)
    assert loaded.device_address == "AA:BB:CC:DD:EE:FF"
    assert loaded.device_name == "pre-buddy"
    assert loaded.autostart is True
    assert loaded.viewer_port == 8080
    assert loaded.last_connected_iso == "2026-05-26T19:00:00Z"


def test_save_uses_atomic_replace(tmp_path: Path) -> None:
    path = tmp_path / "config.json"
    config.save(config.Config(device_name="a"), path)
    # No stray temp files left behind under the same dir.
    stray = [p for p in path.parent.iterdir() if p.name.endswith(".tmp")]
    assert stray == []


def test_corrupt_file_is_treated_as_missing(tmp_path: Path) -> None:
    path = tmp_path / "config.json"
    path.write_text("not json{{{", encoding="utf-8")
    cfg = config.load(path)
    # Defaults, not an exception.
    assert cfg.device_address is None
    assert cfg.autostart is False


def test_non_object_file_is_treated_as_missing(tmp_path: Path) -> None:
    path = tmp_path / "config.json"
    path.write_text("[1,2,3]", encoding="utf-8")
    cfg = config.load(path)
    assert cfg.device_address is None


def test_unknown_keys_round_trip_via_extras(tmp_path: Path) -> None:
    # Forward compat: when a newer release writes an unknown field, an
    # older release MUST preserve it through save/load so we don't drop
    # state during a downgrade window.
    path = tmp_path / "config.json"
    path.write_text(
        json.dumps(
            {
                "device_name": "pre-buddy",
                "schema_version": 1,
                "viewer_port": 7750,
                "future_feature_flag": True,
                "future_object": {"a": 1},
            }
        ),
        encoding="utf-8",
    )

    cfg = config.load(path)
    assert cfg.extras == {"future_feature_flag": True, "future_object": {"a": 1}}

    cfg.device_name = "pre-buddy-2"
    config.save(cfg, path)

    raw = json.loads(path.read_text(encoding="utf-8"))
    assert raw["device_name"] == "pre-buddy-2"
    assert raw["future_feature_flag"] is True
    assert raw["future_object"] == {"a": 1}


def test_update_helper_writes_changes_and_returns_typed_view(tmp_path: Path) -> None:
    path = tmp_path / "config.json"
    cfg = config.update(path, device_address="X", autostart=True)
    assert cfg.device_address == "X"
    assert cfg.autostart is True

    # Round-trips.
    reloaded = config.load(path)
    assert reloaded.device_address == "X"


def test_update_rejects_unknown_fields(tmp_path: Path) -> None:
    path = tmp_path / "config.json"
    with pytest.raises(KeyError):
        config.update(path, made_up_field="x")


def test_default_config_dir_uses_xdg_on_linux(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    monkeypatch.setattr("pre_buddy.config.platform.system", lambda: "Linux")
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "xdg"))
    assert config.default_config_dir() == tmp_path / "xdg" / "pre-buddy"


def test_default_config_dir_uses_appdata_on_windows(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    monkeypatch.setattr("pre_buddy.config.platform.system", lambda: "Windows")
    monkeypatch.setenv("APPDATA", str(tmp_path / "appdata"))
    assert config.default_config_dir() == tmp_path / "appdata" / "pre-buddy"


def test_default_config_dir_falls_back_when_xdg_unset(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr("pre_buddy.config.platform.system", lambda: "Darwin")
    monkeypatch.delenv("XDG_CONFIG_HOME", raising=False)
    expected = Path.home() / ".config" / "pre-buddy"
    assert config.default_config_dir() == expected
