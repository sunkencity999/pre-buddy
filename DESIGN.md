# PRE Buddy — Design Document

> **Status:** v2 (robot-primary). Supersedes v1 (CoreS3-only).
> **Owner:** Christopher Bradford
> **Last updated:** 2026-05-25

---

## 1. One-Line Pitch

> **PRE Buddy is the embodied avatar of your local PRE agent — a small desktop robot that watches over your work, surfaces what PRE knows about itself, and turns a personal language model into something that lives on your desk.**

Cloud assistants live in datacenters. Yours lives on your desk.

---

## 2. Product Thesis

PRE's competitive edge isn't raw capability — it's **introspection + ownership**. PRE knows things about itself that hosted assistants can't show: which background agents are running, which model tier handled the last turn, how confident it is per-domain, what it's learning, what's in its knowledge graph.

A flat screen could *display* that. An embodied avatar makes people *bond with it*. That bond drives daily use, which drives data, which drives PRE's personalization flywheel.

**Embodiment is the lever. Introspection is the payload.**

---

## 3. Hardware

### Primary (flagship): M5Stack StackChan Remote Kit (SKU K151-R)

- **Brain:** CoreS3 (ESP32-S3, 240 MHz dual-core, 16 MB Flash, 8 MB PSRAM, Wi-Fi + BLE 5)
- **Display:** 2.0" IPS 320×240, capacitive multi-touch
- **Camera:** GC0308, 0.3 MP — feeds Gemma 4 native multimodal
- **Audio:** dual mics (ES7210) + 1W speaker (AW88298)
- **Motion:** X-axis 360° continuous servo + Y-axis 90° servo (both with feedback)
- **Light:** 12× WS2812C RGB LEDs (two rows)
- **Other:** 9-axis IMU, NFC (ST25R3916), IR TX/RX, 3-zone touch panel, microSD, 550 mAh battery
- **Remote:** paired StickC-Plus + Hat Mini JoyC (included)

### Secondary (headless / dev): bare CoreS3

Same firmware, no body. For developers, kiosk installs, and price-sensitive users. Embodiment layer compiles out cleanly.

### Why this hardware
- Same CoreS3 SoC across both builds → **one firmware, two enclosures**
- Servos + LEDs give us a real motion/light vocabulary for ambient signaling
- Camera + dual mics + speaker = voice and vision without external hardware
- NFC enables "tap your phone to push context" later
- Battery means it's a desk object, not a tethered widget

---

## 4. The Embodiment Layer (the spine)

This is **S2 of the build plan and the most important system**. If the body doesn't feel alive in the first 10 seconds, nothing else matters.

### 4.1 Motion budget

Default policy: **still ~95% of the time.** Movement is reserved for meaningful events. A robot that twitches every heartbeat gets unplugged within a week.

| Trigger | Motion |
|---|---|
| Idle (no event) | gaze drift (X ±15°, slow), occasional blink, sub-degree breathing wobble |
| Wake-word detected | head turns toward dominant mic, sprite eyes widen |
| Background agent state change | brief LED ring pulse on tier color, no head motion |
| Model tier escalation (fast→frontier) | LED ring shifts color, slow nod |
| Memory write | single slow nod + soft LED flash |
| Confidence-warning event | head tilt + amber LED |
| Error | LED red, no motion (we don't shake) |
| User approaches (proximity sensor) | look up, gaze tracking for 3s, then back to idle |

### 4.2 Safety
- **Y-axis servo software-clamped to 10–80°** (spec recommends 5–85°; we leave margin)
- **X-axis unrestricted** but with rate limit (no sudden 180° spins)
- **Stall detection** → graceful degrade to "still mode," LED indication, alert via BLE event
- **Power-on pose:** centered, eyes closed → 2s wake animation → idle. Never starts in a weird position.

### 4.3 Uncanny valley discipline
- We stay in **stylized creature** territory, not "tries to be a human face"
- Sprite face uses 2 eyes + optional mouth indicator. No eyebrows-as-emotion, no realistic features.
- Blink rate: 4–8 seconds (human range), randomized
- Motion easing: always ease-in-out, never linear

---

## 5. Character System

Three pickable personalities. Same hardware, same panels, different *behavior*. The character IS how the user experiences PRE.

### 5.1 The three characters

#### **Sage** — *calm, deliberate, wise*
- **Tagline:** "Thinks before it moves."
- **Motion:** slow, smooth easing (1.5–2.5s transitions). Long idle stillness. Gaze drift is rare and minimal.
- **LED palette:** deep blues, soft purples, muted whites
- **Blink rate:** slow (6–8s)
- **Reaction style:** when something happens, *pauses* before reacting — a beat of consideration
- **Sprite face:** half-lidded eyes, gentle curve
- **Voice profile** (when TTS arrives): low, unhurried
- **Best for:** users who do focused deep work, want a quiet partner

#### **Sprout** — *curious, eager, alive*
- **Tagline:** "Always paying attention."
- **Motion:** quick small movements, frequent gaze drift (every 8–15s), tilts head on confusion or new events
- **LED palette:** warm greens, yellows, occasional pinks
- **Blink rate:** fast (3–5s), sometimes double-blinks
- **Reaction style:** immediate, *leans into* events — head jerks toward action
- **Sprite face:** round wide eyes, occasional surprise/curiosity expressions
- **Voice profile:** higher pitch, expressive
- **Best for:** users who want energy, casual users, anyone who likes pets

#### **Sentinel** — *alert, professional, minimal*
- **Tagline:** "On duty."
- **Motion:** crisp, controlled, military-style. No drift. Returns to center after every reaction.
- **LED palette:** white, cyan, amber for warnings, red for errors. No decorative color.
- **Blink rate:** moderate (5s) and *regular* (not randomized)
- **Reaction style:** precise — turns to look, holds, returns to center
- **Sprite face:** narrow focused eyes, minimal expression
- **Voice profile:** neutral, clear
- **Best for:** ops/admin/security users, people who want signal not noise

### 5.2 Character selection
- Set at first boot via paired remote + screen prompt
- Changeable any time via `pre character` CLI or in-screen menu
- Stored in CoreS3 NVS (survives reboots)
- Server-side `pre.character.set` event broadcast so logs/UI can match

### 5.3 What characters do *not* change
- The information surfaced (all 7 panels work identically)
- Wake word, voice handling, command routing
- Safety/motion limits

Character is *personality*, not capability.

---

## 6. The 7 Introspection Panels

All panels work on both robot and headless builds. Source: features PRE actually exposes that Claude Desktop / hosted buddies can't.

1. **Background Agents** *(hero panel)* — fire-and-forget agents with elapsed time, tool counts, last action, tap-to-steer (summary/checkpoint/pause/kill)
2. **Multi-model router** — which tier handled the last turn and why; today's fast/standard/frontier mix with rough latency savings
3. **Confidence dial** — calibrated self-knowledge, risk-first (weakest at top, warn-flagged below threshold)
4. **Knowledge graph** — entities + relationships + today's growth; tap recent entity for 1-hop neighbors
5. **Training data accumulator** — exportable examples toward next LoRA milestone (ChatML/Alpaca/ShareGPT/DPO)
6. **Scheduler preview** — upcoming cron + event triggers
7. **Tools roll-up** — top tools today by call count with success rates

---

## 7. Wire Protocol

**Anthropic-compat NUS + `pre.*` extensions.** Same UUIDs, same JSON-line format, same heartbeat/permission/folder-push primitives. PRE's introspection events are namespaced under `pre.*`. Anthropic's app ignores them; ours acts on them.

**One device, two ecosystems, no fork.**

### Namespaces
- `pre.bg_agents.*` — background agent state
- `pre.router.*` — model routing events
- `pre.confidence.*` — confidence dial updates
- `pre.kg.*` — knowledge graph deltas
- `pre.training.*` — training data accumulator
- `pre.tools.*` — tool call rollups
- `pre.character.*` — character selection events
- `pre.embodiment.*` — motion/LED commands (robot-only; headless ignores)
- `pre.servo.*` — direct servo control (robot-only)
- `pre.led.*` — direct LED control (robot-only)

Headless builds simply ignore `pre.embodiment.*` / `pre.servo.*` / `pre.led.*`. No branching firmware logic — same code, hardware-feature-detected.

---

## 8. Server Side

`pre buddy serve` — subcommand under the existing `pre` CLI (not a separate daemon).
- BLE peripheral advertises NUS service
- Pairing via passkey-on-device (mirrors Anthropic's flow exactly)
- Pairing UX in PRE's web GUI to feel familiar
- Emits `pre.*` events from PRE's introspection bus
- Receives voice transcripts and routes by intent (`task_dispatch`, `memory_note`, `agent_steer`)

### TTS
- **Server-side via PRE's voice interface** (higher quality, no firmware burden, characters can have distinct voices)
- Streams compressed audio over BLE to device speaker

---

## 9. Build Plan

| Stage | Scope | Est. hours |
|---|---|---|
| **S0** | CoreS3 bring-up, BLE NUS peripheral, paired remote control | 3 |
| **S1** | Landscape UI shell, character select first-boot flow, NVS persistence | 3 |
| **S2** | **Embodiment layer** — idle choreography for all 3 characters, LED system, motion budget, safety clamps | 6 |
| **S3** | Background agents panel (hero) + tap-to-steer | 4 |
| **S4** | Router / confidence / kg / training / scheduler / tools panels | 5 |
| **S5** | Voice — wake word, push-to-talk, whisper transcription, intent routing | 4 |
| **S6** | Camera — Gemma 4 multimodal vision flow | 2 |
| **S7** | Polish — animations, TTS, error states, pairing UX, docs | 3 |
| **Total** | | **~30 hours** over 6–7 evenings |

Note: S2 grew from "ambient layer" to "embodiment layer" and now sits *before* the panels because feeling-alive matters more than panel content at unboxing.

---

## 10. Repo Structure

**Separate repo: `sunkencity999/pre-buddy`** (own release cadence, own CI, easier community firmware forks)

```
pre-buddy/
├── DESIGN.md                 (this file)
├── README.md
├── firmware/
│   ├── src/
│   │   ├── main.cpp
│   │   ├── ble_nus.cpp
│   │   ├── embodiment/        (motion, LEDs, characters)
│   │   │   ├── character_sage.cpp
│   │   │   ├── character_sprout.cpp
│   │   │   ├── character_sentinel.cpp
│   │   │   └── motion_engine.cpp
│   │   ├── panels/            (the 7 panels)
│   │   ├── voice/
│   │   └── vision/
│   └── platformio.ini
├── server/
│   ├── pre_buddy/             (Python module loaded by `pre buddy serve`)
│   └── tests/
├── docs/
│   ├── protocol.md            (wire protocol spec)
│   ├── embodiment.md          (motion budget, character design)
│   └── pairing.md
└── assets/
    └── characters/             (sprite faces per character)
```

---

## 11. Open Decisions (resolved)

| # | Decision | Resolution |
|---|---|---|
| 1 | Separate repo or subtree under `pre`? | **Separate** — `sunkencity999/pre-buddy` |
| 2 | `pre buddy serve` subcommand or standalone? | **Subcommand** under `pre` |
| 3 | Character pack size? | **Three, user picks** — Sage / Sprout / Sentinel |
| 4 | TTS server-side or device-side? | **Server-side** via PRE's voice interface |
| 5 | Pairing UX mirroring Anthropic? | **Yes** — passkey-on-device flow |
| 6 | Primary hardware? | **StackChan (K151-R)** — robot-primary, headless CoreS3 secondary |
| 7 | Naming — separate robot brand? | **No** — single brand "PRE." The robot IS the agent. |

---

## 12. Risks & Mitigations

| Risk | Mitigation |
|---|---|
| Servo failure / stall | Stall detection → still-mode degrade; replaceable-parts story long-term |
| Motion becomes annoying | Strict motion budget (still ~95% of time); user can set "quiet hours" |
| Uncanny valley | Stylized creature aesthetic; no realistic facial features |
| Battery life on always-on | Display dim + LED off after inactivity; USB-C tethered as default |
| Character choice paralysis | Three only, with one-line tagline each; switchable any time |
| Anthropic protocol changes | We own only the `pre.*` namespace; their NUS layer is theirs to evolve |
| BLE pairing UX confusion | Mirror Anthropic's exact flow (familiar to anyone using their buddy) |

---

## 13. What This Is Not

- **Not a smart speaker.** Voice is a feature, not the headline.
- **Not a toy.** The cute factor serves the bond; the bond serves daily use.
- **Not a Claude Desktop competitor.** This complements PRE; it doesn't compete with hosted assistants on hosted-assistant terms.
- **Not a separate product brand.** It's PRE, embodied.

---

## 14. Next Concrete Actions

1. Scaffold `sunkencity999/pre-buddy` repo with this DESIGN.md + READMEs + empty firmware/server skeletons (ready for when CoreS3/StackChan arrives)
2. Draft `docs/protocol.md` spec for `pre.*` namespace
3. Draft `docs/embodiment.md` with the motion budget table fleshed out
4. Order the StackChan K151-R kit
5. Decide on sprite face artist / style (commission vs do-it-yourself pixel art)
