"""PRE Buddy system tray app.

Two layers:

- :class:`TrayController` — pure state machine. Holds the BleNusTransport,
  responds to verbs (``connect``, ``disconnect``, ``toggle_autostart``,
  ``open_viewer``, ``run_setup``, ``quit``), and exposes status strings.
  Pystray and Pillow are NOT imported by this layer; tests exercise it
  with a fake transport and an in-memory autostart override.

- :func:`run_tray` — the pystray glue. Loads the icon PNG, builds the
  menu, binds it to the controller, and blocks in pystray's main loop.
  This is the only place that imports pystray/Pillow.

The tray runs in-process: clicking "Connect" opens the BLE transport
right here; clicking "Disconnect" closes it. No separate background
``pre-buddy serve`` to manage.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
import threading
from dataclasses import dataclass, field
from enum import Enum
from pathlib import Path
from typing import Callable, Optional

from . import autostart, config


class TrayState(str, Enum):
    DISCONNECTED = "disconnected"
    SCANNING = "scanning"
    CONNECTED = "connected"
    ERROR = "error"


# ── controller ────────────────────────────────────────────────────────


TransportFactory = Callable[[config.Config], object]
"""Build the transport for a given config. Default factory uses bleak;
tests inject a fake that records open/close calls."""


def _default_transport_factory(cfg: config.Config) -> object:
    """Build a real BleNusTransport from the saved config.

    Imported lazily so the tray controller is importable on a machine
    without bleak.
    """
    if not cfg.device_address and not cfg.device_name:
        raise RuntimeError(
            "No device configured. Run `pre-buddy setup` first."
        )
    from .transport_ble import BleakNusBackend, BleNusTransport

    backend = BleakNusBackend(address=cfg.device_address, name=cfg.device_name)
    return BleNusTransport(backend)


ViewerSpawner = Callable[[int], None]
"""Spawn the scenario viewer at a given port. Default uses subprocess;
tests inject a recorder."""


def _default_viewer_spawner(port: int) -> None:
    # Spawn as a detached subprocess so the tray stays responsive and
    # the viewer outlives a "Quit" if the user keeps it open. We pin to
    # the same Python that runs the tray to avoid PATH surprises.
    subprocess.Popen(
        [sys.executable, "-m", "pre_buddy.cli", "viewer", "--port", str(port)],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )


@dataclass
class TrayController:
    """Headless state machine for the tray app. Safe to unit-test."""

    config_path: Optional[Path] = None
    transport_factory: TransportFactory = field(default=_default_transport_factory)
    viewer_spawner: ViewerSpawner = field(default=_default_viewer_spawner)
    autostart_overrides: dict = field(default_factory=dict)
    on_change: Callable[[], None] = field(default=lambda: None)

    state: TrayState = TrayState.DISCONNECTED
    last_error: str = ""
    _transport: object | None = None
    _lock: threading.Lock = field(default_factory=threading.Lock)

    # ── lifecycle ─────────────────────────────────────────────────────

    @property
    def cfg(self) -> config.Config:
        return config.load(self.config_path)

    def status_text(self) -> str:
        """One-line status for the tray title / menu header."""
        cfg = self.cfg
        target = cfg.device_name or cfg.device_address or "(no device)"
        if self.state is TrayState.CONNECTED:
            return f"● Connected to {target}"
        if self.state is TrayState.SCANNING:
            return f"○ Scanning for {target}..."
        if self.state is TrayState.ERROR:
            return f"⚠ Error: {self.last_error or 'unknown'}"
        return f"● Disconnected ({target})"

    def is_connected(self) -> bool:
        return self.state is TrayState.CONNECTED

    # ── verbs ─────────────────────────────────────────────────────────

    def connect(self) -> None:
        """Open the BLE transport. Idempotent on already-connected."""
        with self._lock:
            if self.state is TrayState.CONNECTED:
                return
            self.state = TrayState.SCANNING
            self.last_error = ""
        self._emit_change()
        try:
            transport = self.transport_factory(self.cfg)
            # The transport's open() blocks while bleak scans + connects.
            transport.open()  # type: ignore[attr-defined]
        except Exception as exc:  # noqa: BLE001 - surface whatever failed
            with self._lock:
                self.state = TrayState.ERROR
                self.last_error = str(exc)
                self._transport = None
            self._emit_change()
            return
        with self._lock:
            self._transport = transport
            self.state = TrayState.CONNECTED
        self._emit_change()

    def disconnect(self) -> None:
        """Close the BLE transport. Idempotent on already-disconnected."""
        with self._lock:
            transport = self._transport
            self._transport = None
        if transport is not None:
            try:
                transport.close()  # type: ignore[attr-defined]
            except Exception as exc:  # noqa: BLE001
                # We're tearing down — log via last_error but stay in
                # DISCONNECTED state so the user can try a fresh connect.
                self.last_error = f"close: {exc}"
        with self._lock:
            self.state = TrayState.DISCONNECTED
        self._emit_change()

    def toggle_connect(self) -> None:
        if self.state is TrayState.CONNECTED:
            self.disconnect()
        else:
            self.connect()

    def is_autostart_enabled(self) -> bool:
        return autostart.is_installed(**self.autostart_overrides)

    def set_autostart(self, enabled: bool) -> None:
        if enabled:
            autostart.install(**self.autostart_overrides)
        else:
            autostart.uninstall(**self.autostart_overrides)
        # Mirror the choice into the config file so other components see it.
        cfg = self.cfg
        cfg.autostart = enabled
        config.save(cfg, self.config_path)
        self._emit_change()

    def toggle_autostart(self) -> None:
        self.set_autostart(not self.is_autostart_enabled())

    def open_viewer(self) -> None:
        self.viewer_spawner(self.cfg.viewer_port)

    def run_setup(self) -> None:
        # Launch the wizard in a terminal subprocess so the tray itself
        # stays responsive. The setup wizard rewrites our config file
        # under us; the tray will pick up the new device on next connect.
        subprocess.Popen(
            [sys.executable, "-m", "pre_buddy.cli", "setup"],
            stdout=None, stderr=None,
        )

    def quit_app(self) -> None:
        self.disconnect()

    # ── plumbing ──────────────────────────────────────────────────────

    def _emit_change(self) -> None:
        try:
            self.on_change()
        except Exception:  # noqa: BLE001
            pass


# ── pystray glue ─────────────────────────────────────────────────────


def _load_tray_icon():
    """Load the bundled PNG via Pillow. Raises ImportError if Pillow
    isn't installed."""
    from PIL import Image  # type: ignore[import-not-found]

    here = Path(__file__).resolve().parent
    candidates = [
        here / "assets" / "tray_icon.png",
        here / "tray_icon.png",
    ]
    for path in candidates:
        if path.exists():
            return Image.open(path)
    raise FileNotFoundError(
        f"tray_icon.png not found under {here / 'assets'} — re-install the package?"
    )


def _build_menu(controller: TrayController):
    """Build a pystray.Menu bound to the controller.

    The closures here capture ``controller`` and call ``icon.update_menu()``
    after every action so the labels reflect the new state.
    """
    import pystray  # type: ignore[import-not-found]

    Item = pystray.MenuItem
    Sep = pystray.Menu.SEPARATOR

    def _refresh(icon):
        icon.title = "PRE Buddy"
        icon.update_menu()

    def _on_toggle_connect(icon, _item):
        controller.toggle_connect()
        _refresh(icon)

    def _on_toggle_autostart(icon, _item):
        controller.toggle_autostart()
        _refresh(icon)

    def _on_open_viewer(icon, _item):
        controller.open_viewer()
        _refresh(icon)

    def _on_run_setup(icon, _item):
        controller.run_setup()
        _refresh(icon)

    def _on_quit(icon, _item):
        controller.quit_app()
        icon.stop()

    return pystray.Menu(
        Item(lambda _: controller.status_text(), None, enabled=False),
        Sep,
        Item(
            lambda _: "Disconnect" if controller.is_connected() else "Connect",
            _on_toggle_connect,
            default=True,
        ),
        Sep,
        Item("Open Viewer", _on_open_viewer),
        Item("Run Setup…", _on_run_setup),
        Sep,
        Item(
            "Launch at login",
            _on_toggle_autostart,
            checked=lambda _: controller.is_autostart_enabled(),
        ),
        Sep,
        Item("Quit", _on_quit),
    )


def run_tray(args: argparse.Namespace) -> int:
    """Block in pystray's main loop until the user picks Quit.

    Returns the process exit code (0 on graceful quit).
    """
    try:
        import pystray  # type: ignore[import-not-found]  # noqa: F401
    except ImportError as exc:
        raise ImportError(
            "tray requires the 'pystray' and 'Pillow' packages. "
            "Install with: pip install 'pre-buddy[tray]'"
        ) from exc

    icon_image = _load_tray_icon()
    controller = TrayController()
    menu = _build_menu(controller)

    import pystray  # type: ignore[import-not-found]

    icon = pystray.Icon("pre-buddy", icon=icon_image, title="PRE Buddy", menu=menu)
    controller.on_change = lambda: icon.update_menu()

    if getattr(args, "once", False):
        # Smoke test path: don't enter the run loop, just verify the
        # icon and menu construct without error. Used by CI on
        # platforms that lack a display.
        return 0

    icon.run()
    return 0
