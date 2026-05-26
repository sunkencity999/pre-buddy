"""Event pump for PRE Buddy server.

Keeps outbound events queued and emits them in transport-ready JSON-lines.
"""

from __future__ import annotations

from collections import deque
from dataclasses import dataclass, field
from typing import Deque, Iterable, Iterator

from .events import (
    BgAgentChangeData,
    ConfidenceWarningData,
    Event,
    EventKind,
    ProximityData,
    RouterDecisionData,
    Tier,
    WakeWordData,
)
from .serializer import dumps


@dataclass
class EventPump:
    _queue: Deque[Event] = field(default_factory=deque)

    def enqueue(self, event: Event) -> None:
        self._queue.append(event)

    def enqueue_many(self, events: Iterable[Event]) -> None:
        for event in events:
            self.enqueue(event)

    def pop_next(self) -> Event | None:
        if not self._queue:
            return None
        return self._queue.popleft()

    def __len__(self) -> int:
        return len(self._queue)

    def iter_lines(self) -> Iterator[str]:
        while True:
            event = self.pop_next()
            if event is None:
                break
            yield dumps(event)


def demo_events() -> list[Event]:
    """Small deterministic sequence for local bring-up and CLI demo mode."""
    return [
        Event(EventKind.WAKE_WORD, WakeWordData(source_mic="left")),
        Event(
            EventKind.BG_AGENT_CHANGE,
            BgAgentChangeData(agent_id="bootstrap", state="running", tier=Tier.FAST),
        ),
        Event(
            EventKind.ROUTER_DECISION,
            RouterDecisionData(from_tier=Tier.FAST, to_tier=Tier.FRONTIER, reason="complexity"),
        ),
        Event(
            EventKind.CONFIDENCE_WARNING,
            ConfidenceWarningData(domain="network", confidence=0.41, threshold=0.6),
        ),
        Event(EventKind.PROXIMITY, ProximityData(distance_cm=32.0)),
    ]
