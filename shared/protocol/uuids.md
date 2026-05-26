# Nordic UART Service (NUS) UUIDs

PRE Buddy reuses the standard Nordic UART Service for BLE framing. This
matches the Anthropic-compatible buddy convention described in
[`events.md`](./events.md). All three UUIDs are 128-bit randoms from the
Nordic vendor block — do not change them, or off-the-shelf NUS clients
(nRF Connect, etc.) won't recognise the device during bring-up.

| Role | UUID | Direction | Properties |
|---|---|---|---|
| Service | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` | — | — |
| RX characteristic | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` | central → peripheral | write, write-without-response |
| TX characteristic | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` | peripheral → central | notify |

PRE Buddy framing on top of NUS:

- The Python server is the BLE **central**: it scans, connects, writes
  JSON lines to RX, and subscribes to TX notifications.
- The ESP32 robot is the BLE **peripheral**: it advertises the service
  and pushes `pre.*` events to the central via TX notifications.
- One BLE write / notification = one JSON line (no MTU stitching in v1;
  see `BleNusTransport` docstring for the MTU constraint).

These UUIDs are mirrored in code:

- Python: `server/pre_buddy/uuids.py`
- C++:    `firmware/core/include/pre_buddy/hal/uuids.h`

Keep both in sync with this document.
