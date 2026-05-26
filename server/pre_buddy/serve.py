"""Server runtime skeleton: mock transport + event pump.

This is the first real ``serve`` path for host-side iteration before hardware.
"""

from __future__ import annotations

from dataclasses import dataclass, field

from .events import Event
from .pump import EventPump
from .serializer import dumps, loads
from .transport import MockBleSession


@dataclass
class BuddyServer:
    transport: MockBleSession
    pump: EventPump
    received_events: list[Event] = field(default_factory=list)

    def run(self, *, max_steps: int | None = None) -> int:
        """Run the mock session until queue drains or max_steps reached.

        Returns number of outbound lines sent.
        """
        sent = 0
        self.transport.open()
        try:
            steps = 0
            while True:
                if max_steps is not None and steps >= max_steps:
                    break

                progress = False

                outbound = self.pump.pop_next()
                if outbound is not None:
                    self.transport.send_line(dumps(outbound))
                    sent += 1
                    progress = True

                inbound = self.transport.recv_line()
                if inbound:
                    self.received_events.append(loads(inbound))
                    progress = True

                steps += 1
                if not progress:
                    break
        finally:
            self.transport.close()

        return sent
