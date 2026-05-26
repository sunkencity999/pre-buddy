"""Tests for the headless tray controller (no pystray, no display)."""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import List, Optional

import pytest

from pre_buddy import autostart, config
from pre_buddy.tray import TrayController, TrayState


# ── fakes ──────────────────────────────────────────────────────────────


@dataclass
class FakeTransport:
    cfg: config.Config
    open_calls: int = 0
    close_calls: int = 0
    fail_on_open: bool = False
    connected: bool = False
    sent_lines: List[str] = field(default_factory=list)

    def open(self) -> None:
        self.open_calls += 1
        if self.fail_on_open:
            raise RuntimeError("simulated open failure")
        self.connected = True

    def close(self) -> None:
        self.close_calls += 1
        self.connected = False

    def send_line(self, line: str) -> None:
        self.sent_lines.append(line)


@dataclass
class ViewerRecorder:
    ports: List[int] = field(default_factory=list)

    def __call__(self, port: int) -> None:
        self.ports.append(port)


# ── helpers ────────────────────────────────────────────────────────────


def _make_controller(
    *,
    cfg_path: Path,
    autostart_path: Path,
    transport: Optional[FakeTransport] = None,
    viewer: Optional[ViewerRecorder] = None,
) -> tuple[TrayController, ViewerRecorder, List[FakeTransport]]:
    """Return a controller wired to fakes + tmpdir-scoped autostart."""
    built: List[FakeTransport] = []

    def factory(c: config.Config) -> FakeTransport:
        if transport is not None:
            return transport
        t = FakeTransport(c)
        built.append(t)
        return t

    recorder = viewer or ViewerRecorder()
    ctrl = TrayController(
        config_path=cfg_path,
        transport_factory=factory,
        viewer_spawner=recorder,
        autostart_overrides={
            "override_system": "Linux",
            "override_linux_path": autostart_path,
        },
    )
    return ctrl, recorder, built


def _seed_config(cfg_path: Path, **fields) -> None:
    cfg = config.Config(**fields)
    config.save(cfg, cfg_path)


# ── connect / disconnect ──────────────────────────────────────────────


def test_initial_state_is_disconnected(tmp_path: Path) -> None:
    _seed_config(tmp_path / "c.json", device_name="pre-buddy")
    ctrl, _, _ = _make_controller(
        cfg_path=tmp_path / "c.json", autostart_path=tmp_path / "x.desktop"
    )
    assert ctrl.state is TrayState.DISCONNECTED
    assert "Disconnected" in ctrl.status_text()


def test_connect_uses_factory_and_opens_transport(tmp_path: Path) -> None:
    _seed_config(tmp_path / "c.json", device_address="AA:01", device_name="pre-buddy")
    ctrl, _, built = _make_controller(
        cfg_path=tmp_path / "c.json", autostart_path=tmp_path / "x.desktop"
    )

    ctrl.connect()
    assert ctrl.state is TrayState.CONNECTED
    assert built[0].open_calls == 1
    assert "Connected to pre-buddy" in ctrl.status_text()


def test_connect_failure_sets_error_state_with_message(tmp_path: Path) -> None:
    _seed_config(tmp_path / "c.json", device_name="x")
    t = FakeTransport(config.Config(device_name="x"), fail_on_open=True)
    ctrl, _, _ = _make_controller(
        cfg_path=tmp_path / "c.json",
        autostart_path=tmp_path / "x.desktop",
        transport=t,
    )
    ctrl.connect()
    assert ctrl.state is TrayState.ERROR
    assert "simulated open failure" in ctrl.last_error
    assert "Error" in ctrl.status_text()


def test_disconnect_closes_transport_and_returns_to_idle(tmp_path: Path) -> None:
    _seed_config(tmp_path / "c.json", device_name="x")
    ctrl, _, built = _make_controller(
        cfg_path=tmp_path / "c.json", autostart_path=tmp_path / "x.desktop"
    )
    ctrl.connect()
    ctrl.disconnect()
    assert ctrl.state is TrayState.DISCONNECTED
    assert built[0].close_calls == 1


def test_disconnect_when_already_disconnected_is_noop(tmp_path: Path) -> None:
    _seed_config(tmp_path / "c.json", device_name="x")
    ctrl, _, _ = _make_controller(
        cfg_path=tmp_path / "c.json", autostart_path=tmp_path / "x.desktop"
    )
    ctrl.disconnect()
    assert ctrl.state is TrayState.DISCONNECTED


def test_toggle_connect_flips_state(tmp_path: Path) -> None:
    _seed_config(tmp_path / "c.json", device_name="x")
    ctrl, _, _ = _make_controller(
        cfg_path=tmp_path / "c.json", autostart_path=tmp_path / "x.desktop"
    )
    ctrl.toggle_connect()
    assert ctrl.state is TrayState.CONNECTED
    ctrl.toggle_connect()
    assert ctrl.state is TrayState.DISCONNECTED


def test_connect_with_no_configured_device_errors_cleanly(tmp_path: Path) -> None:
    _seed_config(tmp_path / "c.json")  # no device
    # Use the default factory so it raises a "no device configured" error.
    from pre_buddy.tray import _default_transport_factory

    ctrl = TrayController(
        config_path=tmp_path / "c.json",
        transport_factory=_default_transport_factory,
        autostart_overrides={
            "override_system": "Linux",
            "override_linux_path": tmp_path / "x.desktop",
        },
    )
    ctrl.connect()
    assert ctrl.state is TrayState.ERROR
    assert "No device configured" in ctrl.last_error


# ── autostart toggle ──────────────────────────────────────────────────


def test_autostart_toggle_installs_then_uninstalls(tmp_path: Path) -> None:
    cfg_path = tmp_path / "c.json"
    desktop = tmp_path / "x.desktop"
    _seed_config(cfg_path, device_name="x")
    ctrl, _, _ = _make_controller(cfg_path=cfg_path, autostart_path=desktop)

    assert not ctrl.is_autostart_enabled()
    ctrl.toggle_autostart()
    assert ctrl.is_autostart_enabled()
    assert desktop.exists()
    # Config mirror updated.
    assert config.load(cfg_path).autostart is True

    ctrl.toggle_autostart()
    assert not ctrl.is_autostart_enabled()
    assert not desktop.exists()
    assert config.load(cfg_path).autostart is False


# ── viewer ────────────────────────────────────────────────────────────


def test_open_viewer_uses_configured_port(tmp_path: Path) -> None:
    cfg_path = tmp_path / "c.json"
    _seed_config(cfg_path, device_name="x", viewer_port=9090)
    ctrl, recorder, _ = _make_controller(
        cfg_path=cfg_path, autostart_path=tmp_path / "x.desktop"
    )
    ctrl.open_viewer()
    assert recorder.ports == [9090]


# ── status text ───────────────────────────────────────────────────────


def test_status_text_for_each_state(tmp_path: Path) -> None:
    cfg_path = tmp_path / "c.json"
    _seed_config(cfg_path, device_name="pre-buddy")
    ctrl, _, _ = _make_controller(
        cfg_path=cfg_path, autostart_path=tmp_path / "x.desktop"
    )

    ctrl.state = TrayState.DISCONNECTED
    assert "Disconnected" in ctrl.status_text()
    ctrl.state = TrayState.SCANNING
    assert "Scanning" in ctrl.status_text()
    ctrl.state = TrayState.CONNECTED
    assert "Connected" in ctrl.status_text()
    ctrl.state = TrayState.ERROR
    ctrl.last_error = "boom"
    assert "Error" in ctrl.status_text() and "boom" in ctrl.status_text()


# ── on_change callback ────────────────────────────────────────────────


def test_on_change_fires_on_state_transitions(tmp_path: Path) -> None:
    _seed_config(tmp_path / "c.json", device_name="x")
    ctrl, _, _ = _make_controller(
        cfg_path=tmp_path / "c.json", autostart_path=tmp_path / "x.desktop"
    )
    ticks: List[TrayState] = []
    ctrl.on_change = lambda: ticks.append(ctrl.state)

    ctrl.connect()
    ctrl.disconnect()

    # Expect at least: SCANNING (start of connect), CONNECTED, DISCONNECTED.
    assert TrayState.SCANNING in ticks
    assert TrayState.CONNECTED in ticks
    assert TrayState.DISCONNECTED in ticks


def test_on_change_callback_exceptions_dont_break_state(tmp_path: Path) -> None:
    # A buggy UI shouldn't take the controller down with it.
    _seed_config(tmp_path / "c.json", device_name="x")
    ctrl, _, _ = _make_controller(
        cfg_path=tmp_path / "c.json", autostart_path=tmp_path / "x.desktop"
    )

    def explode() -> None:
        raise RuntimeError("ui broken")

    ctrl.on_change = explode
    ctrl.connect()
    assert ctrl.state is TrayState.CONNECTED


# ── character ─────────────────────────────────────────────────────────


def test_set_character_persists_to_config(tmp_path: Path) -> None:
    _seed_config(tmp_path / "c.json", device_name="x", character="sage")
    ctrl, _, _ = _make_controller(
        cfg_path=tmp_path / "c.json", autostart_path=tmp_path / "x.desktop"
    )
    ctrl.set_character("sprout")
    assert ctrl.current_character() == "sprout"
    assert config.load(tmp_path / "c.json").character == "sprout"


def test_set_character_rejects_unknown_name(tmp_path: Path) -> None:
    _seed_config(tmp_path / "c.json", device_name="x")
    ctrl, _, _ = _make_controller(
        cfg_path=tmp_path / "c.json", autostart_path=tmp_path / "x.desktop"
    )
    with pytest.raises(ValueError):
        ctrl.set_character("dreamer")


def test_set_character_when_connected_pushes_pre_character_set(tmp_path: Path) -> None:
    # The tray is the only path through which the device discovers the
    # user's choice mid-session. Confirm a pre.character.set line lands
    # on the transport whenever set_character is called while connected.
    _seed_config(tmp_path / "c.json", device_name="x", character="sage")
    t = FakeTransport(config.Config(character="sage"))
    ctrl, _, _ = _make_controller(
        cfg_path=tmp_path / "c.json",
        autostart_path=tmp_path / "x.desktop",
        transport=t,
    )
    ctrl.connect()
    # Reset sent_lines so we ignore the connect-time resync push.
    t.sent_lines.clear()

    ctrl.set_character("sentinel")

    assert len(t.sent_lines) == 1
    line = t.sent_lines[0]
    assert '"pre.character.set"' in line
    assert '"sentinel"' in line


def test_set_character_when_disconnected_is_silent(tmp_path: Path) -> None:
    # No transport → no BLE write attempted. Picking offline must still
    # persist to disk so the next connect picks it up.
    _seed_config(tmp_path / "c.json", device_name="x", character="sage")
    ctrl, _, _ = _make_controller(
        cfg_path=tmp_path / "c.json", autostart_path=tmp_path / "x.desktop"
    )
    ctrl.set_character("sprout")
    assert ctrl.current_character() == "sprout"


def test_connect_resyncs_character_to_device(tmp_path: Path) -> None:
    # When a user pre-picks a character offline (or via the wizard), then
    # later opens the tray and clicks Connect, the device should learn
    # the choice immediately — no manual menu click required.
    _seed_config(tmp_path / "c.json", device_name="x", character="sprout")
    ctrl, _, built = _make_controller(
        cfg_path=tmp_path / "c.json", autostart_path=tmp_path / "x.desktop"
    )
    ctrl.connect()
    sent = built[0].sent_lines
    assert sent, "expected a pre.character.set push on connect"
    assert any('"pre.character.set"' in line and '"sprout"' in line for line in sent)


def test_current_character_falls_back_to_sage_on_garbage(tmp_path: Path) -> None:
    # Manually corrupt the on-disk character so the tray sees a value
    # it doesn't know. It should still surface "sage" instead of erroring,
    # so the menu radio always points at a valid item.
    cfg_path = tmp_path / "c.json"
    cfg_path.write_text(
        '{"device_name": "x", "character": "weirdmood", "schema_version": 1}\n',
        encoding="utf-8",
    )
    ctrl, _, _ = _make_controller(
        cfg_path=cfg_path, autostart_path=tmp_path / "x.desktop"
    )
    assert ctrl.current_character() == "sage"
