"""Tests for the standalone development CLI."""

from __future__ import annotations

import io
import json
from contextlib import redirect_stdout, redirect_stderr
from pathlib import Path

from pre_buddy.cli import main


def _run(argv):
    buf = io.StringIO()
    with redirect_stdout(buf):
        rc = main(argv)
    return rc, buf.getvalue()


def _run_capture_err(argv):
    out = io.StringIO()
    err = io.StringIO()
    with redirect_stdout(out), redirect_stderr(err):
        rc = main(argv)
    return rc, out.getvalue(), err.getvalue()


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


def test_simulate_replays_scenario_and_prints_summary(tmp_path):
    playback = tmp_path / "scenario.jsonl"
    playback.write_text(
        "\n".join(
            [
                '{"event":"pre.system.wake_word","data":{"source_mic":"left"}}',
                '{"event":"pre.system.error","data":{"code":"E1","message":"boom"}}',
                "",
            ]
        ),
        encoding="utf-8",
    )

    rc, out = _run(["simulate", "--playback", str(playback), "--character", "sprout"])
    assert rc == 0
    lines = [line for line in out.splitlines() if line.strip()]
    assert lines[0].startswith("[01] pre.system.wake_word")
    assert "led=yellow" in lines[0]
    assert "led=red" in lines[1]
    assert lines[-1] == "simulated=2 character=sprout severity=normal"


def test_simulate_csv_output(tmp_path):
    playback = tmp_path / "scenario.jsonl"
    playback.write_text(
        '{"event":"pre.system.error","data":{"code":"E1","message":"boom"}}\n',
        encoding="utf-8",
    )

    rc, out = _run([
        "simulate",
        "--playback",
        str(playback),
        "--character",
        "sage",
        "--format",
        "csv",
    ])
    assert rc == 0
    lines = [line for line in out.splitlines() if line.strip()]
    assert lines[0].startswith("scenario_index,source_event,led")
    assert "pre.system.error" in lines[1]


def test_bridge_from_file_maps_and_prints_pre_events(tmp_path: Path):
    fixture = tmp_path / "pre.jsonl"
    fixture.write_text(
        "\n".join([
            '{"type":"route","tier":"frontier","escalated":true}',
            '{"type":"bg_agent","event":"running","id":"bg_1"}',
            '{"type":"memory_saved","memories":[{"name":"x"}]}',
            '{"type":"token","content":"chatter"}',
            "{not json",
            "",
        ]),
        encoding="utf-8",
    )
    rc, out, err = _run_capture_err(["bridge", "--from-file", str(fixture)])
    assert rc == 0

    # All emitted lines must be valid pre.* events.
    emitted = [json.loads(line) for line in out.strip().splitlines()]
    assert len(emitted) == 3
    assert emitted[0]["event"] == "pre.router.decision"
    assert emitted[1]["event"] == "pre.bg_agents.change"
    assert emitted[2]["event"] == "pre.system.memory_write"

    # Summary tallies the unknown + malformed lines. The CLI filters blank
    # lines before ingest, so the trailing newline in the fixture isn't counted.
    assert "received=5" in err
    assert "forwarded=3" in err
    assert "unmapped=1" in err
    assert "malformed=1" in err


def test_bridge_help_lists_pre_url_flag():
    import contextlib
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        try:
            main(["bridge", "--help"])
        except SystemExit:
            pass
    assert "--pre-url" in out.getvalue()
    assert "--from-file" in out.getvalue()


def test_viewer_help_lists_port_flag():
    import contextlib
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        try:
            main(["viewer", "--help"])
        except SystemExit:
            pass
    text = out.getvalue()
    assert "--port" in text
    assert "--no-open" in text
