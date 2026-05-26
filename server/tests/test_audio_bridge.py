"""Tests for the server-side audio bridge."""

from __future__ import annotations

import base64
from dataclasses import dataclass, field

import pytest

from pre_buddy.audio_bridge import AudioBridge
from pre_buddy.events import (
    AudioInputFrameData,
    AudioInputStartData,
    AudioInputStopData,
    AudioOutputFrameData,
    Event,
    EventKind,
)
from pre_buddy.pump import EventPump


# ── helpers ────────────────────────────────────────────────────────────


@dataclass
class RecordingStt:
    """STT mock — returns ``response`` for whatever it's given."""

    response: str = "hello world"
    calls: list[bytes] = field(default_factory=list)

    def transcribe(self, pcm16_mono: bytes, *, sample_rate_hz: int) -> str:
        self.calls.append(pcm16_mono)
        return self.response


@dataclass
class ChunkingTts:
    """TTS mock — yields one bytestring per word so we can count frames."""

    calls: list[str] = field(default_factory=list)

    def synthesize(self, text: str, *, sample_rate_hz: int):
        self.calls.append(text)
        for word in text.split():
            yield word.encode("ascii")


@dataclass
class RecordingPreClient:
    reply: str = "ack"
    asks: list[str] = field(default_factory=list)

    def ask(self, user_text: str) -> str:
        self.asks.append(user_text)
        return self.reply


def _make_bridge(**adapters):
    pump = EventPump()
    return AudioBridge(pump=pump, **adapters), pump


def _b64(payload: bytes) -> str:
    return base64.b64encode(payload).decode("ascii")


def _drain(pump: EventPump) -> list[Event]:
    out = []
    while True:
        ev = pump.pop_next()
        if ev is None:
            return out
        out.append(ev)


# ── inbound (device → server) lifecycle ───────────────────────────────


def test_full_round_trip_produces_transcript_assistant_reply_and_output_frames():
    stt = RecordingStt(response="what time is it")
    tts = ChunkingTts()
    pre_ = RecordingPreClient(reply="three pm")
    bridge, pump = _make_bridge(stt=stt, tts=tts, pre_client=pre_)

    sid = "sess-1"
    bridge.handle_event(Event(
        EventKind.AUDIO_INPUT_START,
        AudioInputStartData(session_id=sid),
    ))
    bridge.handle_event(Event(
        EventKind.AUDIO_INPUT_FRAME,
        AudioInputFrameData(session_id=sid, seq=0, data=_b64(b"abc")),
    ))
    bridge.handle_event(Event(
        EventKind.AUDIO_INPUT_FRAME,
        AudioInputFrameData(session_id=sid, seq=1, data=_b64(b"def")),
    ))
    bridge.handle_event(Event(
        EventKind.AUDIO_INPUT_STOP,
        AudioInputStopData(session_id=sid),
    ))

    # STT saw all accumulated bytes, in order.
    assert stt.calls == [b"abcdef"]
    # Bridge stored the transcript + asked PRE.
    assert bridge.transcripts == ["what time is it"]
    assert pre_.asks == ["what time is it"]
    assert bridge.assistant_replies == ["three pm"]

    # The output side emitted start + 2 frames ("three", "pm") + stop.
    events = _drain(pump)
    kinds = [ev.kind for ev in events]
    assert kinds == [
        EventKind.AUDIO_OUTPUT_START,
        EventKind.AUDIO_OUTPUT_FRAME,
        EventKind.AUDIO_OUTPUT_FRAME,
        EventKind.AUDIO_OUTPUT_STOP,
    ]
    payloads = [base64.b64decode(ev.data.data) for ev in events if isinstance(ev.data, AudioOutputFrameData)]
    assert payloads == [b"three", b"pm"]


def test_empty_transcript_skips_pre_round_trip():
    # If STT can't make sense of the audio (silence, noise) we keep the
    # bridge quiet rather than spamming PRE with empty messages.
    stt = RecordingStt(response="   ")
    pre_ = RecordingPreClient()
    bridge, pump = _make_bridge(stt=stt, pre_client=pre_)

    sid = "sess-empty"
    bridge.handle_event(Event(EventKind.AUDIO_INPUT_START, AudioInputStartData(session_id=sid)))
    bridge.handle_event(Event(EventKind.AUDIO_INPUT_FRAME, AudioInputFrameData(session_id=sid, seq=0, data=_b64(b"_"))))
    bridge.handle_event(Event(EventKind.AUDIO_INPUT_STOP, AudioInputStopData(session_id=sid)))

    assert pre_.asks == []
    # No output events either.
    assert _drain(pump) == []


def test_frame_for_unknown_session_is_dropped():
    stt = RecordingStt()
    bridge, pump = _make_bridge(stt=stt)
    # Frame arrives without a matching start.
    bridge.handle_event(Event(
        EventKind.AUDIO_INPUT_FRAME,
        AudioInputFrameData(session_id="ghost", seq=0, data=_b64(b"x")),
    ))
    # And a stop for the ghost session does nothing.
    bridge.handle_event(Event(
        EventKind.AUDIO_INPUT_STOP,
        AudioInputStopData(session_id="ghost"),
    ))
    assert stt.calls == []
    assert _drain(pump) == []


def test_session_id_mismatch_between_start_and_frame_drops_frame():
    stt = RecordingStt()
    bridge, _ = _make_bridge(stt=stt)
    bridge.handle_event(Event(EventKind.AUDIO_INPUT_START, AudioInputStartData(session_id="a")))
    bridge.handle_event(Event(
        EventKind.AUDIO_INPUT_FRAME,
        AudioInputFrameData(session_id="b", seq=0, data=_b64(b"x")),
    ))
    bridge.handle_event(Event(EventKind.AUDIO_INPUT_STOP, AudioInputStopData(session_id="a")))
    # The bridge correctly drained "a" but with zero frames.
    assert stt.calls == [b""]


def test_bad_base64_emits_audio_error():
    bridge, pump = _make_bridge()
    bridge.handle_event(Event(EventKind.AUDIO_INPUT_START, AudioInputStartData(session_id="x")))
    # Real callers would never send a non-base64 string, but defensive
    # decode is the only protection against a misbehaving firmware.
    bridge.handle_event(Event(EventKind.AUDIO_INPUT_FRAME,
                              AudioInputFrameData(session_id="x", seq=0, data="!!!not_base64!!!")))
    out = _drain(pump)
    assert any(ev.kind is EventKind.AUDIO_ERROR for ev in out)
    err = next(ev for ev in out if ev.kind is EventKind.AUDIO_ERROR)
    assert err.data.code == "E_BAD_BASE64"


# ── outbound (server → device) ────────────────────────────────────────


def test_speak_with_explicit_session_id_reuses_it_for_all_frames():
    tts = ChunkingTts()
    bridge, pump = _make_bridge(tts=tts)
    sid = bridge.speak("a b c", session_id="cli-sess")
    assert sid == "cli-sess"
    events = _drain(pump)
    assert all(ev.data.session_id == "cli-sess" for ev in events)


def test_speak_with_empty_text_still_sends_start_and_stop():
    # Even an empty reply benefits from the start/stop bookends so the
    # device-side state machine doesn't get stuck mid-output.
    tts = ChunkingTts()
    bridge, pump = _make_bridge(tts=tts)
    bridge.speak("")
    events = _drain(pump)
    assert [ev.kind for ev in events] == [
        EventKind.AUDIO_OUTPUT_START,
        EventKind.AUDIO_OUTPUT_STOP,
    ]


def test_speak_increments_seq_per_frame():
    tts = ChunkingTts()
    bridge, pump = _make_bridge(tts=tts)
    bridge.speak("one two three four")
    frames = [ev for ev in _drain(pump) if ev.kind is EventKind.AUDIO_OUTPUT_FRAME]
    assert [f.data.seq for f in frames] == [0, 1, 2, 3]


# ── codec hand-shake ──────────────────────────────────────────────────


def test_publish_codec_emits_audio_codec_event_with_defaults():
    bridge, pump = _make_bridge()
    bridge.publish_codec()
    events = _drain(pump)
    assert len(events) == 1
    assert events[0].kind is EventKind.AUDIO_CODEC
    assert events[0].data.name == "opus"
    assert events[0].data.sample_rate_hz == 16000
    assert events[0].data.bitrate_bps == 32000


def test_publish_codec_passes_overrides_through():
    bridge, pump = _make_bridge()
    bridge.publish_codec(name="pcm16", sample_rate_hz=8000, bitrate_bps=128000)
    events = _drain(pump)
    assert events[0].data.name == "pcm16"
    assert events[0].data.sample_rate_hz == 8000


# ── unrelated events do not leak into bridge state ────────────────────


def test_handle_event_ignores_non_inbound_audio_events():
    # The serve loop hands every received event to handle_event; the
    # bridge should silently ignore anything outside its concern so it
    # composes with the embodiment dispatcher. AUDIO_ERROR is something
    # the bridge *emits*, not consumes — so handing it back as inbound
    # must not invalidate state or produce side effects.
    from pre_buddy.events import AudioErrorData
    bridge, pump = _make_bridge()
    bridge.handle_event(Event(
        EventKind.AUDIO_ERROR,
        AudioErrorData(code="ignored", message="bridge should not react"),
    ))
    assert _drain(pump) == []
