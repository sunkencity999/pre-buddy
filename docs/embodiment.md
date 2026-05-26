# Embodiment

This document is the human-readable companion to the implementation in:
- `firmware/core/include/pre_buddy/character.h`
- `firmware/core/include/pre_buddy/motion.h`
- `firmware/core/include/pre_buddy/protocol.h`

## Motion budget (v1)

Default behavior remains **still ~95% of the time**. Movement is reserved for
meaningful, low-frequency events.

| Trigger | Motion | LED |
|---|---|---|
| `pre.system.wake_word` | Turn toward dominant mic (±35° X) | Character idle color (Sprout uses yellow accent) |
| `pre.bg_agents.change` | No motion | Tier color (fast=green, standard=blue, frontier=purple) |
| `pre.router.decision` | Escalation only: subtle nod | `to_tier` color |
| `pre.confidence.warning` | Downward tilt (uncertainty) | Amber |
| `pre.system.memory_write` | Slow nod | White |
| `pre.system.proximity` | Look up | Character idle color |
| `pre.system.error` | No motion | Red |
| `pre.character.set` | Acknowledge nod | New character idle color |

## Character tuning (v1)

| Character | Reaction ms | Blink (ms) | Idle LED | Returns to center |
|---|---:|---:|---|---|
| Sage | 2000 | 6000–8000 | Blue | No |
| Sprout | 350 | 3000–5000 | Green | No |
| Sentinel | 500 | 5000 fixed | White | Yes |

## Safety + comfort

- Y axis software clamp: **10°..80°**
- X axis rate limit: **180°/s** max (implemented by stretching command duration)
- No shake behavior on errors
- Unknown events are no-op

## Next expansion candidates

- Quiet hours schedule + per-character reduced-motion mode
- Proximity-based gaze hold/release sequence (currently single look-up command)
- LED pulse envelopes (`soft`, `alert`, `error`) as protocol-level fields
