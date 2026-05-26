from __future__ import annotations

import json
from pathlib import Path

from pre_buddy.events import Character
from pre_buddy.serializer import load_many
from pre_buddy.simulate import build_timeline_rows

ROOT = Path(__file__).resolve().parents[1]
EXAMPLES = ROOT / "examples"
GOLDEN = Path(__file__).resolve().parent / "golden"


def _simulate(path: Path, *, character: Character, severity: str) -> list[dict[str, object]]:
    events = list(load_many(path.read_text(encoding="utf-8")))
    return build_timeline_rows(events, character=character, severity=severity)


def _load_golden(name: str) -> list[dict[str, object]]:
    return json.loads((GOLDEN / name).read_text(encoding="utf-8"))


def test_golden_alerts_sentinel_loud():
    rows = _simulate(EXAMPLES / "alerts_scenario.jsonl", character=Character.SENTINEL, severity="loud")
    assert rows == _load_golden("alerts_sentinel_loud.json")


def test_golden_daily_sprout_normal():
    rows = _simulate(EXAMPLES / "daily_flow_scenario.jsonl", character=Character.SPROUT, severity="normal")
    assert rows == _load_golden("daily_sprout_normal.json")
