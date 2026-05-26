"""Tests for the PRE→pre-buddy WS event bridge."""

from __future__ import annotations

import json

import pytest

from pre_buddy.bridge import (
    HANDLERS,
    PreBridge,
    map_line,
    map_message,
)
from pre_buddy.events import EventKind, Tier
from pre_buddy.pump import EventPump


# --- map_message: bg_agent --------------------------------------------------

@pytest.mark.parametrize(
    "pre_event,expected_state",
    [
        ("queued", "started"),
        ("running", "running"),
        ("completed", "finished"),
        ("failed", "failed"),
        ("cancelled", "failed"),
    ],
)
def test_bg_agent_lifecycle_states_map(pre_event: str, expected_state: str) -> None:
    events = map_message({"type": "bg_agent", "event": pre_event, "id": "bg_7"})
    assert len(events) == 1
    ev = events[0]
    assert ev.kind is EventKind.BG_AGENT_CHANGE
    assert ev.data.agent_id == "bg_7"
    assert ev.data.state == expected_state
    assert ev.data.tier is Tier.STANDARD  # default when PRE doesn't tag tier


def test_bg_agent_drops_unmodelled_phases() -> None:
    # PRE has phases the v1 robot protocol doesn't surface — they should be dropped.
    for phase in ("progress", "paused", "weird-future-phase"):
        assert map_message({"type": "bg_agent", "event": phase, "id": "x"}) == []


def test_bg_agent_drops_when_id_missing() -> None:
    assert map_message({"type": "bg_agent", "event": "running"}) == []


def test_bg_agent_honours_explicit_tier_when_present() -> None:
    events = map_message(
        {"type": "bg_agent", "event": "running", "id": "x", "tier": "frontier"}
    )
    assert events[0].data.tier is Tier.FRONTIER


# --- map_message: route -----------------------------------------------------

def test_route_emits_router_decision() -> None:
    events = map_message({"type": "route", "tier": "frontier", "model": "claude"})
    assert len(events) == 1
    ev = events[0]
    assert ev.kind is EventKind.ROUTER_DECISION
    assert ev.data.to_tier is Tier.FRONTIER
    assert ev.data.from_tier is Tier.STANDARD  # default placeholder


def test_route_marks_escalated_in_reason() -> None:
    events = map_message({"type": "route", "tier": "frontier", "escalated": True})
    assert events[0].data.reason == "escalated"


def test_route_drops_invalid_tier() -> None:
    assert map_message({"type": "route", "tier": "ultra-frontier"}) == []
    assert map_message({"type": "route"}) == []


def test_route_to_standard_uses_fast_as_from() -> None:
    # When the robot routes to standard, fabricate a different `from_tier`
    # so the firmware can still distinguish "stayed standard" from "changed".
    events = map_message({"type": "route", "tier": "standard"})
    assert events[0].data.from_tier is Tier.FAST
    assert events[0].data.to_tier is Tier.STANDARD


# --- map_message: memory_saved ---------------------------------------------

def test_memory_saved_emits_one_per_memory() -> None:
    events = map_message(
        {
            "type": "memory_saved",
            "memories": [
                {"name": "user_role", "type": "user"},
                {"name": "feedback_terse", "type": "feedback"},
            ],
        }
    )
    assert len(events) == 2
    assert all(ev.kind is EventKind.MEMORY_WRITE for ev in events)
    assert [ev.data.key for ev in events] == ["user_role", "feedback_terse"]
    assert [ev.data.source for ev in events] == ["user", "feedback"]


def test_memory_saved_handles_missing_memories_list() -> None:
    assert map_message({"type": "memory_saved"}) == []
    assert map_message({"type": "memory_saved", "memories": []}) == []


def test_memory_saved_skips_entries_without_name() -> None:
    events = map_message(
        {"type": "memory_saved", "memories": [{"type": "user"}, {"name": "ok"}]}
    )
    assert len(events) == 1
    assert events[0].data.key == "ok"


# --- map_message: error ----------------------------------------------------

def test_error_emits_system_error_with_default_code() -> None:
    events = map_message({"type": "error", "message": "tool failed"})
    assert len(events) == 1
    assert events[0].kind is EventKind.ERROR
    assert events[0].data.code == "pre.error"
    assert events[0].data.message == "tool failed"


def test_error_with_empty_message_is_dropped() -> None:
    assert map_message({"type": "error"}) == []
    assert map_message({"type": "error", "message": "   "}) == []


# --- map_message: unknown / malformed --------------------------------------

@pytest.mark.parametrize(
    "msg",
    [
        {"type": "token", "content": "hi"},        # streaming chatter we don't surface
        {"type": "argus_thinking"},                # companion noise
        {"type": "tier_result", "success": True},  # internal UI feedback
        {"type": "image_generated", "path": "/a/b.png"},
    ],
)
def test_unknown_pre_types_drop_silently(msg: dict) -> None:
    assert map_message(msg) == []


def test_non_dict_input_is_safe() -> None:
    assert map_message(None) == []
    assert map_message(["not", "a", "dict"]) == []
    assert map_message("nope") == []


def test_handlers_cover_every_documented_kind() -> None:
    # Guardrail: if someone removes a handler without updating the bridge,
    # this test fails loudly so we don't ship a silent regression.
    assert {"bg_agent", "route", "memory_saved", "error"} <= set(HANDLERS)


# --- map_line ---------------------------------------------------------------

def test_map_line_parses_and_maps() -> None:
    line = json.dumps({"type": "route", "tier": "fast"})
    events = map_line(line)
    assert events[0].kind is EventKind.ROUTER_DECISION


def test_map_line_tolerates_bad_json() -> None:
    assert map_line("{not json") == []
    assert map_line("") == []


# --- PreBridge.ingest (offline driver) -------------------------------------

def test_ingest_pushes_mapped_events_into_pump_and_tallies_stats() -> None:
    pump = EventPump()
    bridge = PreBridge(pump=pump)
    lines = [
        json.dumps({"type": "bg_agent", "event": "running", "id": "bg_1"}),
        json.dumps({"type": "route", "tier": "frontier"}),
        json.dumps({"type": "tier_result"}),    # known-but-unmapped
        "{not json",                              # malformed
        json.dumps(
            {"type": "memory_saved", "memories": [{"name": "x"}, {"name": "y"}]}
        ),
    ]
    bridge.ingest(lines)

    assert bridge.stats.received == 5
    assert bridge.stats.forwarded == 4   # bg + route + two memory writes
    assert bridge.stats.unmapped == 1
    assert bridge.stats.malformed == 1
    assert len(pump) == 4
