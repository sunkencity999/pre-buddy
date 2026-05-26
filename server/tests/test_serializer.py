"""Tests for the JSON-lines serializer."""

from __future__ import annotations

import json

from pre_buddy.events import (
    BgAgentChangeData,
    ConfidenceWarningData,
    Event,
    EventKind,
    WakeWordData,
)
from pre_buddy.serializer import dump_many, dumps, load_many, loads


def test_dumps_produces_single_compact_line():
    ev = Event(EventKind.WAKE_WORD, WakeWordData(source_mic="right"))
    line = dumps(ev)
    assert "\n" not in line
    decoded = json.loads(line)
    assert decoded["event"] == "pre.system.wake_word"
    assert decoded["data"] == {"source_mic": "right"}


def test_loads_round_trip():
    ev = Event(
        EventKind.CONFIDENCE_WARNING,
        ConfidenceWarningData(domain="code", confidence=0.42, threshold=0.7),
    )
    restored = loads(dumps(ev))
    assert restored.kind is EventKind.CONFIDENCE_WARNING
    assert restored.data.domain == "code"
    assert restored.data.confidence == 0.42
    assert restored.data.threshold == 0.7


def test_dump_many_and_load_many_skip_blank_lines():
    events = [
        Event(EventKind.WAKE_WORD, WakeWordData("left")),
        Event(
            EventKind.BG_AGENT_CHANGE,
            BgAgentChangeData(agent_id="a", state="running", tier="standard"),
        ),
    ]
    blob = dump_many(events) + "\n   \n"
    restored = list(load_many(blob))
    assert len(restored) == 2
    assert restored[0].kind is EventKind.WAKE_WORD
    assert restored[1].kind is EventKind.BG_AGENT_CHANGE
    assert restored[1].data.tier == "standard"


def test_dumps_is_sorted_for_stable_diffs():
    # sort_keys=True is part of the contract — useful for diffing logs.
    ev = Event(
        EventKind.BG_AGENT_CHANGE,
        BgAgentChangeData(agent_id="z", state="finished", tier="frontier"),
        ts=1.0,
    )
    line = dumps(ev)
    # Top-level keys appear in alphabetical order.
    assert line.index('"data"') < line.index('"event"') < line.index('"ts"')
