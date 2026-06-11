#!/usr/bin/env python3
"""Live bridge: forward PRE WebSocket events to the PRE Buddy robot over BLE.

The missing glue between `pre-buddy bridge` (PRE WS -> pre.* events) and
`pre-buddy serve` (pre.* events -> BLE): connects to the robot over BLE NUS
*and* to PRE's WebSocket, maps each PRE event to a pre.* protocol event, and
writes it to the robot's RX characteristic in real time.

Usage (from the repo root, with the venv):
    .venv/bin/python tools/live_bridge.py [--pre-url ws://localhost:7749]
                                          [--device-name pre-buddy]
"""
from __future__ import annotations

import argparse
import asyncio
import json
import sys

import websockets

from pre_buddy.bridge import map_line
from pre_buddy.buddy import device_lines
from pre_buddy.serializer import dumps
from pre_buddy.transport_ble import BleakNusBackend, BleNusTransport


async def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--pre-url", default="ws://localhost:7749")
    ap.add_argument("--device-name", default="pre-buddy")
    ap.add_argument("--connect-timeout", type=float, default=20.0)
    args = ap.parse_args()

    backend = BleakNusBackend(name=args.device_name)
    transport = BleNusTransport(backend, connect_timeout_s=args.connect_timeout)
    print(f"[live-bridge] connecting to robot '{args.device_name}' over BLE...", file=sys.stderr, flush=True)
    transport.open()
    print(f"[live-bridge] BLE connected={transport.connected}", file=sys.stderr, flush=True)

    print(f"[live-bridge] connecting to PRE at {args.pre_url} ...", file=sys.stderr, flush=True)
    async with websockets.connect(args.pre_url, max_size=None) as ws:
        print("[live-bridge] PRE WS connected; forwarding live events", file=sys.stderr, flush=True)
        # Tell PRE a robot is connected so its GUI can show the manage-bot panel.
        # PRE relays this to browser clients; it marks the bot gone when this WS
        # closes (i.e. when this process exits).
        await ws.send(json.dumps({"type": "buddy_status", "connected": True,
                                  "name": args.device_name}))
        async for raw in ws:
            # Settings from PRE's GUI / the buddy_control agent tool, broadcast
            # to all WS clients (including us). Translate to device line(s).
            try:
                msg = json.loads(raw)
            except (ValueError, TypeError):
                msg = None
            if isinstance(msg, dict) and msg.get("type") == "buddy_control":
                for line in device_lines(msg.get("settings") or {}):
                    transport.send_line(line)
                    print(f"[live-bridge] -> robot (control): {line}", flush=True)
                continue
            # Otherwise map PRE's live events onto embodiment events.
            for ev in map_line(raw):
                line = dumps(ev)
                transport.send_line(line)
                print(f"[live-bridge] -> robot: {line}", flush=True)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(asyncio.run(main()))
    except KeyboardInterrupt:
        print("\n[live-bridge] stopped", file=sys.stderr)
