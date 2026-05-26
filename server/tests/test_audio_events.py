"""Tests for the pre.audio.* protocol events.

Covers: typed payload validation + JSON envelope round-trip via the
existing serializer.
"""

from __future__ import annotations

import base64
import json

import pytest

from pre_buddy.events import (
    AudioCodecData,
    AudioErrorData,
    AudioInputFrameData,
    AudioInputStartData,
    AudioInputStopData,
    AudioOutputFrameData,
    AudioOutputStartData,
    AudioOutputStopData,
    AudioWakeWordDetectedData,
    Event,
    EventKind,
)
from pre_buddy.serializer import dumps, loads


def _round_trip(ev: Event) -> Event:
    return loads(dumps(ev))


# ── wake word detected ────────────────────────────────────────────────


def test_wake_word_detected_round_trip():
    ev = Event(
        EventKind.AUDIO_WAKE_WORD_DETECTED,
        AudioWakeWordDetectedData(phrase="hey buddy", confidence=0.93),
    )
    out = _round_trip(ev)
    assert out.kind is EventKind.AUDIO_WAKE_WORD_DETECTED
    assert out.data.phrase == "hey buddy"
    assert out.data.confidence == pytest.approx(0.93)


def test_wake_word_detected_rejects_empty_phrase():
    with pytest.raises(ValueError):
        AudioWakeWordDetectedData(phrase="")


def test_wake_word_detected_rejects_out_of_range_confidence():
    with pytest.raises(ValueError):
        AudioWakeWordDetectedData(phrase="x", confidence=1.4)


# ── audio session lifecycle ───────────────────────────────────────────


def test_input_start_defaults_to_opus_16khz():
    d = AudioInputStartData(session_id="s1")
    assert d.codec == "opus"
    assert d.sample_rate_hz == 16000


def test_input_start_rejects_unknown_codec():
    with pytest.raises(ValueError):
        AudioInputStartData(session_id="s1", codec="mp3")


def test_input_start_rejects_zero_sample_rate():
    with pytest.raises(ValueError):
        AudioInputStartData(session_id="s1", sample_rate_hz=0)


def test_input_stop_validates_reason():
    AudioInputStopData(session_id="s1", reason="vad_silence")
    AudioInputStopData(session_id="s1", reason="timeout")
    AudioInputStopData(session_id="s1", reason="manual")
    with pytest.raises(ValueError):
        AudioInputStopData(session_id="s1", reason="user_yelled")


def test_output_stop_validates_reason():
    AudioOutputStopData(session_id="s1", reason="complete")
    AudioOutputStopData(session_id="s1", reason="cancelled")
    with pytest.raises(ValueError):
        AudioOutputStopData(session_id="s1", reason="???")


# ── frames carry base64 payloads in JSON envelopes ────────────────────


def test_input_frame_round_trip_preserves_base64_payload():
    raw_audio = bytes(range(64))   # arbitrary "pcm" bytes
    payload = base64.b64encode(raw_audio).decode("ascii")
    ev = Event(
        EventKind.AUDIO_INPUT_FRAME,
        AudioInputFrameData(session_id="s1", seq=0, data=payload),
    )
    out = _round_trip(ev)
    assert out.data.session_id == "s1"
    assert out.data.seq == 0
    assert base64.b64decode(out.data.data) == raw_audio


def test_output_frame_round_trip_preserves_base64_payload():
    raw_audio = b"\xff\xee\xdd\xcc"
    payload = base64.b64encode(raw_audio).decode("ascii")
    ev = Event(
        EventKind.AUDIO_OUTPUT_FRAME,
        AudioOutputFrameData(session_id="s2", seq=42, data=payload),
    )
    out = _round_trip(ev)
    assert out.data.seq == 42
    assert base64.b64decode(out.data.data) == raw_audio


def test_input_frame_rejects_negative_seq():
    with pytest.raises(ValueError):
        AudioInputFrameData(session_id="s1", seq=-1, data="AA==")


def test_input_frame_rejects_empty_session():
    with pytest.raises(ValueError):
        AudioInputFrameData(session_id="", seq=0, data="AA==")


# ── codec + error events ─────────────────────────────────────────────


def test_codec_round_trip_with_defaults():
    ev = Event(EventKind.AUDIO_CODEC, AudioCodecData())
    out = _round_trip(ev)
    assert out.data.name == "opus"
    assert out.data.sample_rate_hz == 16000
    assert out.data.bitrate_bps == 32000


def test_codec_rejects_unknown_name():
    with pytest.raises(ValueError):
        AudioCodecData(name="mp3")


def test_audio_error_round_trip():
    ev = Event(
        EventKind.AUDIO_ERROR,
        AudioErrorData(code="E_OPUS_INIT", message="encoder out of memory"),
    )
    out = _round_trip(ev)
    assert out.data.code == "E_OPUS_INIT"
    assert "out of memory" in out.data.message


def test_audio_error_rejects_empty_code():
    with pytest.raises(ValueError):
        AudioErrorData(code="")


# ── catalog completeness ──────────────────────────────────────────────


def test_every_audio_event_kind_has_a_hydrator():
    # If somebody adds a pre.audio.* EventKind but forgets the typed
    # payload class, the round-trip would fall back to a dict and break
    # downstream consumers silently. Catch that here.
    from pre_buddy.events import _HYDRATORS

    audio_kinds = [k for k in EventKind if k.value.startswith("pre.audio.")]
    assert audio_kinds, "no audio event kinds — did the protocol revert?"
    for kind in audio_kinds:
        assert kind in _HYDRATORS, f"no typed payload for {kind.value}"
