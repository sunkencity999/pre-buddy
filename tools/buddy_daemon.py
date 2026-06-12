#!/usr/bin/env python3
"""Unified PRE Buddy daemon — the one process that makes the robot "just work".

Holds the BLE link to the robot and does everything over it, auto-reconnecting
whenever the robot powers on / comes back in range:

  - Voice loop: wake ("Hi, ESP") → mic audio → Whisper STT → PRE → spoken reply.
  - Ambient: forwards PRE's live events (router/memory/etc.) to the robot.
  - Presence: announces buddy_status to PRE so its GUI shows the manage panel.
  - Control: forwards buddy_control (GUI / agent tool) to the robot.

This supersedes running tools/converse.py and tools/live_bridge.py separately
(only one process can hold the BLE link). PRE's GUI Start/Stop button spawns
and kills this; it also runs fine standalone:

    .venv/bin/python tools/buddy_daemon.py [--device-name pre-buddy]
                                           [--pre-url ws://localhost:7749]
                                           [--out-rate 8000] [--out-frame-ms 80]

Structure: an asyncio main loop owns the persistent PRE WS (ambient + control +
presence) and the reconnect supervisor; the voice loop runs in a worker thread
(its STT/PRE/TTS calls block). Both share one BLE transport; whole-line sends
are serialized in BleNusTransport.
"""
from __future__ import annotations

import argparse
import asyncio
import json
import sys
import threading
import time

import websockets

from pre_buddy.audio_bridge import AudioBridge
from pre_buddy.bridge import map_line
from pre_buddy.buddy import device_lines
from pre_buddy.conversation import ConversationOrchestrator
from pre_buddy.pump import EventPump
from pre_buddy.serializer import dumps
from pre_buddy.transport_ble import BleakNusBackend, BleNusTransport
from pre_buddy.voice_adapters import PreWsClient, SayTts, WhisperStt


def log(msg: str) -> None:
    print(f"[buddy-daemon] {msg}", file=sys.stderr, flush=True)


def _voice_loop(transport, bridge, args, stop: threading.Event) -> None:
    """Run the wake→speak voice loop until the link drops or we're told to stop.

    Runs in its own thread because STT / PRE / TTS calls block for seconds.
    """
    orch = ConversationOrchestrator(transport, bridge, forward_output=True)
    try:
        orch.announce_codec(name="pcm16", sample_rate_hz=args.out_rate)
    except Exception as exc:  # noqa: BLE001 — link may have just dropped
        log(f"voice: codec announce failed ({type(exc).__name__})")
        return
    shown = 0
    while not stop.is_set() and transport.connected:
        try:
            did = orch.poll_once()
        except Exception as exc:  # noqa: BLE001 — never let one turn kill the loop
            log(f"voice: poll error ({type(exc).__name__}: {str(exc)[:60]})")
            break
        while shown < len(bridge.transcripts):
            heard = bridge.transcripts[shown]
            reply = (bridge.assistant_replies[shown]
                     if shown < len(bridge.assistant_replies) else "(skipped)")
            log(f"heard: {heard!r}  →  PRE: {reply[:80]!r}")
            shown += 1
        if not did:
            time.sleep(0.02)


async def _pre_ws_listener(transport, device_name: str, stop: asyncio.Event) -> None:
    """Persistent PRE WS: announce presence, forward ambient events + control.

    Reconnects to PRE on its own (PRE may restart); exits when the BLE link
    drops (so the supervisor can reconnect BLE) or we're told to stop.
    """
    while not stop.is_set() and transport.connected:
        try:
            async with websockets.connect("ws://localhost:7749", max_size=None,
                                          ping_interval=None) as ws:
                await ws.send(json.dumps({"type": "buddy_status", "connected": True,
                                          "name": device_name}))
                log("announced presence to PRE")
                async for raw in ws:
                    if stop.is_set() or not transport.connected:
                        break
                    try:
                        msg = json.loads(raw)
                    except (ValueError, TypeError):
                        msg = None
                    try:
                        if isinstance(msg, dict) and msg.get("type") == "buddy_control":
                            for line in device_lines(msg.get("settings") or {}):
                                transport.send_line(line)
                        else:
                            for ev in map_line(raw):
                                transport.send_line(dumps(ev))
                    except Exception:  # noqa: BLE001 — link hiccup; supervisor handles it
                        if not transport.connected:
                            break
        except Exception as exc:  # noqa: BLE001 — PRE down / WS error; retry shortly
            if stop.is_set() or not transport.connected:
                break
            log(f"PRE WS retry ({type(exc).__name__})")
            await asyncio.sleep(2.0)


async def _supervise(args) -> int:
    backend = BleakNusBackend(name=args.device_name)
    transport = BleNusTransport(backend, connect_timeout_s=args.connect_timeout)
    bridge = AudioBridge(
        pump=EventPump(), stt=WhisperStt(), tts=SayTts(frame_ms=args.out_frame_ms),
        pre_client=PreWsClient(ws_url=args.pre_url),
        sample_rate_hz=args.sample_rate, output_codec="pcm16",
        output_sample_rate_hz=args.out_rate,
    )
    # Warm the STT model once up front so the first real turn is instant.
    log("warming STT model…")
    bridge.stt.transcribe(b"\x00\x00" * (args.sample_rate // 2), sample_rate_hz=args.sample_rate)
    log("ready — supervising BLE connection")

    while True:
        # 1) Connect to the robot, retrying until it appears.
        try:
            transport.open()
        except Exception as exc:  # noqa: BLE001 — robot off / out of range
            log(f"robot not found ({type(exc).__name__}); retrying in 5s")
            await asyncio.sleep(5.0)
            continue
        log(f"BLE connected to '{args.device_name}'")

        # 2) Run the voice thread + PRE WS listener until the link drops.
        stop_threads = threading.Event()
        stop_async = asyncio.Event()
        vt = threading.Thread(target=_voice_loop, args=(transport, bridge, args, stop_threads),
                              name="buddy-voice", daemon=True)
        vt.start()
        listener = asyncio.create_task(_pre_ws_listener(transport, args.device_name, stop_async))
        try:
            while transport.connected:
                await asyncio.sleep(1.0)
        finally:
            stop_threads.set()
            stop_async.set()
            listener.cancel()
            try:
                await listener
            except (asyncio.CancelledError, Exception):
                pass
            vt.join(timeout=5.0)
            try:
                transport.close()
            except Exception:
                pass
        log("robot disconnected — will reconnect when it returns")
        await asyncio.sleep(2.0)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--device-name", default="pre-buddy")
    ap.add_argument("--pre-url", default="ws://localhost:7749")
    ap.add_argument("--sample-rate", type=int, default=16000)
    ap.add_argument("--out-rate", type=int, default=8000)
    ap.add_argument("--out-frame-ms", type=int, default=80)
    ap.add_argument("--connect-timeout", type=float, default=20.0)
    args = ap.parse_args()
    try:
        return asyncio.run(_supervise(args))
    except KeyboardInterrupt:
        log("stopped")
        return 0


if __name__ == "__main__":
    sys.exit(main())
