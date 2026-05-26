"""Transport primitives for PRE Buddy server runtime.

For now this includes a mock BLE session used for host-side integration tests.
A real BLE/NUS transport will implement the same shape later.
"""

from __future__ import annotations

from collections import deque
from dataclasses import dataclass, field
from typing import Deque, Iterable


class TransportError(RuntimeError):
    """Raised when transport operations are invalid for current state."""


@dataclass
class MockBleSession:
    """In-memory stand-in for a BLE NUS session.

    - ``inbound_lines`` emulate device->server messages waiting to be read.
    - ``sent_lines`` captures server->device writes for assertions.
    """

    inbound_lines: Iterable[str] = field(default_factory=tuple)
    connected: bool = False

    def __post_init__(self) -> None:
        self._inbound: Deque[str] = deque(self.inbound_lines)
        self.sent_lines: list[str] = []

    def open(self) -> None:
        self.connected = True

    def close(self) -> None:
        self.connected = False

    def send_line(self, line: str) -> None:
        if not self.connected:
            raise TransportError("cannot send while disconnected")
        self.sent_lines.append(line)

    def recv_line(self) -> str | None:
        if not self.connected:
            raise TransportError("cannot receive while disconnected")
        if not self._inbound:
            return None
        return self._inbound.popleft()

    def inject_inbound(self, line: str) -> None:
        self._inbound.append(line)
