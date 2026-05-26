from __future__ import annotations

from pre_buddy.events import Event, EventKind, WakeWordData
from pre_buddy.pump import EventPump, demo_events


def test_event_pump_enqueue_and_pop_order():
    pump = EventPump()
    a = Event(EventKind.WAKE_WORD, WakeWordData(source_mic="left"))
    b = Event(EventKind.WAKE_WORD, WakeWordData(source_mic="right"))
    pump.enqueue(a)
    pump.enqueue(b)

    assert len(pump) == 2
    assert pump.pop_next() == a
    assert pump.pop_next() == b
    assert pump.pop_next() is None


def test_event_pump_iter_lines_drains_queue():
    pump = EventPump()
    pump.enqueue_many(demo_events())

    lines = list(pump.iter_lines())
    assert len(lines) == 5
    assert lines[0].startswith('{"data":{"source_mic":"left"}')
    assert len(pump) == 0
