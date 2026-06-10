"""Conversational loop orchestrator: BLE device ↔ AudioBridge.

This is the host half of the "say 'Hi, ESP' → speak → PRE answers out loud"
loop. The firmware streams microphone audio up as ``pre.audio.input_*``
events and plays back ``pre.audio.output_*`` events; this orchestrator sits
in the middle and drives the :class:`~pre_buddy.audio_bridge.AudioBridge`:

    device  --(input_start / input_frame× / input_stop)-->  AudioBridge
                                                                 │  STT → PRE → TTS
    device  <--(output_start / output_frame× / output_stop)--   AudioBridge

The transport is anything with the :class:`BleNusTransport` surface
(``connected`` / ``send_line`` / ``recv_line``), so the whole loop runs
against :class:`FakeBleBackend` in tests with no radio.

Framing note: ``send_line`` writes one GATT op per output event. Audio
*output* frames are large, so the real BLE path will want
write-without-response fragmentation eventually (mirroring the firmware's
notify fragmentation); that optimization is deferred — correctness first.
"""

from __future__ import annotations

import time
from typing import Optional, Protocol

from .audio_bridge import AudioBridge
from .serializer import loads
from .transport import TransportError


class _LineTransport(Protocol):
    """The slice of :class:`BleNusTransport` the orchestrator needs."""

    @property
    def connected(self) -> bool: ...
    def send_line(self, line: str) -> None: ...
    def recv_line(self) -> Optional[str]: ...


class ConversationOrchestrator:
    """Pumps audio events between a BLE transport and an :class:`AudioBridge`.

    The stepping methods (:meth:`process_line`, :meth:`poll_once`) are pure
    and synchronous so tests can drive them deterministically; :meth:`run`
    is the blocking poll loop used by the CLI runner.
    """

    def __init__(
        self,
        transport: _LineTransport,
        bridge: AudioBridge,
        *,
        poll_interval_s: float = 0.02,
        on_line: Optional[callable] = None,
    ) -> None:
        self._t = transport
        self._bridge = bridge
        self._poll = poll_interval_s
        self._on_line = on_line  # optional observer(direction, line) for logging
        self._running = False

    # ── outbound drain ──────────────────────────────────────────────────

    def _flush_outbound(self) -> int:
        """Send every queued bridge output event back to the device."""
        sent = 0
        for line in self._bridge.pump.iter_lines():
            self._t.send_line(line)
            if self._on_line:
                self._on_line("out", line)
            sent += 1
        return sent

    def announce_codec(self, *, name: str = "pcm16", sample_rate_hz: int = 16000) -> int:
        """Tell the device which wire codec to expect, then flush it out."""
        self._bridge.publish_codec(
            name=name, sample_rate_hz=sample_rate_hz, bitrate_bps=sample_rate_hz * 16
        )
        return self._flush_outbound()

    # ── inbound stepping ────────────────────────────────────────────────

    def process_line(self, line: str) -> int:
        """Decode one inbound device line, run it through the bridge, and
        send back any resulting output events. Returns lines sent.

        A malformed line is logged and skipped — one bad frame must not kill
        a long-running loop.
        """
        line = line.strip()
        if not line:
            return 0
        if self._on_line:
            self._on_line("in", line)
        try:
            event = loads(line)
        except Exception as exc:  # noqa: BLE001 — never let one bad line stop us
            if self._on_line:
                self._on_line("err", f"{type(exc).__name__}: {line[:80]}")
            return 0
        self._bridge.handle_event(event)
        return self._flush_outbound()

    def poll_once(self) -> bool:
        """Process at most one queued inbound line. True if one was handled."""
        try:
            line = self._t.recv_line()
        except TransportError:
            return False
        if line is None:
            return False
        self.process_line(line)
        return True

    # ── blocking loop (CLI) ─────────────────────────────────────────────

    def run(self) -> None:
        """Poll the transport until stopped or disconnected."""
        self._running = True
        while self._running and self._t.connected:
            if not self.poll_once():
                time.sleep(self._poll)

    def stop(self) -> None:
        self._running = False
