#!/usr/bin/env python3
"""Host test for the conversational-loop 'brain' — no device/BLE needed.

Synthesizes a spoken question with macOS `say`, runs it through the real
Whisper STT -> PRE -> `say` TTS adapters, then exercises the full AudioBridge
the same way the device will (input_start/frame/stop -> output_* frames).

Run from the repo root:  .venv/bin/python tools/test_brain.py
"""
from __future__ import annotations

import base64
import sys
from collections import Counter

from pre_buddy.audio_bridge import AudioBridge
from pre_buddy.events import (
    AudioInputFrameData,
    AudioInputStartData,
    AudioInputStopData,
    Event,
    EventKind,
)
from pre_buddy.pump import EventPump
from pre_buddy.voice_adapters import PreWsClient, SayTts, WhisperStt

SR = 16000
QUESTION = "What is the capital of France? Answer in one short sentence."


def main() -> int:
    say = SayTts()

    print("[1] synthesize the question with `say` (stands in for the mic)...")
    qpcm = b"".join(say.synthesize(QUESTION, sample_rate_hz=SR))
    print(f"    {len(qpcm)} bytes (~{len(qpcm) / 2 / SR:.1f}s of PCM16)")
    if not qpcm:
        print("    FAIL: say produced no audio"); return 1

    print("[2] Whisper STT (this can take a few seconds on CPU)...")
    transcript = WhisperStt().transcribe(qpcm, sample_rate_hz=SR)
    print(f"    transcript: {transcript!r}")
    if not transcript:
        print("    FAIL: empty transcript"); return 1

    print("[3] ask PRE over its WebSocket...")
    reply = PreWsClient().ask(transcript)
    print(f"    PRE reply: {reply!r}")
    if not reply:
        print("    FAIL: empty PRE reply (is PRE running on :7749?)"); return 1

    print("[4] TTS the reply with `say`...")
    rpcm = b"".join(say.synthesize(reply, sample_rate_hz=SR))
    print(f"    {len(rpcm)} bytes (~{len(rpcm) / 2 / SR:.1f}s of reply audio)")

    print("\n[bridge] full AudioBridge loop with the real adapters...")
    pump = EventPump()
    bridge = AudioBridge(pump=pump, stt=WhisperStt(), tts=SayTts(),
                         pre_client=PreWsClient(), sample_rate_hz=SR)
    sid = "brain-test"
    bridge.handle_event(Event(EventKind.AUDIO_INPUT_START,
                              AudioInputStartData(session_id=sid, sample_rate_hz=SR, codec="pcm16")))
    frame = SR // 50 * 2  # 20ms mono16
    for i in range(0, len(qpcm), frame):
        bridge.handle_event(Event(EventKind.AUDIO_INPUT_FRAME, AudioInputFrameData(
            session_id=sid, seq=i // frame,
            data=base64.b64encode(qpcm[i:i + frame]).decode("ascii"))))
    bridge.handle_event(Event(EventKind.AUDIO_INPUT_STOP,
                              AudioInputStopData(session_id=sid, reason="manual")))

    kinds: list[str] = []
    while True:
        ev = pump.pop_next()
        if ev is None:
            break
        kinds.append(getattr(ev.kind, "name", str(ev.kind)))
    print(f"    bridge transcript: {bridge.transcripts}")
    print(f"    bridge reply:      {bridge.assistant_replies}")
    print(f"    output events:     {dict(Counter(kinds))}")

    ok = bool(bridge.transcripts and bridge.assistant_replies
              and kinds.count("AUDIO_OUTPUT_FRAME") > 0)
    print("\nRESULT:", "PASS — full brain loop works end to end" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
