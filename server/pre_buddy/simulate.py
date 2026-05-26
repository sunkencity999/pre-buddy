"""Scenario simulation helpers for software robot playback."""

from __future__ import annotations

import csv
import io
import json
from typing import Iterable, Sequence

from .events import Character, Event
from .mock_robot import simulate_event


def build_timeline_rows(
    events: Sequence[Event] | Iterable[Event],
    *,
    character: Character,
    severity: str = "normal",
) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for idx, ev in enumerate(events, start=1):
        response = simulate_event(ev, character, severity=severity)
        rows.append(
            {
                "scenario_index": idx,
                "source_event": response.event,
                "led": response.led,
                "has_motion": response.has_motion,
                "head_x_deg": response.head_x_deg,
                "head_y_deg": response.head_y_deg,
                "duration_ms": response.duration_ms,
                "note": response.note,
                "expression": response.expression,
                "character": character.value,
                "severity": severity,
            }
        )
    return rows


def render_rows_text(rows: Sequence[dict[str, object]]) -> str:
    out: list[str] = []
    for row in rows:
        if row["has_motion"]:
            motion = (
                f"x={float(row['head_x_deg']):.1f} y={float(row['head_y_deg']):.1f} "
                f"dur={int(row['duration_ms'])}ms"
            )
        else:
            motion = "still"
        out.append(
            f"[{int(row['scenario_index']):02d}] {row['source_event']} -> "
            f"led={row['led']} motion={motion} note={row['note']}"
        )
    if rows:
        out.append(f"simulated={len(rows)} character={rows[0]['character']} severity={rows[0]['severity']}")
    else:
        out.append("simulated=0")
    return "\n".join(out)


def render_rows_json(rows: Sequence[dict[str, object]]) -> str:
    return json.dumps(rows, sort_keys=True, separators=(",", ":"))


def render_rows_csv(rows: Sequence[dict[str, object]]) -> str:
    fieldnames = [
        "scenario_index",
        "source_event",
        "led",
        "has_motion",
        "head_x_deg",
        "head_y_deg",
        "duration_ms",
        "note",
        "expression",
        "character",
        "severity",
    ]
    buf = io.StringIO()
    writer = csv.DictWriter(buf, fieldnames=fieldnames)
    writer.writeheader()
    writer.writerows(rows)
    return buf.getvalue().rstrip("\n")
