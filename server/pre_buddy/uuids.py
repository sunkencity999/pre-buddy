"""Nordic UART Service UUIDs used for the BLE/NUS transport.

Single source of truth on the Python side. The firmware mirror lives at
``firmware/core/include/pre_buddy/hal/uuids.h`` and the human-readable
spec is ``shared/protocol/uuids.md``.
"""

from __future__ import annotations

NUS_SERVICE_UUID: str = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
"""Nordic UART Service (the device advertises this)."""

NUS_RX_CHAR_UUID: str = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
"""Central → peripheral. The server writes JSON lines here."""

NUS_TX_CHAR_UUID: str = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"
"""Peripheral → central. The server subscribes to notifications on this."""

__all__ = ["NUS_SERVICE_UUID", "NUS_RX_CHAR_UUID", "NUS_TX_CHAR_UUID"]
