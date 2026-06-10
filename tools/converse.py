#!/usr/bin/env python3
"""Conversational loop: talk to PRE Buddy out loud over BLE.

Connects to the robot over BLE NUS, announces the PCM16 wire codec, then
runs the device's microphone audio through Whisper → PRE → macOS ``say``
and streams the spoken reply back for the device to play. This is the host
half of "say 'Hi, ESP' → speak → PRE answers out loud"; the device firmware
provides the mic-capture and speaker-playback halves.

Usage (from the repo root, with the venv):
    .venv/bin/python tools/converse.py [--device-name pre-buddy]
                                       [--pre-url ws://localhost:7749]
                                       [--sample-rate 16000]
"""
from __future__ import annotations

import argparse
import sys

from pre_buddy.audio_bridge import AudioBridge
from pre_buddy.conversation import ConversationOrchestrator
from pre_buddy.pump import EventPump
from pre_buddy.transport_ble import BleakNusBackend, BleNusTransport
from pre_buddy.voice_adapters import PreWsClient, SayTts, WhisperStt


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--device-name", default="pre-buddy")
    ap.add_argument("--pre-url", default="ws://localhost:7749")
    ap.add_argument("--sample-rate", type=int, default=16000)
    ap.add_argument("--connect-timeout", type=float, default=20.0)
    args = ap.parse_args()

    backend = BleakNusBackend(name=args.device_name)
    transport = BleNusTransport(backend, connect_timeout_s=args.connect_timeout)
    print(f"[converse] connecting to robot '{args.device_name}' over BLE...",
          file=sys.stderr, flush=True)
    transport.open()
    print(f"[converse] BLE connected={transport.connected}", file=sys.stderr, flush=True)

    bridge = AudioBridge(
        pump=EventPump(),
        stt=WhisperStt(),
        tts=SayTts(),
        pre_client=PreWsClient(ws_url=args.pre_url),
        sample_rate_hz=args.sample_rate,
        output_codec="pcm16",
    )

    def log(direction: str, line: str) -> None:
        tag = {"in": "<- dev", "out": "-> dev", "err": "!! err"}.get(direction, direction)
        # Audio frames are huge; trim so the console stays readable.
        print(f"[converse] {tag}: {line[:120]}", flush=True)

    orch = ConversationOrchestrator(transport, bridge, on_line=log)
    orch.announce_codec(name="pcm16", sample_rate_hz=args.sample_rate)
    print("[converse] codec announced; say 'Hi, ESP' to the robot. Ctrl-C to stop.",
          file=sys.stderr, flush=True)

    try:
        orch.run()
    finally:
        transport.close()
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print("\n[converse] stopped", file=sys.stderr)
