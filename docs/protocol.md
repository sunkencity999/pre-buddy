# Protocol

Canonical spec: [`../shared/protocol/events.md`](../shared/protocol/events.md).

This page explains implementation intent and where to edit code when adding an
event.

## Where the contract is enforced

- **Python server model:** `server/pre_buddy/events.py`
- **Firmware parser + mapping:** `firmware/core/include/pre_buddy/protocol.h`
- **Server tests:** `server/tests/test_events.py`
- **Firmware tests:** `firmware/test/test_protocol.cpp`

When introducing a new event:
1. Update `shared/protocol/events.md`
2. Add/update typed payload in Python
3. Add/update C++ event parsing + embodiment mapping
4. Add tests on both sides in the same commit

## v1 implemented events

- `pre.system.wake_word`
- `pre.bg_agents.change`
- `pre.router.decision`
- `pre.confidence.warning`
- `pre.confidence.snapshot`
- `pre.kg.delta`
- `pre.training.progress`
- `pre.scheduler.upcoming`
- `pre.tools.rollup`
- `pre.system.memory_write`
- `pre.system.proximity`
- `pre.system.error`
- `pre.character.set`

## Compatibility expectations

- Unknown events are ignored safely.
- `data` payloads should tolerate additive fields.
- `pre.*` namespace is owned by PRE Buddy; all non-`pre.*` behavior remains
  Anthropic-compatible BLE/NUS framing.
