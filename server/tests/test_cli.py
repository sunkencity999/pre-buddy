"""Tests for the standalone development CLI."""

from __future__ import annotations

import io
import json
from contextlib import redirect_stdout

from pre_buddy.cli import main


def _run(argv):
    buf = io.StringIO()
    with redirect_stdout(buf):
        rc = main(argv)
    return rc, buf.getvalue()


def test_version_command():
    rc, out = _run(["version"])
    assert rc == 0
    assert out.startswith("pre-buddy ")


def test_emit_wake_word_produces_valid_event_line():
    rc, out = _run(["emit", "pre.system.wake_word", "--mic", "left"])
    assert rc == 0
    obj = json.loads(out.strip())
    assert obj["event"] == "pre.system.wake_word"
    assert obj["data"] == {"source_mic": "left"}


def test_emit_bg_agent_change_with_timestamp():
    rc, out = _run(
        [
            "emit",
            "pre.bg_agents.change",
            "--agent-id",
            "lib-1",
            "--state",
            "finished",
            "--tier",
            "frontier",
            "--timestamp",
        ]
    )
    assert rc == 0
    obj = json.loads(out.strip())
    assert obj["event"] == "pre.bg_agents.change"
    assert obj["data"] == {
        "agent_id": "lib-1",
        "state": "finished",
        "tier": "frontier",
    }
    assert "ts" in obj


def test_emit_character_set():
    rc, out = _run(["emit", "pre.character.set", "--character", "sentinel"])
    assert rc == 0
    obj = json.loads(out.strip())
    assert obj["data"] == {"character": "sentinel"}


def test_emit_router_decision():
    rc, out = _run(
        [
            "emit",
            "pre.router.decision",
            "--from-tier",
            "fast",
            "--to-tier",
            "frontier",
            "--reason",
            "complexity",
        ]
    )
    assert rc == 0
    obj = json.loads(out.strip())
    assert obj["event"] == "pre.router.decision"
    assert obj["data"]["to_tier"] == "frontier"


def test_serve_demo_prints_event_lines_and_summary():
    rc, out = _run(["serve", "--demo"])
    assert rc == 0
    lines = [line for line in out.splitlines() if line.strip()]
    # 5 demo events + 1 summary line
    assert len(lines) == 6
    assert lines[-1].startswith("sent=")
    assert any('"event":"pre.system.wake_word"' in line for line in lines)
