"""Bridge PRE's WebSocket event stream into ``pre.*`` protocol events.

PRE (the local agent at ``ws://localhost:7749``) broadcasts a rich set of
internal events over WebSocket — background agent lifecycle, router
decisions, memory writes, etc. This module maps the relevant subset onto
the wire protocol defined in ``shared/protocol/events.md`` and enqueues
the translated events into an :class:`EventPump`.

The pure mapping (``map_message``) has no I/O or third-party deps and is
unit-tested directly. The async driver (``PreBridge.run``) lazily imports
``websockets``; install it via the ``bridge`` extra when you want to run
against a live PRE.
"""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from typing import Any, Callable, Iterable, Optional

from .events import (
    BgAgentChangeData,
    ConfidenceWarningData,
    ErrorData,
    Event,
    EventKind,
    MemoryWriteData,
    RouterDecisionData,
    Tier,
)
from .pump import EventPump


# PRE bg_agent event names → pre.bg_agents.change states. PRE has a few extra
# lifecycle phases the v1 robot protocol doesn't model (progress / paused);
# we collapse those to None so they're dropped quietly.
_BG_STATE_MAP: dict[str, str] = {
    "queued": "started",
    "running": "running",
    "completed": "finished",
    "failed": "failed",
    "cancelled": "failed",
}


def _map_bg_agent(msg: dict[str, Any]) -> list[Event]:
    state = _BG_STATE_MAP.get(str(msg.get("event") or ""))
    if not state:
        return []
    agent_id = str(msg.get("id") or msg.get("agent_id") or "")
    if not agent_id:
        return []
    # PRE doesn't tag bg agents with a tier today. Use STANDARD as the safe
    # default LED color until a tier hint is added upstream.
    tier_raw = msg.get("tier")
    try:
        tier = Tier(tier_raw) if tier_raw else Tier.STANDARD
    except ValueError:
        tier = Tier.STANDARD
    return [
        Event(
            EventKind.BG_AGENT_CHANGE,
            BgAgentChangeData(agent_id=agent_id, state=state, tier=tier),
        )
    ]


def _map_route(msg: dict[str, Any]) -> list[Event]:
    tier_raw = msg.get("tier")
    if tier_raw not in {"fast", "standard", "frontier"}:
        return []
    to_tier = Tier(tier_raw)
    # PRE's route message doesn't carry a `from_tier`; the robot only uses it
    # to detect escalation magnitude. Default to STANDARD and let the firmware
    # decide whether to nod based on to_tier + escalated.
    from_tier = Tier.STANDARD if to_tier is not Tier.STANDARD else Tier.FAST
    reason = "escalated" if msg.get("escalated") else str(msg.get("reason") or "auto")
    return [
        Event(
            EventKind.ROUTER_DECISION,
            RouterDecisionData(from_tier=from_tier, to_tier=to_tier, reason=reason),
        )
    ]


def _map_memory_saved(msg: dict[str, Any]) -> list[Event]:
    memories = msg.get("memories") or []
    events: list[Event] = []
    for mem in memories:
        if not isinstance(mem, dict):
            continue
        name = str(mem.get("name") or "").strip()
        if not name:
            continue
        events.append(
            Event(
                EventKind.MEMORY_WRITE,
                MemoryWriteData(key=name, source=str(mem.get("type") or "auto")),
            )
        )
    return events


def _map_error(msg: dict[str, Any]) -> list[Event]:
    message = str(msg.get("message") or "").strip()
    if not message:
        return []
    code = str(msg.get("code") or "pre.error")
    return [Event(EventKind.ERROR, ErrorData(code=code, message=message))]


# Optional: PRE doesn't broadcast calibration over WS today, but if a future
# `calibration_warning` push lands we'll know what to do with it.
def _map_calibration(msg: dict[str, Any]) -> list[Event]:
    domain = str(msg.get("domain") or "").strip()
    confidence = msg.get("confidence")
    if not domain or not isinstance(confidence, (int, float)):
        return []
    threshold = msg.get("threshold", 0.6)
    try:
        return [
            Event(
                EventKind.CONFIDENCE_WARNING,
                ConfidenceWarningData(
                    domain=domain,
                    confidence=float(confidence),
                    threshold=float(threshold),
                ),
            )
        ]
    except ValueError:
        return []


# Map of PRE WS `type` → handler. Unlisted types are dropped silently; that
# is intentional and forward-compatible with PRE adding new event kinds.
HANDLERS: dict[str, Callable[[dict[str, Any]], list[Event]]] = {
    "bg_agent": _map_bg_agent,
    "route": _map_route,
    "memory_saved": _map_memory_saved,
    "error": _map_error,
    "calibration_warning": _map_calibration,
}


def map_message(msg: Any) -> list[Event]:
    """Translate a single PRE WS message into zero or more ``pre.*`` events.

    Unknown ``type`` values and malformed payloads yield an empty list. This
    keeps the bridge forward-compatible with PRE's evolving event surface.
    """
    if not isinstance(msg, dict):
        return []
    kind = msg.get("type")
    if not isinstance(kind, str):
        return []
    handler = HANDLERS.get(kind)
    if handler is None:
        return []
    try:
        return handler(msg)
    except (TypeError, ValueError):
        return []


def map_line(line: str | bytes) -> list[Event]:
    """Parse a single JSON line from PRE's WS and map it. Tolerates junk."""
    try:
        raw = json.loads(line)
    except (json.JSONDecodeError, TypeError):
        return []
    return map_message(raw)


@dataclass
class BridgeStats:
    received: int = 0
    forwarded: int = 0
    unmapped: int = 0
    malformed: int = 0


@dataclass
class PreBridge:
    """Async driver that connects to PRE's WS and feeds an :class:`EventPump`.

    The actual ``websockets`` import is deferred so this module can be
    imported in environments that don't have the optional dep installed.
    """

    pump: EventPump
    ws_url: str = "ws://localhost:7749"
    stats: BridgeStats = field(default_factory=BridgeStats)

    def ingest(self, raw_lines: Iterable[str | bytes]) -> None:
        """Synchronously feed a batch of JSON lines through the mapper.

        Used by tests and the offline ``--from-file`` CLI mode.
        """
        for line in raw_lines:
            self.stats.received += 1
            events = map_line(line)
            if not events:
                # Distinguish parse-failure vs. known-but-unmapped using a
                # second cheap parse — fine, this path is the offline tool.
                try:
                    parsed = json.loads(line)
                    if isinstance(parsed, dict) and parsed.get("type"):
                        self.stats.unmapped += 1
                    else:
                        self.stats.malformed += 1
                except (json.JSONDecodeError, TypeError):
                    self.stats.malformed += 1
                continue
            for ev in events:
                self.pump.enqueue(ev)
                self.stats.forwarded += 1

    async def run(self, *, max_messages: Optional[int] = None) -> BridgeStats:
        """Connect to ``ws_url`` and feed mapped events into the pump.

        Returns the accumulated :class:`BridgeStats` when the loop exits
        (either because the remote closed, ``max_messages`` was reached, or
        the caller cancelled the task).
        """
        try:
            import websockets  # type: ignore[import-not-found]
        except ImportError as exc:  # pragma: no cover - exercised manually
            raise RuntimeError(
                "PreBridge.run requires the 'websockets' package. "
                "Install with: pip install 'pre-buddy[bridge]'"
            ) from exc

        async with websockets.connect(self.ws_url) as ws:
            async for raw in ws:
                self.stats.received += 1
                events = map_line(raw)
                if not events:
                    self.stats.unmapped += 1
                else:
                    for ev in events:
                        self.pump.enqueue(ev)
                        self.stats.forwarded += 1
                if max_messages is not None and self.stats.received >= max_messages:
                    break

        return self.stats
