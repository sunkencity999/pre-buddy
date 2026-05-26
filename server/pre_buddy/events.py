"""Typed event model for the ``pre.*`` wire protocol.

The Python side and the C++ firmware core share the same canonical names
defined in ``shared/protocol/events.md``. Keep both in lockstep.
"""

from __future__ import annotations

from dataclasses import dataclass, field, asdict
from enum import Enum
from typing import Any, ClassVar


class Character(str, Enum):
    SAGE = "sage"
    SPROUT = "sprout"
    SENTINEL = "sentinel"


class EventKind(str, Enum):
    WAKE_WORD = "pre.system.wake_word"
    BG_AGENT_CHANGE = "pre.bg_agents.change"
    CONFIDENCE_WARNING = "pre.confidence.warning"
    ERROR = "pre.system.error"
    CHARACTER_SET = "pre.character.set"


# --- payload dataclasses ----------------------------------------------------

@dataclass
class WakeWordData:
    source_mic: str = "unknown"  # "left" | "right" | "unknown"

    def __post_init__(self) -> None:
        if self.source_mic not in {"left", "right", "unknown"}:
            raise ValueError(f"invalid source_mic: {self.source_mic!r}")


@dataclass
class BgAgentChangeData:
    agent_id: str
    state: str   # "started" | "running" | "finished" | "failed"
    tier: str    # "fast" | "standard" | "frontier"

    _VALID_STATES: ClassVar[frozenset[str]] = frozenset(
        {"started", "running", "finished", "failed"}
    )
    _VALID_TIERS: ClassVar[frozenset[str]] = frozenset(
        {"fast", "standard", "frontier"}
    )

    def __post_init__(self) -> None:
        if self.state not in self._VALID_STATES:
            raise ValueError(f"invalid state: {self.state!r}")
        if self.tier not in self._VALID_TIERS:
            raise ValueError(f"invalid tier: {self.tier!r}")


@dataclass
class ConfidenceWarningData:
    domain: str
    confidence: float
    threshold: float = 0.6

    def __post_init__(self) -> None:
        if not 0.0 <= self.confidence <= 1.0:
            raise ValueError("confidence must be in [0,1]")
        if not 0.0 <= self.threshold <= 1.0:
            raise ValueError("threshold must be in [0,1]")


@dataclass
class ErrorData:
    code: str
    message: str = ""


@dataclass
class CharacterSetData:
    character: Character

    def __post_init__(self) -> None:
        # Allow plain strings for convenience.
        if isinstance(self.character, str):
            self.character = Character(self.character)


# --- envelope ---------------------------------------------------------------

@dataclass
class Event:
    """Envelope around a typed payload, serialized one-per-line."""

    kind: EventKind
    data: Any = field(default_factory=dict)
    ts: float | None = None

    def to_dict(self) -> dict[str, Any]:
        payload: Any
        if hasattr(self.data, "__dataclass_fields__"):
            payload = asdict(self.data)
            # Unwrap Enums (Character) to their .value
            for k, v in list(payload.items()):
                if isinstance(v, Enum):
                    payload[k] = v.value
        elif isinstance(self.data, dict):
            payload = dict(self.data)
        else:
            raise TypeError(
                f"Event.data must be a dataclass payload or dict, got {type(self.data)!r}"
            )

        out: dict[str, Any] = {"event": self.kind.value, "data": payload}
        if self.ts is not None:
            out["ts"] = float(self.ts)
        return out

    @classmethod
    def from_dict(cls, raw: dict[str, Any]) -> "Event":
        if "event" not in raw:
            raise ValueError("event field missing")
        kind = EventKind(raw["event"])
        data = raw.get("data", {})
        ts = raw.get("ts")

        typed = _hydrate(kind, data)
        return cls(kind=kind, data=typed, ts=ts)


_HYDRATORS: dict[EventKind, type] = {
    EventKind.WAKE_WORD: WakeWordData,
    EventKind.BG_AGENT_CHANGE: BgAgentChangeData,
    EventKind.CONFIDENCE_WARNING: ConfidenceWarningData,
    EventKind.ERROR: ErrorData,
    EventKind.CHARACTER_SET: CharacterSetData,
}


def _hydrate(kind: EventKind, data: dict[str, Any]) -> Any:
    cls = _HYDRATORS.get(kind)
    if cls is None:
        return dict(data)
    return cls(**data)
