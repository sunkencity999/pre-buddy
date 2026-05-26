"""Tests for the typed pre.* event model."""

from __future__ import annotations

import json

import pytest

from pre_buddy.events import (
    BgAgentChangeData,
    Character,
    CharacterSetData,
    ConfidenceSnapshotData,
    ConfidenceWarningData,
    ErrorData,
    Event,
    EventKind,
    KgDeltaData,
    MemoryWriteData,
    ProximityData,
    RouterDecisionData,
    SchedulerUpcomingData,
    Tier,
    ToolsRollupData,
    TrainingProgressData,
    WakeWordData,
)


def test_event_kind_values_match_protocol_spec_core_set():
    assert EventKind.WAKE_WORD.value == "pre.system.wake_word"
    assert EventKind.BG_AGENT_CHANGE.value == "pre.bg_agents.change"
    assert EventKind.ROUTER_DECISION.value == "pre.router.decision"
    assert EventKind.CONFIDENCE_WARNING.value == "pre.confidence.warning"
    assert EventKind.CONFIDENCE_SNAPSHOT.value == "pre.confidence.snapshot"
    assert EventKind.KG_DELTA.value == "pre.kg.delta"
    assert EventKind.TRAINING_PROGRESS.value == "pre.training.progress"
    assert EventKind.SCHEDULER_UPCOMING.value == "pre.scheduler.upcoming"
    assert EventKind.TOOLS_ROLLUP.value == "pre.tools.rollup"
    assert EventKind.MEMORY_WRITE.value == "pre.system.memory_write"
    assert EventKind.PROXIMITY.value == "pre.system.proximity"
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
    assert ok.tier is Tier.FRONTIER
    with pytest.raises(ValueError):
        BgAgentChangeData(agent_id="a1", state="bogus", tier="fast")
    with pytest.raises(ValueError):
        BgAgentChangeData(agent_id="a1", state="running", tier="ultra")


def test_router_decision_validates_tiers():
    d = RouterDecisionData(from_tier="fast", to_tier="frontier")
    assert d.from_tier is Tier.FAST
    assert d.to_tier is Tier.FRONTIER
    with pytest.raises(ValueError):
        RouterDecisionData(from_tier="fast", to_tier="godmode")


def test_confidence_payloads_validate_range():
    ConfidenceWarningData(domain="code", confidence=0.5, threshold=0.6)
    ConfidenceSnapshotData(weakest_domain="network", confidence=0.4)
    with pytest.raises(ValueError):
        ConfidenceWarningData(domain="code", confidence=2.0)
    with pytest.raises(ValueError):
        ConfidenceSnapshotData(weakest_domain="network", confidence=-0.1)


def test_kg_and_training_and_scheduler_validation():
    KgDeltaData(entities_added=1, relations_added=2)
    TrainingProgressData(examples_total=10, goal_examples=100)
    SchedulerUpcomingData(event_name="meeting", minutes_until=45)
    with pytest.raises(ValueError):
        KgDeltaData(entities_added=-1, relations_added=0)
    with pytest.raises(ValueError):
        TrainingProgressData(examples_total=1, goal_examples=-2)
    with pytest.raises(ValueError):
        SchedulerUpcomingData(event_name="meeting", minutes_until=-1)


def test_tools_and_proximity_validation():
    ToolsRollupData(tool="web_search", calls=5, success_rate=0.8)
    ProximityData(distance_cm=35.5)
    with pytest.raises(ValueError):
        ToolsRollupData(tool="exec", calls=-1, success_rate=0.8)
    with pytest.raises(ValueError):
        ToolsRollupData(tool="exec", calls=1, success_rate=1.5)
    with pytest.raises(ValueError):
        ProximityData(distance_cm=-0.1)


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


def test_event_to_dict_unwraps_enums():
    ev = Event(EventKind.CHARACTER_SET, CharacterSetData(character=Character.SPROUT))
    d = ev.to_dict()
    assert d["data"] == {"character": "sprout"}

    ev2 = Event(
        EventKind.ROUTER_DECISION,
        RouterDecisionData(from_tier=Tier.FAST, to_tier=Tier.FRONTIER),
    )
    d2 = ev2.to_dict()
    assert d2["data"] == {"from_tier": "fast", "to_tier": "frontier", "reason": ""}


def test_event_from_dict_hydrates_typed_payloads():
    raw = {
        "event": "pre.bg_agents.change",
        "data": {"agent_id": "x", "state": "started", "tier": "fast"},
    }
    ev = Event.from_dict(raw)
    assert ev.kind is EventKind.BG_AGENT_CHANGE
    assert isinstance(ev.data, BgAgentChangeData)
    assert ev.data.agent_id == "x"

    raw2 = {
        "event": "pre.system.memory_write",
        "data": {"key": "note:123", "source": "voice"},
    }
    ev2 = Event.from_dict(raw2)
    assert isinstance(ev2.data, MemoryWriteData)


def test_event_from_dict_unknown_event_raises():
    with pytest.raises(ValueError):
        Event.from_dict({"event": "pre.does_not_exist", "data": {}})


def test_round_trip_json_preserves_event():
    original = Event(EventKind.WAKE_WORD, WakeWordData(source_mic="left"), ts=42.0)
    line = json.dumps(original.to_dict(), sort_keys=True)
    restored = Event.from_dict(json.loads(line))
    assert restored.kind is original.kind
    assert restored.data == original.data
    assert restored.ts == 42.0
