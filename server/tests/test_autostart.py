"""Cross-platform autostart installer tests.

These run on any host because every platform path accepts an override
that points at a tmp directory.
"""

from __future__ import annotations

from pathlib import Path

from pre_buddy import autostart


# ── macOS LaunchAgent ──────────────────────────────────────────────────


def test_macos_install_writes_well_formed_plist(tmp_path: Path) -> None:
    plist = tmp_path / "LaunchAgents" / f"{autostart.APP_ID}.plist"
    result = autostart.install(
        override_system="Darwin",
        override_macos_path=plist,
        override_command='"/usr/local/bin/pre-buddy" tray',
    )
    assert result.installed
    assert result.path == plist
    text = plist.read_text(encoding="utf-8")
    assert "<?xml" in text
    assert f"<string>{autostart.APP_ID}</string>" in text
    assert "<key>RunAtLoad</key>" in text
    assert "<true/>" in text
    # ProgramArguments split correctly.
    assert "<string>/usr/local/bin/pre-buddy</string>" in text
    assert "<string>tray</string>" in text


def test_macos_install_is_idempotent(tmp_path: Path) -> None:
    plist = tmp_path / f"{autostart.APP_ID}.plist"
    autostart.install(
        override_system="Darwin",
        override_macos_path=plist,
        override_command='"/a" b',
    )
    autostart.install(
        override_system="Darwin",
        override_macos_path=plist,
        override_command='"/c" d',
    )
    text = plist.read_text(encoding="utf-8")
    assert "<string>/c</string>" in text   # latest write wins
    assert "<string>/a</string>" not in text


def test_macos_uninstall_removes_and_is_idempotent(tmp_path: Path) -> None:
    plist = tmp_path / f"{autostart.APP_ID}.plist"
    autostart.install(
        override_system="Darwin",
        override_macos_path=plist,
        override_command='"x" tray',
    )
    assert plist.exists()
    autostart.uninstall(override_system="Darwin", override_macos_path=plist)
    assert not plist.exists()
    # Second call doesn't raise.
    autostart.uninstall(override_system="Darwin", override_macos_path=plist)


def test_macos_is_installed_reflects_file_presence(tmp_path: Path) -> None:
    plist = tmp_path / f"{autostart.APP_ID}.plist"
    assert not autostart.is_installed(
        override_system="Darwin", override_macos_path=plist
    )
    autostart.install(
        override_system="Darwin",
        override_macos_path=plist,
        override_command='"x" tray',
    )
    assert autostart.is_installed(
        override_system="Darwin", override_macos_path=plist
    )


# ── Linux .desktop ────────────────────────────────────────────────────


def test_linux_install_writes_freedesktop_compliant_entry(tmp_path: Path) -> None:
    desktop = tmp_path / "autostart" / "pre-buddy.desktop"
    result = autostart.install(
        override_system="Linux",
        override_linux_path=desktop,
        override_command='"/usr/bin/pre-buddy" tray',
    )
    assert result.installed
    text = desktop.read_text(encoding="utf-8")
    assert text.startswith("[Desktop Entry]\n")
    assert "Type=Application" in text
    assert f"Name={autostart.APP_NAME}" in text
    assert 'Exec="/usr/bin/pre-buddy" tray' in text
    # GNOME-specific opt-in flag (harmless on KDE/XFCE).
    assert "X-GNOME-Autostart-enabled=true" in text


def test_linux_uninstall_removes_file(tmp_path: Path) -> None:
    desktop = tmp_path / "pre-buddy.desktop"
    autostart.install(
        override_system="Linux",
        override_linux_path=desktop,
        override_command='"/u/b" tray',
    )
    autostart.uninstall(override_system="Linux", override_linux_path=desktop)
    assert not desktop.exists()


def test_linux_is_installed_reflects_file_presence(tmp_path: Path) -> None:
    desktop = tmp_path / "pre-buddy.desktop"
    assert not autostart.is_installed(
        override_system="Linux", override_linux_path=desktop
    )
    autostart.install(
        override_system="Linux",
        override_linux_path=desktop,
        override_command='"x" tray',
    )
    assert autostart.is_installed(
        override_system="Linux", override_linux_path=desktop
    )


# ── Windows registry (via dict override) ──────────────────────────────


def test_windows_install_writes_to_run_key_via_override() -> None:
    fake_registry: dict[str, str] = {}
    result = autostart.install(
        override_system="Windows",
        override_windows_registry=fake_registry,
        override_command='"C:\\app\\pre-buddy.exe" tray',
    )
    assert result.installed
    assert fake_registry[autostart.APP_NAME] == '"C:\\app\\pre-buddy.exe" tray'


def test_windows_uninstall_removes_from_run_key() -> None:
    fake_registry: dict[str, str] = {autostart.APP_NAME: '"x" tray'}
    autostart.uninstall(
        override_system="Windows", override_windows_registry=fake_registry
    )
    assert autostart.APP_NAME not in fake_registry
    # Second uninstall is a no-op.
    autostart.uninstall(
        override_system="Windows", override_windows_registry=fake_registry
    )


def test_windows_is_installed_uses_registry_dict() -> None:
    empty: dict[str, str] = {}
    assert not autostart.is_installed(
        override_system="Windows", override_windows_registry=empty
    )
    populated = {autostart.APP_NAME: '"x" tray'}
    assert autostart.is_installed(
        override_system="Windows", override_windows_registry=populated
    )


# ── unsupported platform ──────────────────────────────────────────────


def test_install_on_unsupported_platform_returns_not_installed() -> None:
    result = autostart.install(override_system="Plan9", override_command="x")
    assert not result.installed
    assert "unsupported platform" in result.note


# ── command resolution ────────────────────────────────────────────────


def test_resolve_command_falls_back_to_python_module_when_pre_buddy_not_on_path(
    monkeypatch,
) -> None:
    # Simulate the binary not being on PATH (e.g. fresh pip install
    # without venv activation). The resolver should give a usable command.
    monkeypatch.setattr("pre_buddy.autostart.shutil.which", lambda _: None)
    cmd = autostart._resolve_command()
    assert "pre_buddy.cli" in cmd or "pre-buddy" in cmd
    assert "tray" in cmd
