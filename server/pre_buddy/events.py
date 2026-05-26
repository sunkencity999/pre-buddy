"""Typed event model for the ``pre.*`` wire protocol.

The Python side and the C++ firmware core share canonical names defined in
``shared/protocol/events.md``. Keep both in lockstep.
"""

from __future__ import annotations

from dataclasses import dataclass, field, asdict
from enum import Enum
from typing import Any, ClassVar


class Character(str, Enum):
    SAGE = "sage"
    SPROUT = "sprout"
    SENTINEL = "sentinel"


class Tier(str, Enum):
    FAST = "fast"
    STANDARD = "standard"
    FRONTIER = "frontier"


class EventKind(str, Enum):
    WAKE_WORD = "pre.system.wake_word"
    BG_AGENT_CHANGE = "pre.bg_agents.change"
    ROUTER_DECISION = "pre.router.decision"
    CONFIDENCE_WARNING = "pre.confidence.warning"
    CONFIDENCE_SNAPSHOT = "pre.confidence.snapshot"
    KG_DELTA = "pre.kg.delta"
    TRAINING_PROGRESS = "pre.training.progress"
    SCHEDULER_UPCOMING = "pre.scheduler.upcoming"
    TOOLS_ROLLUP = "pre.tools.rollup"
    MEMORY_WRITE = "pre.system.memory_write"
    PROXIMITY = "pre.system.proximity"
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
    tier: str | Tier   # "fast" | "standard" | "frontier"

    _VALID_STATES: ClassVar[frozenset[str]] = frozenset(
        {"started", "running", "finished", "failed"}
    )

    def __post_init__(self) -> None:
        if self.state not in self._VALID_STATES:
            raise ValueError(f"invalid state: {self.state!r}")
        if isinstance(self.tier, str):
            self.tier = Tier(self.tier)


@dataclass
class RouterDecisionData:
    from_tier: str | Tier
    to_tier: str | Tier
    reason: str = ""

    def __post_init__(self) -> None:
        if isinstance(self.from_tier, str):
            self.from_tier = Tier(self.from_tier)
        if isinstance(self.to_tier, str):
            self.to_tier = Tier(self.to_tier)


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
class ConfidenceSnapshotData:
    weakest_domain: str
    confidence: float

    def __post_init__(self) -> None:
        if not 0.0 <= self.confidence <= 1.0:
            raise ValueError("confidence must be in [0,1]")


@dataclass
class KgDeltaData:
    entities_added: int = 0
    relations_added: int = 0

    def __post_init__(self) -> None:
        if self.entities_added < 0 or self.relations_added < 0:
            raise ValueError("entity/relation deltas must be >= 0")


@dataclass
class TrainingProgressData:
    examples_total: int
    goal_examples: int

    def __post_init__(self) -> None:
        if self.examples_total < 0 or self.goal_examples < 0:
            raise ValueError("example counts must be >= 0")


@dataclass
class SchedulerUpcomingData:
    event_name: str
    minutes_until: int

    def __post_init__(self) -> None:
        if self.minutes_until < 0:
            raise ValueError("minutes_until must be >= 0")


@dataclass
class ToolsRollupData:
    tool: str
    calls: int
    success_rate: float

    def __post_init__(self) -> None:
        if self.calls < 0:
            raise ValueError("calls must be >= 0")
        if not 0.0 <= self.success_rate <= 1.0:
            raise ValueError("success_rate must be in [0,1]")


@dataclass
class MemoryWriteData:
    key: str
    source: str = "unknown"


@dataclass
class ProximityData:
    distance_cm: float

    def __post_init__(self) -> None:
        if self.distance_cm < 0.0:
            raise ValueError("distance_cm must be >= 0")


@dataclass
class ErrorData:
    code: str
    message: str = ""


@dataclass
class CharacterSetData:
    character: Character

    def __post_init__(self) -> None:
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
    EventKind.ROUTER_DECISION: RouterDecisionData,
    EventKind.CONFIDENCE_WARNING: ConfidenceWarningData,
    EventKind.CONFIDENCE_SNAPSHOT: ConfidenceSnapshotData,
    EventKind.KG_DELTA: KgDeltaData,
    EventKind.TRAINING_PROGRESS: TrainingProgressData,
    EventKind.SCHEDULER_UPCOMING: SchedulerUpcomingData,
    EventKind.TOOLS_ROLLUP: ToolsRollupData,
    EventKind.MEMORY_WRITE: MemoryWriteData,
    EventKind.PROXIMITY: ProximityData,
    EventKind.ERROR: ErrorData,
    EventKind.CHARACTER_SET: CharacterSetData,
}


def _hydrate(kind: EventKind, data: dict[str, Any]) -> Any:
    cls = _HYDRATORS.get(kind)
    if cls is None:
        return dict(data)
    return cls(**data)
