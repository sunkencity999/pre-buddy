"""Standalone CLI for PRE Buddy server development.

Production target is `pre buddy serve`; this package keeps an equivalent
standalone tool for rapid local iteration.
"""

from __future__ import annotations

import argparse
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
from . import autostart, config as _config
from .bridge import PreBridge
from .setup_wizard import discover_via_bleak, run_setup
from .simulate import (
    build_timeline_rows,
    render_rows_csv,
    render_rows_json,
    render_rows_text,
)
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

    if args.transport == "ble":
        if not args.device_address and not args.device_name:
            print("serve --transport ble requires --device-address or --device-name", file=sys.stderr)
            return 2
        try:
            from .transport_ble import BleakNusBackend, BleNusTransport
        except ImportError as exc:
            print(f"serve --transport ble: {exc}", file=sys.stderr)
            return 2
        try:
            backend = BleakNusBackend(address=args.device_address, name=args.device_name)
        except RuntimeError as exc:
            print(f"serve --transport ble: {exc}", file=sys.stderr)
            return 2
        transport = BleNusTransport(backend, connect_timeout_s=args.connect_timeout)
    else:
        transport = MockBleSession()

    server = BuddyServer(transport=transport, pump=pump)
    sent = server.run(max_steps=args.max_steps)

    if args.print_outbound:
        for line in transport.sent_lines:
            print(line)

    if args.summary:
        print(f"sent={sent} received={len(server.received_events)}")

    return 0


def _cmd_bridge(args: argparse.Namespace) -> int:
    pump = EventPump()
    bridge = PreBridge(pump=pump, ws_url=args.pre_url)

    if args.from_file:
        lines = Path(args.from_file).read_text(encoding="utf-8").splitlines()
        bridge.ingest(line for line in lines if line.strip())
    else:
        import asyncio

        try:
            asyncio.run(bridge.run(max_messages=args.max_messages))
        except KeyboardInterrupt:
            pass
        except RuntimeError as exc:
            print(f"bridge: {exc}", file=sys.stderr)
            return 2

    if args.print_events:
        while True:
            ev = pump.pop_next()
            if ev is None:
                break
            print(dumps(ev))

    if args.summary:
        s = bridge.stats
        print(
            f"received={s.received} forwarded={s.forwarded} "
            f"unmapped={s.unmapped} malformed={s.malformed}",
            file=sys.stderr,
        )
    return 0


def _cmd_setup(args: argparse.Namespace) -> int:
    # Non-interactive path used by scripts/CI: --device-address X
    # (optionally --device-name) + --autostart on|off + --yes skips
    # every prompt by passing answers through.
    if args.non_interactive:
        cfg = _config.load()
        if args.device_address:
            cfg.device_address = args.device_address
        if args.device_name:
            cfg.device_name = args.device_name
        cfg.autostart = args.autostart == "on"
        path = _config.save(cfg)
        if cfg.autostart:
            autostart.install()
        else:
            autostart.uninstall()
        print(f"wrote {path}")
        return 0

    # Interactive flow.
    discover = None if args.no_scan else discover_via_bleak
    run_setup(
        inp=sys.stdin,
        out=sys.stdout,
        discover=discover,
        scan_timeout_s=args.scan_timeout,
    )
    return 0


def _cmd_tray(args: argparse.Namespace) -> int:
    try:
        from .tray import run_tray
    except ImportError as exc:
        print(f"tray: {exc}", file=sys.stderr)
        return 2
    return run_tray(args)


def _cmd_viewer(args: argparse.Namespace) -> int:
    from .viewer import default_viewer_dir, serve

    directory = Path(args.directory).resolve() if args.directory else default_viewer_dir()
    try:
        serve(
            directory=directory,
            host=args.host,
            port=args.port,
            open_browser=not args.no_open,
        )
    except FileNotFoundError as exc:
        print(f"viewer: {exc}", file=sys.stderr)
        return 2
    return 0


def _cmd_simulate(args: argparse.Namespace) -> int:
    blob = Path(args.playback).read_text(encoding="utf-8")
    events = list(load_many(blob))
    selected_character = CharacterSetData(character=args.character).character

    rows = build_timeline_rows(
        events,
        character=selected_character,
        severity=args.severity,
    )

    if args.format == "json":
        output = render_rows_json(rows)
    elif args.format == "csv":
        output = render_rows_csv(rows)
    else:
        output = render_rows_text(rows)

    print(output)
    if args.out:
        Path(args.out).write_text(output + "\n", encoding="utf-8")

    return 0


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="pre-buddy",
        description="PRE Buddy server development CLI.",
    )
    sub = p.add_subparsers(dest="cmd", required=True)

    sub.add_parser("version", help="print version").set_defaults(func=_cmd_version)

    serve = sub.add_parser("serve", help="run BLE session + event pump")
    serve.add_argument(
        "--transport",
        choices=["mock", "ble"],
        default="mock",
        help="mock=in-memory session; ble=real Nordic UART central via bleak",
    )
    serve.add_argument("--device-address", help="BLE device address (ble transport)")
    serve.add_argument("--device-name", help="BLE device advertising name (ble transport)")
    serve.add_argument(
        "--connect-timeout",
        type=float,
        default=10.0,
        help="seconds to wait for scan+connect (ble transport)",
    )
    serve.add_argument("--playback", help="path to JSON-lines file to send")
    serve.add_argument("--demo", action="store_true", help="enqueue built-in demo event sequence")
    serve.add_argument("--max-steps", type=int, default=None, help="optional server loop step limit")
    serve.add_argument("--print-outbound", action="store_true", default=True, help="print sent JSON lines")
    serve.add_argument("--summary", action="store_true", default=True, help="print sent/received summary")
    serve.set_defaults(func=_cmd_serve)

    bridge = sub.add_parser(
        "bridge",
        help="bridge PRE WebSocket events to pre.* protocol events",
    )
    bridge.add_argument("--pre-url", default="ws://localhost:7749", help="PRE WebSocket URL")
    bridge.add_argument(
        "--from-file",
        help="read PRE WS lines from a file instead of connecting (offline test mode)",
    )
    bridge.add_argument(
        "--max-messages",
        type=int,
        default=None,
        help="stop after N PRE WS messages have been received (live mode only)",
    )
    bridge.add_argument(
        "--print-events",
        action="store_true",
        default=True,
        help="print mapped pre.* events to stdout as JSON-lines",
    )
    bridge.add_argument("--summary", action="store_true", default=True, help="print stats to stderr")
    bridge.set_defaults(func=_cmd_bridge)

    setup = sub.add_parser("setup", help="first-time PRE Buddy setup (scan BLE, pick device, autostart)")
    setup.add_argument("--no-scan", action="store_true", help="skip BLE scan, enter address manually")
    setup.add_argument("--scan-timeout", type=float, default=5.0)
    setup.add_argument("--non-interactive", action="store_true", help="use the flags below instead of prompting")
    setup.add_argument("--device-address", default=None)
    setup.add_argument("--device-name", default=None)
    setup.add_argument("--autostart", choices=["on", "off"], default="off")
    setup.set_defaults(func=_cmd_setup)

    tray = sub.add_parser("tray", help="run the system tray app")
    tray.add_argument("--once", action="store_true", help="boot the tray once and exit (smoke test)")
    tray.set_defaults(func=_cmd_tray)

    viewer = sub.add_parser("viewer", help="serve the browser-based scenario viewer")
    viewer.add_argument("--host", default="127.0.0.1")
    viewer.add_argument("--port", type=int, default=7750)
    viewer.add_argument("--directory", help="override the viewer/ directory location")
    viewer.add_argument("--no-open", action="store_true", help="don't auto-open a browser tab")
    viewer.set_defaults(func=_cmd_viewer)

    simulate = sub.add_parser("simulate", help="simulate robot responses from a playback JSON-lines file")
    simulate.add_argument("--playback", required=True, help="path to JSON-lines input events")
    simulate.add_argument("--character", default="sage", choices=["sage", "sprout", "sentinel"])
    simulate.add_argument("--severity", default="normal", choices=["quiet", "normal", "loud"])
    simulate.add_argument("--format", default="text", choices=["text", "json", "csv"])
    simulate.add_argument("--out", help="optional output file path")
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
