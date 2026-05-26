"""Server-side audio bridge: BLE audio frames ↔ STT / TTS.

The bridge sits on top of :class:`BleNusTransport` and turns wire-level
``pre.audio.*`` events into transcripts (one side) and turns assistant
text into outgoing audio frames (the other side). The actual STT and
TTS engines plug in via ``IStt`` / ``ITts`` adapters so the bridge can
be host-tested without depending on Whisper or macOS ``say``.

Wire shape (see ``shared/protocol/events.md``):

    device → server  : input_start → input_frame × N → input_stop
    server → device  : output_start → output_frame × N → output_stop

The bridge stays codec-agnostic at this layer. The default impls assume
the wire payload is base64 of whatever the codec is currently set to
via ``pre.audio.codec`` (Opus is the default; tests use a passthrough
"pcm16" codec to keep the math obvious).
"""

from __future__ import annotations

import base64
import time
from dataclasses import dataclass, field
from typing import Callable, Iterable, Optional, Protocol

from .events import (
    AudioCodecData,
    AudioErrorData,
    AudioInputFrameData,
    AudioInputStartData,
    AudioInputStopData,
    AudioOutputFrameData,
    AudioOutputStartData,
    AudioOutputStopData,
    Event,
    EventKind,
)
from .pump import EventPump


# ── adapter interfaces ────────────────────────────────────────────────


class IStt(Protocol):
    """Speech-to-text adapter.

    ``transcribe`` receives the concatenated PCM16 mono samples from one
    voice session (between input_start and input_stop) and returns the
    detected text. An empty string is acceptable — the bridge will skip
    the round-trip to PRE in that case.
    """

    def transcribe(self, pcm16_mono: bytes, *, sample_rate_hz: int) -> str: ...


class ITts(Protocol):
    """Text-to-speech adapter.

    ``synthesize`` yields PCM16 frames suitable for sending over BLE.
    The bridge iterates the generator and forwards each chunk as an
    ``output_frame`` event so users get audio as soon as the first
    syllable is ready (no need to buffer the whole utterance).
    """

    def synthesize(self, text: str, *, sample_rate_hz: int) -> Iterable[bytes]: ...


class IPreClient(Protocol):
    """Hook for sending the transcript onward to PRE.

    The real impl forwards via PRE's WebSocket; tests use a recorder.
    Returns PRE's assistant text (the bridge then runs it through TTS).
    """

    def ask(self, user_text: str) -> str: ...


# ── default no-op adapters (used when the user hasn't wired real ones) ──


class _EchoStt:
    """Transcript = literal ASCII decoding of the PCM bytes (test default)."""

    def transcribe(self, pcm16_mono: bytes, *, sample_rate_hz: int) -> str:
        return pcm16_mono.decode("ascii", errors="replace").strip()


class _EchoTts:
    """One frame per character — used by tests to count chunks deterministically."""

    def synthesize(self, text: str, *, sample_rate_hz: int):
        for ch in text:
            yield ch.encode("ascii")


class _EchoPreClient:
    """PRE bridge stub: prefixes the user's text so tests can tell apart."""

    def ask(self, user_text: str) -> str:
        return f"reply: {user_text}"


# ── bridge ────────────────────────────────────────────────────────────


@dataclass
class _ActiveSession:
    session_id: str
    sample_rate_hz: int
    codec: str
    chunks: list[bytes] = field(default_factory=list)
    started_at: float = field(default_factory=time.monotonic)


def _next_session_id() -> str:
    return f"sess-{int(time.monotonic() * 1000):x}"


@dataclass
class AudioBridge:
    """Glues incoming audio events to STT, and outbound text to TTS.

    Outbound events land in ``pump`` so callers can drain them through
    a transport (mock or real BLE) using the existing serve loop.
    """

    pump: EventPump
    stt: IStt = field(default_factory=_EchoStt)
    tts: ITts = field(default_factory=_EchoTts)
    pre_client: IPreClient = field(default_factory=_EchoPreClient)
    sample_rate_hz: int = 16000

    _input_session: Optional[_ActiveSession] = None
    _output_seq: int = 0
    _session_id_factory: Callable[[], str] = field(default=_next_session_id)
    # Public for inspection by tests.
    transcripts: list[str] = field(default_factory=list)
    assistant_replies: list[str] = field(default_factory=list)

    # ── ingress: events from the device ─────────────────────────────

    def handle_event(self, event: Event) -> None:
        if event.kind is EventKind.AUDIO_INPUT_START:
            self._on_input_start(event.data)
        elif event.kind is EventKind.AUDIO_INPUT_FRAME:
            self._on_input_frame(event.data)
        elif event.kind is EventKind.AUDIO_INPUT_STOP:
            self._on_input_stop(event.data)
        # Other audio.* events (output_*, codec, error) are emitted by
        # the bridge itself, not consumed here. Silently ignore.

    def _on_input_start(self, data: AudioInputStartData) -> None:
        self._input_session = _ActiveSession(
            session_id=data.session_id,
            sample_rate_hz=data.sample_rate_hz,
            codec=data.codec,
        )

    def _on_input_frame(self, data: AudioInputFrameData) -> None:
        if self._input_session is None or self._input_session.session_id != data.session_id:
            # Frame for an unknown session — drop. Either a duplicate
            # tail from a closed session or a missed start_event.
            return
        try:
            chunk = base64.b64decode(data.data, validate=False)
        except ValueError:
            self.pump.enqueue(Event(
                EventKind.AUDIO_ERROR,
                AudioErrorData(code="E_BAD_BASE64",
                               message=f"frame {data.seq} not base64"),
            ))
            return
        self._input_session.chunks.append(chunk)

    def _on_input_stop(self, data: AudioInputStopData) -> None:
        session = self._input_session
        if session is None or session.session_id != data.session_id:
            return
        self._input_session = None

        combined = b"".join(session.chunks)
        transcript = self.stt.transcribe(
            combined, sample_rate_hz=session.sample_rate_hz
        ).strip()
        self.transcripts.append(transcript)

        if not transcript:
            return

        reply = self.pre_client.ask(transcript)
        self.assistant_replies.append(reply)
        if reply:
            self.speak(reply)

    # ── egress: PRE → speaker ───────────────────────────────────────

    def speak(self, text: str, *, session_id: Optional[str] = None) -> str:
        """Run ``text`` through TTS and push output_* events into the pump.

        Returns the session id used so callers can correlate later
        events (cancellation, completion notifications).
        """
        sid = session_id or self._session_id_factory()
        self.pump.enqueue(Event(
            EventKind.AUDIO_OUTPUT_START,
            AudioOutputStartData(
                session_id=sid,
                sample_rate_hz=self.sample_rate_hz,
                codec="opus",  # the on-device decoder is fixed at "opus" today.
            ),
        ))
        seq = 0
        for chunk in self.tts.synthesize(text, sample_rate_hz=self.sample_rate_hz):
            self.pump.enqueue(Event(
                EventKind.AUDIO_OUTPUT_FRAME,
                AudioOutputFrameData(
                    session_id=sid,
                    seq=seq,
                    data=base64.b64encode(chunk).decode("ascii"),
                ),
            ))
            seq += 1
        self.pump.enqueue(Event(
            EventKind.AUDIO_OUTPUT_STOP,
            AudioOutputStopData(session_id=sid, reason="complete"),
        ))
        return sid

    # ── codec hand-shake ────────────────────────────────────────────

    def publish_codec(self, *, name: str = "opus",
                      sample_rate_hz: int = 16000,
                      bitrate_bps: int = 32000) -> None:
        """Announce the wire codec to the device. Call once after connect."""
        self.pump.enqueue(Event(
            EventKind.AUDIO_CODEC,
            AudioCodecData(
                name=name,
                sample_rate_hz=sample_rate_hz,
                bitrate_bps=bitrate_bps,
            ),
        ))
