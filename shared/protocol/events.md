# PRE Buddy Wire Protocol — `pre.*` Event Namespace

> **Status:** Draft v0.1 (matches DESIGN.md v2)
> **Single source of truth** for event names + payload shapes shared between
> server (`pre_buddy` Python package) and firmware (C++ core).
>
> Both sides MUST be updated together when this file changes.

## 1. Transport

- Layer 0: BLE NUS (Anthropic-compatible UUIDs), JSON-lines framing.
- Anthropic's app ignores `pre.*` events; PRE Buddy ignores Anthropic events
  it does not understand. Forward-compat is preserved by the `pre.` prefix.

## 2. Envelope

Every event is one JSON object per line:

```json
{"event": "pre.<namespace>.<name>", "ts": 1748140800.123, "data": { ... }}
```

- `event` — required, dotted string.
- `ts` — optional, float seconds since UNIX epoch. Server emits, firmware may omit.
- `data` — required object (may be empty).

## 3. Namespaces (initial set, expanded over time)

| Namespace | Direction | Purpose |
|---|---|---|
| `pre.bg_agents.*` | server → device | background agent state |
| `pre.router.*` | server → device | model routing decisions |
| `pre.confidence.*` | server → device | confidence dial updates |
| `pre.kg.*` | server → device | knowledge graph deltas |
| `pre.training.*` | server → device | LoRA accumulator state |
| `pre.tools.*` | server → device | tool call rollups |
| `pre.character.*` | bidirectional | character selection |
| `pre.embodiment.*` | server → device | motion + LED commands (robot only) |
| `pre.servo.*` | server → device | direct servo control (robot only) |
| `pre.led.*` | server → device | direct LED control (robot only) |
| `pre.voice.*` | bidirectional | wake word, transcripts, TTS chunks |
| `pre.system.*` | bidirectional | heartbeat, error, pairing |

## 4. Events covered by initial firmware/server tests

These are the events the host-testable core in `firmware/core/` and the
Python `pre_buddy` package both understand today.

### `pre.system.wake_word`
- Direction: device → server, then echo server → device for character reaction
- Data: `{ "source_mic": "left"|"right"|"unknown" }`

### `pre.bg_agents.change`
- Direction: server → device
- Data: `{ "agent_id": str, "state": "started"|"running"|"finished"|"failed", "tier": "fast"|"standard"|"frontier" }`

### `pre.confidence.warning`
- Direction: server → device
- Data: `{ "domain": str, "confidence": float (0..1), "threshold": float }`

### `pre.system.error`
- Direction: server → device or device → server
- Data: `{ "code": str, "message": str }`

### `pre.character.set`
- Direction: bidirectional
- Data: `{ "character": "sage"|"sprout"|"sentinel" }`

### `pre.embodiment.command`
- Direction: server → device (also emitted internally by firmware in response to incoming events)
- Data: `{ "head_x_deg": float, "head_y_deg": float, "led_color": "blue"|"green"|"amber"|"red"|"white"|"off", "duration_ms": int, "ease": "linear"|"in_out" }`

## 5. Safety invariants

- `head_y_deg` MUST be clamped to `[10.0, 80.0]` before reaching the servo.
- `head_x_deg` is unbounded modulo 360, but rate-limited: no more than
  `MAX_DEG_PER_SEC` (default 180°/s) change between consecutive commands.
- LED red is reserved for `error` and confidence-below-threshold warnings
  (amber for warning, red for error).
