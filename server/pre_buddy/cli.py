"""Standalone CLI for PRE Buddy server development.

Production target is `pre buddy serve`; this package keeps an equivalent
standalone tool for rapid local iteration.
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path
from typing import Sequence

from . import __version__
from .events import (
    BgAgentChangeData,
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
from .mock_robot import simulate_event
from .pump import EventPump, demo_events
from .serve import BuddyServer
from .serializer import dumps, load_many
from .transport import MockBleSession


def _cmd_version(_args: argparse.Namespace) -> int:
    print(f"pre-buddy {__version__}")
    return 0


def _build_event_from_args(args: argparse.Namespace, ts: float | None) -> Event:
    kind = EventKind(args.kind)

    if kind is EventKind.WAKE_WORD:
        return Event(kind, WakeWordData(source_mic=args.mic), ts=ts)
    if kind is EventKind.BG_AGENT_CHANGE:
        return Event(
            kind,
            BgAgentChangeData(agent_id=args.agent_id, state=args.state, tier=args.tier),
            ts=ts,
        )
    if kind is EventKind.ROUTER_DECISION:
        return Event(
            kind,
            RouterDecisionData(from_tier=args.from_tier, to_tier=args.to_tier, reason=args.reason),
            ts=ts,
        )
    if kind is EventKind.CONFIDENCE_WARNING:
        return Event(
            kind,
            ConfidenceWarningData(
                domain=args.domain, confidence=args.confidence, threshold=args.threshold
            ),
            ts=ts,
        )
    if kind is EventKind.CONFIDENCE_SNAPSHOT:
        return Event(
            kind,
            ConfidenceSnapshotData(weakest_domain=args.domain, confidence=args.confidence),
            ts=ts,
        )
    if kind is EventKind.KG_DELTA:
        return Event(
            kind,
            KgDeltaData(entities_added=args.entities_added, relations_added=args.relations_added),
            ts=ts,
        )
    if kind is EventKind.TRAINING_PROGRESS:
        return Event(
            kind,
            TrainingProgressData(examples_total=args.examples_total, goal_examples=args.goal_examples),
            ts=ts,
        )
    if kind is EventKind.SCHEDULER_UPCOMING:
        return Event(
            kind,
            SchedulerUpcomingData(event_name=args.event_name, minutes_until=args.minutes_until),
            ts=ts,
        )
    if kind is EventKind.TOOLS_ROLLUP:
        return Event(
            kind,
            ToolsRollupData(tool=args.tool, calls=args.calls, success_rate=args.success_rate),
            ts=ts,
        )
    if kind is EventKind.MEMORY_WRITE:
        return Event(kind, MemoryWriteData(key=args.key, source=args.source), ts=ts)
    if kind is EventKind.PROXIMITY:
        return Event(kind, ProximityData(distance_cm=args.distance_cm), ts=ts)
    if kind is EventKind.ERROR:
        return Event(kind, ErrorData(code=args.code, message=args.message), ts=ts)
    if kind is EventKind.CHARACTER_SET:
        return Event(kind, CharacterSetData(character=args.character), ts=ts)

    raise ValueError(f"unsupported event kind: {args.kind}")


def _cmd_emit(args: argparse.Namespace) -> int:
    ts = time.time() if args.timestamp else None
    event = _build_event_from_args(args, ts)
    print(dumps(event))
    return 0


def _cmd_serve(args: argparse.Namespace) -> int:
    pump = EventPump()

    if args.playback:
        blob = Path(args.playback).read_text(encoding="utf-8")
        pump.enqueue_many(load_many(blob))
    elif args.demo:
        pump.enqueue_many(demo_events())

    transport = MockBleSession()
    server = BuddyServer(transport=transport, pump=pump)
    sent = server.run(max_steps=args.max_steps)

    if args.print_outbound:
        for line in transport.sent_lines:
            print(line)

    if args.summary:
        print(f"sent={sent} received={len(server.received_events)}")

    return 0


def _cmd_simulate(args: argparse.Namespace) -> int:
    blob = Path(args.playback).read_text(encoding="utf-8")
    events = list(load_many(blob))
    selected_character = CharacterSetData(character=args.character).character

    for idx, ev in enumerate(events, start=1):
        response = simulate_event(ev, selected_character)
        if args.format == "json":
            print(
                json.dumps(
                    {
                        "scenario_index": idx,
                        "source_event": response.event,
                        "led": response.led,
                        "has_motion": response.has_motion,
                        "head_x_deg": response.head_x_deg,
                        "head_y_deg": response.head_y_deg,
                        "duration_ms": response.duration_ms,
                        "note": response.note,
                    },
                    sort_keys=True,
                    separators=(",", ":"),
                )
            )
        else:
            motion = (
                f"x={response.head_x_deg:.1f} y={response.head_y_deg:.1f} dur={response.duration_ms}ms"
                if response.has_motion
                else "still"
            )
            print(
                f"[{idx:02d}] {response.event} -> led={response.led} motion={motion} note={response.note}"
            )

    print(f"simulated={len(events)} character={selected_character.value}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="pre-buddy",
        description="PRE Buddy server development CLI.",
    )
    sub = p.add_subparsers(dest="cmd", required=True)

    sub.add_parser("version", help="print version").set_defaults(func=_cmd_version)

    serve = sub.add_parser("serve", help="run mock BLE session + event pump")
    serve.add_argument("--playback", help="path to JSON-lines file to send")
    serve.add_argument("--demo", action="store_true", help="enqueue built-in demo event sequence")
    serve.add_argument("--max-steps", type=int, default=None, help="optional server loop step limit")
    serve.add_argument("--print-outbound", action="store_true", default=True, help="print sent JSON lines")
    serve.add_argument("--summary", action="store_true", default=True, help="print sent/received summary")
    serve.set_defaults(func=_cmd_serve)

    simulate = sub.add_parser("simulate", help="simulate robot responses from a playback JSON-lines file")
    simulate.add_argument("--playback", required=True, help="path to JSON-lines input events")
    simulate.add_argument("--character", default="sage", choices=["sage", "sprout", "sentinel"])
    simulate.add_argument("--format", default="text", choices=["text", "json"])
    simulate.set_defaults(func=_cmd_simulate)

    emit = sub.add_parser("emit", help="emit a sample event as JSON-line")
    emit.add_argument("kind", choices=[k.value for k in EventKind], help="event kind to emit")
    emit.add_argument("--timestamp", action="store_true", help="include ts field")

    # Common knobs (only used by relevant event kind)
    emit.add_argument("--mic", default="unknown", choices=["left", "right", "unknown"])
    emit.add_argument("--agent-id", default="demo-agent")
    emit.add_argument("--state", default="started", choices=["started", "running", "finished", "failed"])
    emit.add_argument("--tier", default=Tier.FAST.value, choices=[t.value for t in Tier])
    emit.add_argument("--from-tier", default=Tier.FAST.value, choices=[t.value for t in Tier])
    emit.add_argument("--to-tier", default=Tier.STANDARD.value, choices=[t.value for t in Tier])
    emit.add_argument("--reason", default="demo")
    emit.add_argument("--domain", default="general")
    emit.add_argument("--confidence", type=float, default=0.3)
    emit.add_argument("--threshold", type=float, default=0.6)
    emit.add_argument("--entities-added", type=int, default=1)
    emit.add_argument("--relations-added", type=int, default=1)
    emit.add_argument("--examples-total", type=int, default=10)
    emit.add_argument("--goal-examples", type=int, default=100)
    emit.add_argument("--event-name", default="demo-event")
    emit.add_argument("--minutes-until", type=int, default=60)
    emit.add_argument("--tool", default="web_search")
    emit.add_argument("--calls", type=int, default=1)
    emit.add_argument("--success-rate", type=float, default=1.0)
    emit.add_argument("--key", default="note:demo")
    emit.add_argument("--source", default="demo")
    emit.add_argument("--distance-cm", type=float, default=35.0)
    emit.add_argument("--code", default="E_DEMO")
    emit.add_argument("--message", default="demo error")
    emit.add_argument("--character", default="sage", choices=["sage", "sprout", "sentinel"])
    emit.set_defaults(func=_cmd_emit)

    return p


def main(argv: Sequence[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    return int(args.func(args))


if __name__ == "__main__":  # pragma: no cover
    sys.exit(main())
