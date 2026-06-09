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
import sys

import websockets

from pre_buddy.bridge import map_line
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
        async for raw in ws:
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
