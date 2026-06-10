"""Tests for the conversational-loop orchestrator (no radio).

Drives :class:`ConversationOrchestrator` against the in-memory
:class:`FakeBleBackend`, feeding ``pre.audio.input_*`` lines as if the
device streamed them and asserting the bridge's ``output_*`` events come
back out the transport. Uses the bridge's default echo adapters so the
text path is deterministic: PCM bytes → ASCII transcript → "reply: …" →
one TTS frame per character.
"""

from __future__ import annotations

import base64

from pre_buddy.audio_bridge import AudioBridge
from pre_buddy.conversation import ConversationOrchestrator
from pre_buddy.events import (
    AudioInputFrameData,
    AudioInputStartData,
    AudioInputStopData,
    Event,
    EventKind,
)
from pre_buddy.pump import EventPump
from pre_buddy.serializer import dumps, loads
from pre_buddy.transport_ble import BleNusTransport, FakeBleBackend


def _wired(output_codec: str = "pcm16"):
    backend = FakeBleBackend()
    transport = BleNusTransport(backend)
    transport.open()
    bridge = AudioBridge(pump=EventPump(), output_codec=output_codec)
    orch = ConversationOrchestrator(transport, bridge)
    return backend, transport, orch


def _push_input_session(backend, *, sid="s1", pcm=b"hi", sr=16000):
    backend.push_tx(dumps(Event(
        EventKind.AUDIO_INPUT_START,
        AudioInputStartData(session_id=sid, sample_rate_hz=sr, codec="pcm16"))))
    backend.push_tx(dumps(Event(
        EventKind.AUDIO_INPUT_FRAME,
        AudioInputFrameData(session_id=sid, seq=0,
                            data=base64.b64encode(pcm).decode("ascii")))))
    backend.push_tx(dumps(Event(
        EventKind.AUDIO_INPUT_STOP,
        AudioInputStopData(session_id=sid, reason="manual"))))


def _drain(orch):
    while orch.poll_once():
        pass


def test_input_session_produces_full_output_session():
    backend, transport, orch = _wired()
    _push_input_session(backend, pcm=b"hi")
    _drain(orch)

    events = [loads(line) for line in transport.sent_lines]
    kinds = [e.kind for e in events]
    assert kinds[0] is EventKind.AUDIO_OUTPUT_START
    assert kinds[-1] is EventKind.AUDIO_OUTPUT_STOP
    # echo path: transcript "hi" -> "reply: hi" -> one frame per character
    assert kinds.count(EventKind.AUDIO_OUTPUT_FRAME) == len("reply: hi")


def test_output_start_announces_configured_codec():
    backend, transport, orch = _wired(output_codec="pcm16")
    _push_input_session(backend)
    _drain(orch)

    start = next(loads(l) for l in transport.sent_lines
                 if loads(l).kind is EventKind.AUDIO_OUTPUT_START)
    assert start.data.codec == "pcm16"


def test_output_frames_reassemble_to_the_reply():
    backend, transport, orch = _wired()
    _push_input_session(backend, pcm=b"hi")
    _drain(orch)

    frames = [loads(l) for l in transport.sent_lines
              if loads(l).kind is EventKind.AUDIO_OUTPUT_FRAME]
    decoded = b"".join(
        base64.b64decode(f.data.data) for f in sorted(frames, key=lambda e: e.data.seq))
    assert decoded == b"reply: hi"


def test_announce_codec_emits_one_pcm16_codec_line():
    backend, transport, orch = _wired()
    sent = orch.announce_codec(name="pcm16", sample_rate_hz=16000)
    assert sent == 1
    ev = loads(transport.sent_lines[-1])
    assert ev.kind is EventKind.AUDIO_CODEC
    assert ev.data.name == "pcm16"
    assert ev.data.sample_rate_hz == 16000


def test_malformed_or_blank_lines_do_not_stop_the_loop():
    backend, transport, orch = _wired()
    backend.push_tx("not json {{{")
    backend.push_tx("")
    _push_input_session(backend, pcm=b"yo")
    _drain(orch)

    kinds = [loads(l).kind for l in transport.sent_lines]
    # The loop recovered from the junk and still served the real session.
    assert EventKind.AUDIO_OUTPUT_START in kinds
    assert EventKind.AUDIO_OUTPUT_STOP in kinds


def test_poll_once_is_false_when_nothing_queued():
    _backend, _transport, orch = _wired()
    assert orch.poll_once() is False


def test_forward_output_false_drains_pump_without_writing():
    backend = FakeBleBackend()
    transport = BleNusTransport(backend)
    transport.open()
    bridge = AudioBridge(pump=EventPump(), output_codec="pcm16")
    orch = ConversationOrchestrator(transport, bridge, forward_output=False)
    _push_input_session(backend, pcm=b"hi")
    _drain(orch)

    # The bridge still ran the turn (STT -> PRE -> TTS)...
    assert bridge.transcripts == ["hi"]
    assert bridge.assistant_replies == ["reply: hi"]
    # ...but nothing was written back to the device, and the pump is empty
    # (drained, not left to grow unbounded).
    assert transport.sent_lines == []
    assert len(bridge.pump) == 0
