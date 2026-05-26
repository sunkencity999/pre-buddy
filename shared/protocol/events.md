# PRE Buddy Wire Protocol — `pre.*` Event Namespace

> **Status:** Draft v0.2 (robot-primary, v1 event surface)
> **Single source of truth** for event names + payload shapes shared between
> server (`pre_buddy` Python package) and firmware core (C++).

Both sides MUST be updated together when this file changes.

## 1. Transport

- Layer 0: BLE NUS (Anthropic-compatible UUIDs), JSON-lines framing.
- Anthropic app ignores `pre.*`; PRE Buddy ignores unknown non-`pre.*` events.
- One JSON object per line.

## 2. Envelope

```json
{"event": "pre.<namespace>.<name>", "ts": 1748140800.123, "data": {}}
```

- `event` — required dotted string
- `ts` — optional float (unix seconds)
- `data` — required object (may be `{}`)

## 3. v1 event catalog (implemented now)

### System + character

| Event | Direction | Payload |
|---|---|---|
| `pre.system.wake_word` | device→server (+echo back for reaction) | `{ "source_mic": "left"\|"right"\|"unknown" }` |
| `pre.system.memory_write` | server→device | `{ "key": string, "source": string }` |
| `pre.system.proximity` | device→server | `{ "distance_cm": number >= 0 }` |
| `pre.system.error` | bidirectional | `{ "code": string, "message": string }` |
| `pre.character.set` | bidirectional | `{ "character": "sage"\|"sprout"\|"sentinel" }` |

### Introspection panels

| Event | Direction | Payload |
|---|---|---|
| `pre.bg_agents.change` | server→device | `{ "agent_id": string, "state": "started"\|"running"\|"finished"\|"failed", "tier": "fast"\|"standard"\|"frontier" }` |
| `pre.router.decision` | server→device | `{ "from_tier": "fast"\|"standard"\|"frontier", "to_tier": "fast"\|"standard"\|"frontier", "reason": string }` |
| `pre.confidence.warning` | server→device | `{ "domain": string, "confidence": number [0,1], "threshold": number [0,1] }` |
| `pre.confidence.snapshot` | server→device | `{ "weakest_domain": string, "confidence": number [0,1] }` |
| `pre.kg.delta` | server→device | `{ "entities_added": int >= 0, "relations_added": int >= 0 }` |
| `pre.training.progress` | server→device | `{ "examples_total": int >= 0, "goal_examples": int >= 0 }` |
| `pre.scheduler.upcoming` | server→device | `{ "event_name": string, "minutes_until": int >= 0 }` |
| `pre.tools.rollup` | server→device | `{ "tool": string, "calls": int >= 0, "success_rate": number [0,1] }` |

### Voice / audio

Audio frames travel as base64 strings inside the standard JSON envelope —
no separate binary subchannel — so the existing LineFramer + bridge code
handle everything. Codec defaults to Opus at 16 kHz mono, 32 kbps (≈ 80
bytes per 20 ms frame, well inside the ATT MTU).

| Event | Direction | Payload |
|---|---|---|
| `pre.audio.wake_word_detected` | device→server | `{ "phrase": string, "confidence": number [0,1] }` |
| `pre.audio.input_start` | device→server | `{ "session_id": string, "sample_rate_hz": int, "codec": "opus"\|"pcm16" }` |
| `pre.audio.input_frame` | device→server | `{ "session_id": string, "seq": int >= 0, "data": string (base64) }` |
| `pre.audio.input_stop` | device→server | `{ "session_id": string, "reason": "vad_silence"\|"timeout"\|"manual" }` |
| `pre.audio.output_start` | server→device | `{ "session_id": string, "sample_rate_hz": int, "codec": "opus"\|"pcm16" }` |
| `pre.audio.output_frame` | server→device | `{ "session_id": string, "seq": int >= 0, "data": string (base64) }` |
| `pre.audio.output_stop` | server→device | `{ "session_id": string, "reason": "complete"\|"cancelled" }` |
| `pre.audio.codec` | server→device | `{ "name": "opus"\|"pcm16", "sample_rate_hz": int, "bitrate_bps": int }` — set on connect, before any frames |
| `pre.audio.error` | bidirectional | `{ "code": string, "message": string }` — codec init failure, OOM, etc. |

## 4. Embodiment mapping (firmware core)

Mapping lives in `firmware/core/include/pre_buddy/protocol.h`.

- `wake_word` → head turn toward dominant mic (+ sprout yellow accent)
- `bg_agents.change` → LED-only by tier (fast=green, standard=blue, frontier=purple)
- `router.decision` → LED by `to_tier`; only escalation causes subtle nod
- `confidence.warning` → head tilt + amber
- `memory_write` → slow nod + white
- `proximity` → look up
- `error` → red, no motion
- `character.set` → acknowledge nod in selected character idle color

All other v1 panel updates are LED-only (low-distraction ambient mode).

## 5. Safety invariants

- `head_y_deg` MUST be clamped to `[10.0, 80.0]` before servo output.
- X-axis commands are rate-limited to `MAX_DEG_PER_SEC` (default 180°/s).
- Error state reserves `red`; warning uses `amber`.

## 6. Compatibility rule

- Unknown `event` names MUST be ignored safely.
- Additional fields in `data` MUST be tolerated (forward-compat).
