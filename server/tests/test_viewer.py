"""Smoke tests for the static viewer + its server helper."""

from __future__ import annotations

import json
import re
from pathlib import Path

import pytest

from pre_buddy import viewer as viewer_module


VIEWER_DIR = viewer_module.default_viewer_dir()


def test_viewer_dir_resolves_to_repo_root() -> None:
    assert VIEWER_DIR.exists(), f"expected viewer dir at {VIEWER_DIR}"
    assert (VIEWER_DIR / "index.html").exists()
    assert (VIEWER_DIR / "viewer.js").exists()
    assert (VIEWER_DIR / "viewer.css").exists()


def test_viewer_html_references_assets() -> None:
    html = (VIEWER_DIR / "index.html").read_text(encoding="utf-8")
    assert 'viewer.css' in html
    assert 'viewer.js' in html


def test_viewer_js_handles_known_led_colors() -> None:
    js = (VIEWER_DIR / "viewer.js").read_text(encoding="utf-8")
    # Every LED name produced by mock_robot.simulate_event must be in the
    # palette, otherwise the viewer will fall back to the default blue and
    # silently drop fidelity.
    for led in ["green", "blue", "purple", "yellow", "amber", "white", "red", "cyan"]:
        assert re.search(rf"\b{led}:\s+'#", js), f"viewer.js missing LED color {led!r}"


def test_viewer_demo_timeline_is_valid() -> None:
    js = (VIEWER_DIR / "viewer.js").read_text(encoding="utf-8")
    # Each entry should reference one of the v1 pre.* event names — guard
    # against typos / drift from shared/protocol/events.md.
    valid_events = {
        "pre.system.wake_word",
        "pre.bg_agents.change",
        "pre.router.decision",
        "pre.confidence.warning",
        "pre.confidence.snapshot",
        "pre.kg.delta",
        "pre.training.progress",
        "pre.scheduler.upcoming",
        "pre.tools.rollup",
        "pre.system.memory_write",
        "pre.system.proximity",
        "pre.system.error",
        "pre.character.set",
    }
    used = set(re.findall(r"'(pre\.[a-z_.]+)'", js))
    assert used, "no pre.* events found in viewer.js — DEMO_TIMELINE removed?"
    leaked = used - valid_events
    assert not leaked, f"viewer.js references unknown pre.* events: {leaked}"


def test_make_handler_creates_handler_bound_to_directory(tmp_path: Path) -> None:
    (tmp_path / "index.html").write_text("ok", encoding="utf-8")
    handler_cls = viewer_module.make_handler(tmp_path)
    # We can't easily instantiate the handler without a socket, but the
    # closure should at least carry the directory string.
    assert "Handler" in handler_cls.__name__


def test_serve_rejects_missing_directory(tmp_path: Path) -> None:
    with pytest.raises(FileNotFoundError):
        viewer_module.serve(directory=tmp_path, open_browser=False, port=0)
