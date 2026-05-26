"""Tests for the typed pre.* event model."""

from __future__ import annotations

import json

import pytest

from pre_buddy.events import (
    BgAgentChangeData,
    Character,
    CharacterSetData,
    ConfidenceWarningData,
    ErrorData,
    Event,
    EventKind,
    WakeWordData,
)


def test_event_kind_values_match_protocol_spec():
    # Keep these in lockstep with shared/protocol/events.md.
    assert EventKind.WAKE_WORD.value == "pre.system.wake_word"
    assert EventKind.BG_AGENT_CHANGE.value == "pre.bg_agents.change"
    assert EventKind.CONFIDENCE_WARNING.value == "pre.confidence.warning"
    assert EventKind.ERROR.value == "pre.system.error"
    assert EventKind.CHARACTER_SET.value == "pre.character.set"


def test_wake_word_payload_validates_mic():
    WakeWordData("left")
    WakeWordData("right")
    WakeWordData("unknown")
    with pytest.raises(ValueError):
        WakeWordData("nose")


def test_bg_agent_change_validates_state_and_tier():
    ok = BgAgentChangeData(agent_id="a1", state="running", tier="frontier")
    assert ok.agent_id == "a1"
    with pytest.raises(ValueError):
        BgAgentChangeData(agent_id="a1", state="bogus", tier="fast")
    with pytest.raises(ValueError):
        BgAgentChangeData(agent_id="a1", state="running", tier="ultra")


def test_confidence_warning_validates_range():
    ConfidenceWarningData(domain="code", confidence=0.5, threshold=0.6)
    with pytest.raises(ValueError):
        ConfidenceWarningData(domain="code", confidence=2.0)
    with pytest.raises(ValueError):
        ConfidenceWarningData(domain="code", confidence=-0.1)


def test_character_set_accepts_string_or_enum():
    a = CharacterSetData(character="sage")
    b = CharacterSetData(character=Character.SAGE)
    assert a.character is Character.SAGE
    assert b.character is Character.SAGE
    with pytest.raises(ValueError):
        CharacterSetData(character="dragon")


def test_event_to_dict_includes_optional_timestamp():
    ev = Event(EventKind.ERROR, ErrorData(code="E1", message="boom"), ts=1234.5)
    d = ev.to_dict()
    assert d["event"] == "pre.system.error"
    assert d["data"] == {"code": "E1", "message": "boom"}
    assert d["ts"] == 1234.5

    ev2 = Event(EventKind.ERROR, ErrorData(code="E1"))
    assert "ts" not in ev2.to_dict()


def test_event_to_dict_unwraps_character_enum():
    ev = Event(EventKind.CHARACTER_SET, CharacterSetData(character=Character.SPROUT))
    d = ev.to_dict()
    assert d["data"] == {"character": "sprout"}


def test_event_from_dict_hydrates_typed_payload():
    raw = {
        "event": "pre.bg_agents.change",
        "data": {"agent_id": "x", "state": "started", "tier": "fast"},
    }
    ev = Event.from_dict(raw)
    assert ev.kind is EventKind.BG_AGENT_CHANGE
    assert isinstance(ev.data, BgAgentChangeData)
    assert ev.data.agent_id == "x"


def test_event_from_dict_unknown_event_raises():
    with pytest.raises(ValueError):
        Event.from_dict({"event": "pre.does_not_exist", "data": {}})


def test_round_trip_json_preserves_event():
    original = Event(
        EventKind.WAKE_WORD, WakeWordData(source_mic="left"), ts=42.0
    )
    line = json.dumps(original.to_dict(), sort_keys=True)
    restored = Event.from_dict(json.loads(line))
    assert restored.kind is original.kind
    assert restored.data == original.data
    assert restored.ts == 42.0
