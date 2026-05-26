from __future__ import annotations

import pytest

from pre_buddy.transport import MockBleSession, TransportError


def test_mock_ble_session_send_and_receive():
    session = MockBleSession(inbound_lines=["one", "two"])
    session.open()

    session.send_line("outbound")
    assert session.sent_lines == ["outbound"]

    assert session.recv_line() == "one"
    assert session.recv_line() == "two"
    assert session.recv_line() is None

    session.close()


def test_mock_ble_session_raises_when_disconnected():
    session = MockBleSession()
    with pytest.raises(TransportError):
        session.send_line("x")
    with pytest.raises(TransportError):
        session.recv_line()
