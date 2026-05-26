from __future__ import annotations

from pre_buddy.events import EventKind
from pre_buddy.generated_event_kinds import EVENT_KIND_NAMES


def test_eventkind_enum_matches_generated_schema_list():
    enum_names = tuple(k.value for k in EventKind)
    assert enum_names == EVENT_KIND_NAMES
