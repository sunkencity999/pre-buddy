from __future__ import annotations

from pre_buddy.events import Event, EventKind, WakeWordData
from pre_buddy.pump import EventPump
from pre_buddy.serve import BuddyServer
from pre_buddy.transport import MockBleSession
from pre_buddy.serializer import dumps


def test_buddy_server_sends_pump_events_to_transport():
    pump = EventPump()
    pump.enqueue(Event(EventKind.WAKE_WORD, WakeWordData(source_mic="left")))

    transport = MockBleSession()
    server = BuddyServer(transport=transport, pump=pump)
    sent = server.run()

    assert sent == 1
    assert len(transport.sent_lines) == 1
    assert "pre.system.wake_word" in transport.sent_lines[0]


def test_buddy_server_hydrates_inbound_events():
    inbound = dumps(Event(EventKind.WAKE_WORD, WakeWordData(source_mic="right")))
    transport = MockBleSession(inbound_lines=[inbound])
    server = BuddyServer(transport=transport, pump=EventPump())

    sent = server.run(max_steps=3)

    assert sent == 0
    assert len(server.received_events) == 1
    assert server.received_events[0].kind is EventKind.WAKE_WORD
