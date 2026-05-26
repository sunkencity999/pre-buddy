"""Standalone CLI skeleton for development.

In production this is mounted under the main ``pre`` CLI as ``pre buddy serve``;
during pre-hardware development we expose a small standalone command for
emitting and inspecting events.
"""

from __future__ import annotations

import argparse
import sys
import time
from typing import Sequence

from . import __version__
from .events import (
    BgAgentChangeData,
    CharacterSetData,
    ConfidenceWarningData,
    ErrorData,
    Event,
    EventKind,
    WakeWordData,
)
from .serializer import dumps


def _cmd_version(_args: argparse.Namespace) -> int:
    print(f"pre-buddy {__version__}")
    return 0


def _cmd_emit(args: argparse.Namespace) -> int:
    """Emit a single sample event of the requested kind to stdout."""
    ts = time.time() if args.timestamp else None
    kind = EventKind(args.kind)

    if kind is EventKind.WAKE_WORD:
        ev = Event(kind, WakeWordData(source_mic=args.mic), ts=ts)
    elif kind is EventKind.BG_AGENT_CHANGE:
        ev = Event(
            kind,
            BgAgentChangeData(agent_id=args.agent_id, state=args.state, tier=args.tier),
            ts=ts,
        )
    elif kind is EventKind.CONFIDENCE_WARNING:
        ev = Event(
            kind,
            ConfidenceWarningData(
                domain=args.domain, confidence=args.confidence, threshold=args.threshold
            ),
            ts=ts,
        )
    elif kind is EventKind.ERROR:
        ev = Event(kind, ErrorData(code=args.code, message=args.message), ts=ts)
    elif kind is EventKind.CHARACTER_SET:
        ev = Event(kind, CharacterSetData(character=args.character), ts=ts)
    else:  # pragma: no cover - argparse choices guards this
        print(f"unknown event kind: {args.kind}", file=sys.stderr)
        return 2

    print(dumps(ev))
    return 0


def _cmd_serve(_args: argparse.Namespace) -> int:
    """Placeholder until BLE NUS peripheral lands (DESIGN.md S0)."""
    print(
        "pre-buddy serve: BLE NUS peripheral not yet implemented "
        "(see DESIGN.md S0). This is a scaffold.",
        file=sys.stderr,
    )
    return 0


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="pre-buddy",
        description="PRE Buddy server (scaffold). See DESIGN.md for the full plan.",
    )
    sub = p.add_subparsers(dest="cmd", required=True)

    sub.add_parser("version", help="print version").set_defaults(func=_cmd_version)
    sub.add_parser("serve", help="(stub) run the BLE NUS peripheral").set_defaults(
        func=_cmd_serve
    )

    emit = sub.add_parser("emit", help="emit a sample event as JSON-line")
    emit.add_argument(
        "kind",
        choices=[k.value for k in EventKind],
        help="event kind to emit",
    )
    emit.add_argument("--timestamp", action="store_true", help="include ts field")
    emit.add_argument("--mic", default="unknown", choices=["left", "right", "unknown"])
    emit.add_argument("--agent-id", default="demo-agent")
    emit.add_argument(
        "--state", default="started", choices=["started", "running", "finished", "failed"]
    )
    emit.add_argument(
        "--tier", default="fast", choices=["fast", "standard", "frontier"]
    )
    emit.add_argument("--domain", default="general")
    emit.add_argument("--confidence", type=float, default=0.3)
    emit.add_argument("--threshold", type=float, default=0.6)
    emit.add_argument("--code", default="E_DEMO")
    emit.add_argument("--message", default="demo error")
    emit.add_argument(
        "--character", default="sage", choices=["sage", "sprout", "sentinel"]
    )
    emit.set_defaults(func=_cmd_emit)

    return p


def main(argv: Sequence[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    return int(args.func(args))


if __name__ == "__main__":  # pragma: no cover
    sys.exit(main())
