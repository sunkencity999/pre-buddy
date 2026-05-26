"""Tests for the BLE/NUS transport using an in-memory backend.

These exercise the transport's framing, lifecycle, and queue ordering
without ever touching the real ``bleak`` stack.
"""

from __future__ import annotations

import pytest

from pre_buddy.transport import TransportError
from pre_buddy.transport_ble import (
    BleNusTransport,
    FakeBleBackend,
)
from pre_buddy.uuids import (
    NUS_RX_CHAR_UUID,
    NUS_SERVICE_UUID,
    NUS_TX_CHAR_UUID,
)


# ── UUID constants ─────────────────────────────────────────────────────


def test_nus_uuids_are_standard_nordic_values() -> None:
    # These are baked into nRF Connect and every NUS client out there.
    # Drift here would silently break interop with off-the-shelf tools.
    assert NUS_SERVICE_UUID == "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
    assert NUS_RX_CHAR_UUID == "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
    assert NUS_TX_CHAR_UUID == "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"


# ── BleNusTransport: lifecycle ────────────────────────────────────────


def test_open_connects_backend_and_is_idempotent() -> None:
    backend = FakeBleBackend()
    transport = BleNusTransport(backend)

    transport.open()
    assert transport.connected
    assert backend.connect_calls == 1

    transport.open()  # second open is a no-op
    assert backend.connect_calls == 1


def test_close_disconnects_backend() -> None:
    backend = FakeBleBackend()
    transport = BleNusTransport(backend)
    transport.open()
    transport.close()
    assert not transport.connected
    assert backend.disconnect_calls == 1


def test_open_raises_when_backend_refuses_to_connect() -> None:
    backend = FakeBleBackend(fail_on_connect=True)
    transport = BleNusTransport(backend)
    with pytest.raises(TransportError):
        transport.open()


# ── BleNusTransport: send path ────────────────────────────────────────


def test_send_line_writes_to_rx_as_utf8_bytes() -> None:
    backend = FakeBleBackend()
    transport = BleNusTransport(backend)
    transport.open()
    transport.send_line('{"event":"pre.system.proximity","data":{"distance_cm":35}}')
    assert len(backend.rx_writes) == 1
    assert backend.rx_writes[0].startswith(b'{"event":"pre.system.proximity"')
    assert backend.rx_writes[0].endswith(b"}")


def test_send_line_records_sent_for_observability() -> None:
    backend = FakeBleBackend()
    transport = BleNusTransport(backend)
    transport.open()
    transport.send_line("a")
    transport.send_line("b")
    assert transport.sent_lines == ["a", "b"]


def test_send_line_rejects_embedded_newlines() -> None:
    backend = FakeBleBackend()
    transport = BleNusTransport(backend)
    transport.open()
    # Newline framing is "one write == one line"; embedded newlines would
    # confuse the peripheral. Reject early instead of corrupting the stream.
    with pytest.raises(ValueError):
        transport.send_line('{"event":"a"}\n{"event":"b"}')


def test_send_line_raises_when_disconnected() -> None:
    backend = FakeBleBackend()
    transport = BleNusTransport(backend)
    with pytest.raises(TransportError):
        transport.send_line("x")


# ── BleNusTransport: recv path ────────────────────────────────────────


def test_recv_returns_none_when_no_data() -> None:
    backend = FakeBleBackend()
    transport = BleNusTransport(backend)
    transport.open()
    assert transport.recv_line() is None


def test_recv_drains_backend_tx_queue_in_order() -> None:
    backend = FakeBleBackend()
    transport = BleNusTransport(backend)
    transport.open()
    backend.push_tx('{"event":"pre.system.wake_word"}')
    backend.push_tx('{"event":"pre.bg_agents.change"}')

    a = transport.recv_line()
    b = transport.recv_line()
    c = transport.recv_line()
    assert a == '{"event":"pre.system.wake_word"}'
    assert b == '{"event":"pre.bg_agents.change"}'
    assert c is None


def test_recv_raises_when_disconnected() -> None:
    backend = FakeBleBackend()
    transport = BleNusTransport(backend)
    with pytest.raises(TransportError):
        transport.recv_line()


def test_inject_inbound_is_drained_before_backend_tx() -> None:
    # MockBleSession compatibility: injected lines come out first so any
    # test scripted against the mock keeps working against BLE.
    backend = FakeBleBackend()
    transport = BleNusTransport(backend)
    transport.open()
    transport.inject_inbound("injected-1")
    backend.push_tx("from-backend")
    transport.inject_inbound("injected-2")

    assert transport.recv_line() == "injected-1"
    assert transport.recv_line() == "injected-2"
    assert transport.recv_line() == "from-backend"
    assert transport.recv_line() is None


# ── Integration with the existing BuddyServer ─────────────────────────


def test_buddy_server_runs_against_ble_transport() -> None:
    # The transport must be drop-in compatible with BuddyServer so that
    # `serve --transport ble` produces the same behaviour as the mock
    # path. Use the same demo events as the mock test.
    from pre_buddy.pump import EventPump, demo_events
    from pre_buddy.serve import BuddyServer

    backend = FakeBleBackend()
    transport = BleNusTransport(backend)
    pump = EventPump()
    pump.enqueue_many(demo_events())
    server = BuddyServer(transport=transport, pump=pump)  # type: ignore[arg-type]

    sent = server.run(max_steps=20)

    assert sent == len(demo_events())
    assert len(backend.rx_writes) == sent
    # Every write is a JSON object — sanity-check the first/last bytes.
    for w in backend.rx_writes:
        assert w.startswith(b"{") and w.endswith(b"}")


def test_buddy_server_receives_backend_tx_lines() -> None:
    # When the peripheral pushes a line via TX, BuddyServer's loop should
    # collect it into received_events. Push the line before run() so it's
    # visible when the loop polls.
    from pre_buddy.pump import EventPump
    from pre_buddy.serve import BuddyServer

    backend = FakeBleBackend()
    transport = BleNusTransport(backend)
    backend.push_tx('{"event":"pre.system.wake_word","data":{"source_mic":"left"}}')

    pump = EventPump()
    server = BuddyServer(transport=transport, pump=pump)  # type: ignore[arg-type]
    server.run(max_steps=5)

    assert len(server.received_events) == 1
    assert server.received_events[0].kind.value == "pre.system.wake_word"
