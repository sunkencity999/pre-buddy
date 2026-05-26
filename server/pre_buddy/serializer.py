"""JSON-lines serializer for ``pre.*`` events.

One event = one line of compact JSON. Matches the BLE NUS framing used by
the firmware. Helpers cover both single-event and stream cases.
"""

from __future__ import annotations

import json
from typing import Iterable, Iterator

from .events import Event


def dumps(event: Event) -> str:
    """Serialize a single event to a JSON line (no trailing newline)."""
    return json.dumps(event.to_dict(), separators=(",", ":"), sort_keys=True)


def loads(line: str) -> Event:
    """Parse a single JSON-lines string into a typed Event."""
    raw = json.loads(line)
    if not isinstance(raw, dict):
        raise ValueError("event line must decode to an object")
    return Event.from_dict(raw)


def dump_many(events: Iterable[Event]) -> str:
    """Serialize multiple events to a JSON-lines blob (newline-terminated)."""
    return "".join(dumps(ev) + "\n" for ev in events)


def load_many(blob: str) -> Iterator[Event]:
    """Yield typed Events from a JSON-lines blob, skipping blank lines."""
    for line in blob.splitlines():
        line = line.strip()
        if not line:
            continue
        yield loads(line)
