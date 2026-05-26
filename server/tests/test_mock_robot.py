from __future__ import annotations

from pre_buddy.events import (
    Character,
    ConfidenceWarningData,
    Event,
    EventKind,
    ProximityData,
    RouterDecisionData,
    Tier,
    WakeWordData,
)
from pre_buddy.mock_robot import simulate_event


def test_wake_word_turns_toward_mic_and_sprout_uses_yellow():
    ev = Event(EventKind.WAKE_WORD, WakeWordData(source_mic="left"))
    r = simulate_event(ev, Character.SPROUT)
    assert r.has_motion
    assert r.head_x_deg < 0
    assert r.led == "yellow"


def test_router_escalation_nods_but_downshift_is_led_only():
    up = Event(
        EventKind.ROUTER_DECISION,
        RouterDecisionData(from_tier=Tier.FAST, to_tier=Tier.FRONTIER, reason="complexity"),
    )
    up_r = simulate_event(up, Character.SENTINEL)
    assert up_r.has_motion
    assert up_r.led == "purple"

    down = Event(
        EventKind.ROUTER_DECISION,
        RouterDecisionData(from_tier=Tier.FRONTIER, to_tier=Tier.STANDARD, reason="stable"),
    )
    down_r = simulate_event(down, Character.SENTINEL)
    assert not down_r.has_motion
    assert down_r.led == "blue"


def test_confidence_warning_and_proximity_have_distinct_postures():
    warn = Event(
        EventKind.CONFIDENCE_WARNING,
        ConfidenceWarningData(domain="network", confidence=0.4, threshold=0.6),
    )
    w = simulate_event(warn, Character.SAGE)
    assert w.has_motion
    assert w.led == "amber"
    assert w.head_y_deg < 45

    prox = Event(EventKind.PROXIMITY, ProximityData(distance_cm=30.0))
    p = simulate_event(prox, Character.SAGE)
    assert p.has_motion
    assert p.head_y_deg > 45


def test_severity_profile_changes_behavior():
    warn = Event(
        EventKind.CONFIDENCE_WARNING,
        ConfidenceWarningData(domain="network", confidence=0.4, threshold=0.6),
    )
    quiet = simulate_event(warn, Character.SAGE, severity="quiet")
    normal = simulate_event(warn, Character.SAGE, severity="normal")
    loud = simulate_event(warn, Character.SAGE, severity="loud")

    assert not quiet.has_motion
    assert normal.has_motion
    assert loud.has_motion
    assert loud.duration_ms > normal.duration_ms
