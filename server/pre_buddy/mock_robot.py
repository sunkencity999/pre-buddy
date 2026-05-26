"""Software robot simulator for pre-hardware behavior review.

This mirrors the v1 embodiment mapping so we can test likely robot reactions
against JSON-lines playback scenarios before device bring-up.
"""

from __future__ import annotations

from dataclasses import dataclass

from .events import Character, Event, EventKind, Expression, Tier


@dataclass(frozen=True)
class SimResponse:
    event: str
    led: str
    has_motion: bool
    head_x_deg: float
    head_y_deg: float
    duration_ms: int
    note: str
    expression: str = Expression.NEUTRAL.value


def _idle_led(character: Character) -> str:
    if character is Character.SAGE:
        return "blue"
    if character is Character.SPROUT:
        return "green"
    return "white"


def _reaction_ms(character: Character) -> int:
    if character is Character.SAGE:
        return 2000
    if character is Character.SPROUT:
        return 350
    return 500


def _tier_rank(tier: Tier) -> int:
    if tier is Tier.FAST:
        return 0
    if tier is Tier.STANDARD:
        return 1
    return 2


def _tier_led(tier: Tier) -> str:
    if tier is Tier.FAST:
        return "green"
    if tier is Tier.STANDARD:
        return "blue"
    return "purple"


def _expression_for(event: Event) -> str:
    """Mirror the firmware's map_event() → Expression mapping (protocol.h)."""
    kind = event.kind
    if kind is EventKind.WAKE_WORD:
        return Expression.SURPRISED.value
    if kind is EventKind.BG_AGENT_CHANGE:
        return Expression.THINKING.value
    if kind is EventKind.ROUTER_DECISION:
        escalating = _tier_rank(event.data.to_tier) > _tier_rank(event.data.from_tier)
        return Expression.CURIOUS.value if escalating else Expression.THINKING.value
    if kind is EventKind.CONFIDENCE_WARNING:
        return Expression.CONCERNED.value
    if kind is EventKind.CONFIDENCE_SNAPSHOT:
        confidence = getattr(event.data, "confidence", 1.0)
        return Expression.CONCERNED.value if confidence < 0.7 else Expression.NEUTRAL.value
    if kind is EventKind.KG_DELTA:
        return Expression.THINKING.value
    if kind is EventKind.TRAINING_PROGRESS:
        return Expression.THINKING.value
    if kind is EventKind.SCHEDULER_UPCOMING:
        near = event.data.minutes_until <= 120
        return Expression.SURPRISED.value if near else Expression.NEUTRAL.value
    if kind is EventKind.TOOLS_ROLLUP:
        return Expression.NEUTRAL.value
    if kind is EventKind.MEMORY_WRITE:
        return Expression.HAPPY.value
    if kind is EventKind.PROXIMITY:
        return Expression.CURIOUS.value
    if kind is EventKind.ERROR:
        return Expression.ERROR.value
    if kind is EventKind.CHARACTER_SET:
        return Expression.HAPPY.value
    return Expression.NEUTRAL.value


def simulate_event(event: Event, character: Character, *, severity: str = "normal") -> SimResponse:
    """Return likely robot behavior for one event and character profile.

    severity: quiet|normal|loud
    - quiet reduces non-critical motion to keep the device calm
    - loud increases expressiveness for alert-heavy contexts
    """
    led = _idle_led(character)
    has_motion = False
    x = 0.0
    y = 45.0
    duration = _reaction_ms(character)
    note = "idle"
    expression = _expression_for(event)

    if severity not in {"quiet", "normal", "loud"}:
        raise ValueError(f"invalid severity: {severity}")

    if event.kind is EventKind.WAKE_WORD:
        has_motion = True
        mic = getattr(event.data, "source_mic", "unknown")
        if mic == "left":
            x = -35.0
        elif mic == "right":
            x = 35.0
        if severity == "loud":
            x = x * 1.2
        if character is Character.SPROUT:
            led = "yellow"
        note = "turn_toward_mic"

    elif event.kind is EventKind.BG_AGENT_CHANGE:
        led = _tier_led(event.data.tier)
        note = "agent_tier_pulse"

    elif event.kind is EventKind.ROUTER_DECISION:
        led = _tier_led(event.data.to_tier)
        if _tier_rank(event.data.to_tier) > _tier_rank(event.data.from_tier):
            has_motion = True
            y = 38.0
            duration += 200
            if severity == "loud":
                y = 34.0
            note = "router_escalation_nod"
        else:
            note = "router_led_only"

    elif event.kind is EventKind.CONFIDENCE_WARNING:
        has_motion = True
        y = 30.0
        led = "amber"
        if severity == "quiet":
            has_motion = False
            y = 45.0
        note = "confidence_tilt"

    elif event.kind is EventKind.CONFIDENCE_SNAPSHOT:
        confidence = getattr(event.data, "confidence", 1.0)
        led = "amber" if confidence < 0.7 else _idle_led(character)
        note = "confidence_snapshot"

    elif event.kind is EventKind.KG_DELTA:
        led = "cyan"
        note = "kg_update"

    elif event.kind is EventKind.TRAINING_PROGRESS:
        led = "white"
        note = "training_progress"

    elif event.kind is EventKind.SCHEDULER_UPCOMING:
        led = "amber" if event.data.minutes_until <= 120 else "blue"
        if severity == "quiet" and event.data.minutes_until > 30:
            led = _idle_led(character)
        note = "schedule_reminder"

    elif event.kind is EventKind.TOOLS_ROLLUP:
        led = "white"
        note = "tools_rollup"

    elif event.kind is EventKind.MEMORY_WRITE:
        has_motion = True
        y = 35.0
        duration += 300
        if severity == "quiet":
            has_motion = False
            y = 45.0
        led = "white"
        note = "memory_nod"

    elif event.kind is EventKind.PROXIMITY:
        has_motion = True
        y = 60.0
        duration = 700
        if severity == "loud":
            duration = 550
        note = "look_up"

    elif event.kind is EventKind.ERROR:
        led = "red"
        note = "error_still"

    elif event.kind is EventKind.CHARACTER_SET:
        has_motion = True
        y = 35.0
        led = _idle_led(event.data.character)
        note = "character_ack"

    # Global expressiveness scaling.
    if severity == "quiet" and has_motion:
        duration = int(duration * 0.9)
    elif severity == "loud" and has_motion:
        duration = int(duration * 1.15)

    return SimResponse(
        event=event.kind.value,
        led=led,
        has_motion=has_motion,
        head_x_deg=x,
        head_y_deg=y,
        duration_ms=duration,
        note=note,
        expression=expression,
    )
