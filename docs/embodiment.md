# Embodiment

> 🚧 Placeholder. Will hold the fleshed-out motion budget, idle choreography,
> and per-character behavior tables. See DESIGN.md §4 and §5.

## What lives here (eventually)

- The full motion-budget table per character (idle, reaction, recovery).
- LED palette swatches per character with hex codes.
- Sprite face state diagrams (idle / blink / surprise / focus / error).
- Stall detection + "still mode" degrade flow.
- Quiet hours user-config schema.

## What lives in code today

- Safety clamps + rate limiter: `firmware/core/include/pre_buddy/motion.h`
- Character profiles (timing, blink, LED palette): `firmware/core/include/pre_buddy/character.h`
- Event → embodiment mapping: `firmware/core/include/pre_buddy/protocol.h`
- Host tests for all three: `firmware/test/`
