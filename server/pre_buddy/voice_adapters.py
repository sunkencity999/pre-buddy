"""Real STT / TTS / PRE adapters for the AudioBridge (macOS host).

These plug into AudioBridge's IStt / ITts / IPreClient slots, replacing the
echo stubs:

  - WhisperStt   : openai-whisper CLI (the same engine PRE's voice tool uses)
  - SayTts       : macOS `say` → PCM16 mono
  - PreWsClient  : ask PRE over its WebSocket, collect the streamed reply

All are synchronous to match the bridge's protocol; PreWsClient uses the
websockets sync client internally.
"""
from __future__ import annotations

import json
import os
import subprocess
import tempfile
import time
import wave
from typing import Iterable, Optional


class WhisperStt:
    """openai-whisper CLI STT (matches PRE: model base.en, txt output)."""

    def __init__(self, model: str = "base.en", language: str = "en",
                 whisper_cmd: str = "whisper", timeout_s: float = 120.0) -> None:
        self.model = model
        self.language = language
        self.cmd = whisper_cmd
        self.timeout_s = timeout_s

    def transcribe(self, pcm16_mono: bytes, *, sample_rate_hz: int) -> str:
        if not pcm16_mono:
            return ""
        with tempfile.TemporaryDirectory() as d:
            wav = os.path.join(d, "in.wav")
            with wave.open(wav, "wb") as w:
                w.setnchannels(1)
                w.setsampwidth(2)
                w.setframerate(sample_rate_hz)
                w.writeframes(pcm16_mono)
            try:
                subprocess.run(
                    [self.cmd, wav, "--model", self.model, "--language", self.language,
                     "--output_format", "txt", "--output_dir", d, "--fp16", "False"],
                    check=True, capture_output=True, timeout=self.timeout_s)
            except (subprocess.CalledProcessError, subprocess.TimeoutExpired, FileNotFoundError):
                return ""
            txt = os.path.join(d, "in.txt")
            if not os.path.exists(txt):
                return ""
            with open(txt, encoding="utf-8") as f:
                return f.read().strip()


class SayTts:
    """macOS `say` → PCM16 mono at the requested sample rate, yielded in frames."""

    def __init__(self, voice: Optional[str] = None, frame_ms: int = 20) -> None:
        self.voice = voice
        self.frame_ms = frame_ms

    def synthesize(self, text: str, *, sample_rate_hz: int) -> Iterable[bytes]:
        if not text.strip():
            return
        with tempfile.TemporaryDirectory() as d:
            wav = os.path.join(d, "out.wav")
            cmd = ["say", "--file-format=WAVE",
                   f"--data-format=LEI16@{sample_rate_hz}", "-o", wav]
            if self.voice:
                cmd += ["-v", self.voice]
            cmd += [text]
            subprocess.run(cmd, check=True, capture_output=True, timeout=60)
            with wave.open(wav, "rb") as w:
                pcm = w.readframes(w.getnframes())
        frame_bytes = (sample_rate_hz * self.frame_ms // 1000) * 2  # mono 16-bit
        if frame_bytes <= 0:
            frame_bytes = 640
        for i in range(0, len(pcm), frame_bytes):
            yield pcm[i:i + frame_bytes]


class PreWsClient:
    """Ask PRE over its WebSocket and return the assistant reply text.

    Sends {"type":"message","content":...}; concatenates {"type":"token",
    "content":...} events until {"type":"done"}.
    """

    def __init__(self, ws_url: str = "ws://localhost:7749", timeout_s: float = 180.0) -> None:
        self.ws_url = ws_url
        self.timeout_s = timeout_s

    def ask(self, user_text: str) -> str:
        if not user_text.strip():
            return ""
        from websockets.sync.client import connect

        parts: list[str] = []
        with connect(self.ws_url, max_size=None, open_timeout=10) as ws:
            ws.send(json.dumps({"type": "message", "content": user_text}))
            deadline = time.monotonic() + self.timeout_s
            while time.monotonic() < deadline:
                try:
                    raw = ws.recv(timeout=self.timeout_s)
                except TimeoutError:
                    break
                try:
                    m = json.loads(raw)
                except (ValueError, TypeError):
                    continue
                t = m.get("type")
                if t == "token" and isinstance(m.get("content"), str):
                    parts.append(m["content"])
                elif t == "done":
                    break
        return "".join(parts).strip()
