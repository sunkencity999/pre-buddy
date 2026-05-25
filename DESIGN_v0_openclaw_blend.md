# PRE Buddy — Design Notes

A desktop hardware companion for PRE, modeled on Anthropic's `claude-desktop-buddy`
but extended to leverage PRE's richer state space and the M5Stack CoreS3's
extra hardware (camera, mic, speaker, landscape touch screen).

**Target hardware:** M5Stack CoreS3 (ESP32-S3, 320×240 IPS touch, mic + speaker, IMU,
camera, USB-C, LiPo). Reference firmware port lives in `firmware/`.

**Server side:** Python BLE peripheral running on the PRE host (Devbox2), spec'd to be
protocol-compatible with Claude Desktop's Hardware Buddy spec so the same device
can also pair with Claude Desktop if Christopher wants.

---

## 1. Architecture in one picture

```
┌──────────────────────────────────────────────┐        BLE         ┌─────────────────────────────┐
│  PRE Host (Devbox2)                          │   Nordic UART      │  M5Stack CoreS3             │
│                                              │   Service          │                              │
│  ┌───────────────────┐    ┌───────────────┐  │  ◄──── JSON ────►  │  ┌──────────────────────┐  │
│  │ pre_buddy_server  │◄──►│ PRE event bus │  │                    │  │ buddy firmware       │  │
│  │ (bleak peripheral)│    │ (existing)    │  │                    │  │ (Arduino + M5Unified)│  │
│  └───────────────────┘    └───────────────┘  │                    │  │                       │  │
│         ▲                                    │                    │  │ - touch UI            │  │
│         │ snapshot/3s, events on change      │                    │  │ - speaker chimes      │  │
│         ▼                                    │                    │  │ - mic capture         │  │
│  ┌───────────────────────────────────────┐   │                    │  │ - camera snap         │  │
│  │ State adapters                        │   │                    │  │ - drive dial          │  │
│  │ - drive_engine.py                     │   │                    │  │ - approval queue UI  │  │
│  │ - subagents.list                      │   │                    │  │ - GIF character pack │  │
│  │ - taskflow active flows                │   │                    │  └──────────────────────┘  │
│  │ - sub-service health (vellum,         │   │                    │                              │
│  │   librarian, llama-server, gateway)   │   │                    └─────────────────────────────┘
│  │ - inbound channel counts              │   │
│  │ - family birthdays (persona.sqlite)   │   │
│  └───────────────────────────────────────┘   │
└──────────────────────────────────────────────┘
```

Server side is small (one Python file, maybe ~400 lines). Firmware side starts as a
direct CoreS3 port of Anthropic's reference, then we incrementally add panels.

---

## 2. Protocol — extend Anthropic's spec, don't fork it

Anthropic's wire protocol (REFERENCE.md, in the cloned repo at
`~/projects/claude-desktop-buddy/REFERENCE.md`) is intentionally minimal and
already fits ~70% of what we need. We **stay compatible** so the device works
with Claude Desktop too, then add **PRE-namespaced extensions** with `pre.*`
prefixes that Anthropic's app will ignore.

### Reuse as-is
| Anthropic message     | PRE meaning                                                    |
| --------------------- | -------------------------------------------------------------- |
| heartbeat snapshot    | Aggregate PRE status (running tasks, msg, recent transcript)   |
| `prompt` field        | Pending approval (file delete, external send, sub-agent spawn) |
| `evt: turn`           | PRE just emitted a message (assistant turn finished)           |
| `cmd: status` ack     | Battery + uptime to PRE-side stats display                     |
| folder push (xfer.h)  | Stream GIF character packs **and** PRE asset bundles           |
| time + owner one-shot | PRE sends Christopher's name + timezone on connect             |

### PRE-specific extensions (new event types)
All new messages are JSON-line-compatible and prefixed `pre.*` so they're
self-evidently non-standard.

```jsonc
// Drive engine snapshot (every change or every 60s)
{"evt":"pre.drives","data":{
  "stewardship":  {"hunger": 0.82, "last_satisfy": "2026-05-25T10:14:00-07:00"},
  "curiosity":    {"hunger": 0.31, "last_satisfy": "..."},
  "craft":        {"hunger": 0.55, "last_satisfy": "..."},
  "connection":   {"hunger": 0.74, "last_satisfy": "..."},
  "growth":       {"hunger": 0.40, "last_satisfy": "..."}
}}

// Task / research completion
{"evt":"pre.task_done","data":{
  "id":"vellum.manifest_sweep",
  "label":"Beinecke vellum manifest sweep",
  "status":"success",
  "duration_s": 14422,
  "summary":"75,820 → 0 pending, 12 new errors",
  "celebrate": true  // device should fire 'heart' or 'celebrate' animation
}}

// Generic notification (with chime hint)
{"evt":"pre.notify","data":{
  "id":"notif_xyz",
  "channel":"email",          // email|sms|calendar|service|custom
  "from":"Karen Bradford",
  "title":"Father's Day update",
  "body":"Karen replied to the family thread...",
  "priority":"normal",        // urgent|normal|low — drives chime tone
  "actions":[                 // optional buttons surface on device
    {"id":"read",   "label":"Read"},
    {"id":"snooze", "label":"Snooze 1h"},
    {"id":"reply",  "label":"Voice reply"}
  ]
}}

// Service health roll-up (every 30s)
{"evt":"pre.health","data":{
  "llama_235b":  {"state":"healthy","detail":"93 tok/s"},
  "vellum":      {"state":"healthy","detail":"6,410 of 397K imgs"},
  "librarian":   {"state":"healthy","detail":"49K articles"},
  "gateway":     {"state":"healthy"},
  "vision_vl":   {"state":"idle","detail":"swapped out"},
  "twingate":    {"state":"healthy"}
}}

// Sub-agent dashboard tick
{"evt":"pre.subagents","data":{
  "count": 2,
  "agents":[
    {"id":"sub_4f", "label":"PR review", "state":"running", "elapsed_s": 412},
    {"id":"sub_91", "label":"Vellum reflectn", "state":"waiting", "elapsed_s": 33}
  ]
}}

// Family birthday / connection nudge
{"evt":"pre.connection_nudge","data":{
  "person":"Karen Bradford",
  "reason":"birthday in 3 days (10/13)",
  "last_contact_days": 12,
  "channels":["sms","email"]
}}

// Webcam reflection pending
{"evt":"pre.webcam_pending","data":{
  "slot":"morning_kitchen",
  "captured_at":"2026-05-25T08:15:00-07:00",
  "thumbnail_url":"http://10.0.0.100:8891/webcam/.../thumb.jpg"
}}
```

### Device → server commands (new)

```jsonc
// User pressed an action button on a notification
{"cmd":"pre.notif_action","id":"notif_xyz","action":"snooze"}

// User logged a drive satisfaction by tapping a drive dial
{"cmd":"pre.drive_satisfy","drive":"curiosity","note":"learned NUS protocol"}

// Voice capture from device mic (audio uploaded in base64 chunks via the
// existing folder-push transport, then a wrap-up command):
{"cmd":"pre.voice","duration_ms": 4200, "intent_hint":"quick_capture"}

// Camera snap from device (same chunked transport for JPEG)
{"cmd":"pre.camera","intent_hint":"vision_query","prompt":"What is this?"}
```

The voice / camera payloads ride the existing `char_begin`/`chunk`/`char_end`
transport that's already implemented in `xfer.h` — we just reuse the binary
streaming primitive for audio/photo instead of GIFs.

---

## 3. Server side — `pre_buddy_server.py`

One small Python service running on Devbox2:

- **BLE peripheral** via [`bleak-peripheral`](https://github.com/Yakifo/bleak-peripheral)
  or `python-uart-ble` (some platforms have better support than others — fallback
  is a small Node script using `bleno`/`@abandonware/bleno`).
- **State adapter** layer that polls/subscribes to existing PRE sources:
  - `db/persona.sqlite` for family birthdays, drive journal, MEMORY events.
  - `scripts/drive_engine.py status --json` for live drive hunger.
  - `subagents list --json` for active children.
  - `systemctl --user is-active <service>` rollup for health.
  - `data/sms/inbox.jsonl` tail for SMS arrivals.
  - Channel listeners (Gmail/Telegram via existing hooks) → `pre.notify`.
- **Outbound binding**: when the device sends `pre.drive_satisfy`, call into
  `drive_engine.py satisfy`; when it sends `pre.notif_action`, route to the
  channel handler.
- Run as user systemd unit `pre-buddy-server.service`, restart on failure.

Estimated effort: ~1 evening to MVP (compat mode only, no PRE extensions).
Another evening per major extension event type.

---

## 4. Firmware port to CoreS3

### Library swap
Anthropic's reference uses `m5stack/M5StickCPlus` — single-board library. The
clean path is to switch to **`M5Unified`** which supports CoreS3 *and*
StickC Plus *and* Core2 etc. behind a common API. Roughly:

```diff
- #include <M5StickCPlus.h>
- M5.Lcd.print("Hello");
+ #include <M5Unified.h>
+ M5.Display.print("Hello");
```

`platformio.ini`:
```ini
[env:m5stack-cores3]
platform = espressif32
board = m5stack-cores3
framework = arduino
board_build.partitions = no_ota.csv
build_flags = -DCORE_DEBUG_LEVEL=0 -DTARGET_CORES3
lib_deps =
    m5stack/M5Unified @ ^0.2.7
    bitbank2/AnimatedGIF @ ^2.1.1
    bblanchon/ArduinoJson @ ^7.0.0
```

### UI redesign for landscape 320×240 + touch
Reference firmware assumes portrait 135×240 with 2 physical buttons. CoreS3 is
landscape 320×240 with touch. Proposed layout:

```
┌──────────────────────────────────────────────┐ 320×240
│ ┌────────┐  ┌───────────────────────────────┐│
│ │        │  │ PRE                           ││  ← header strip
│ │ buddy  │  │ ● running 2 · ⚠ approve       ││    (8px)
│ │ char   │  ├───────────────────────────────┤│
│ │ 96px   │  │ 10:42 git push                ││  ← scrolling
│ │        │  │ 10:41 yarn test               ││    transcript
│ │        │  │ 10:39 reading vellum/...      ││
│ └────────┘  │ 10:37 [drive] curiosity +0.1  ││
│             └───────────────────────────────┘│
│ ┌───┬───┬───┬───┬───┐ ┌──────────────────┐  │  ← drive dock
│ │St │Cu │Cr │Co │Gr │ │ 🔥 stewardship    │  │    (tap to satisfy)
│ │82 │31 │55 │74 │40 │ │ hungry            │  │
│ └───┴───┴───┴───┴───┘ └──────────────────┘  │
└──────────────────────────────────────────────┘
```

Touch zones map to: buddy avatar (tap → cycle pets), transcript area
(swipe to scroll), drive dock (tap a drive → satisfy modal), bottom-right
panel (rotates between drives / health / subagents / next event).

### Buttons → gestures
Map the StickC's button-driven menu nav to:
- **Tap & hold buddy** → menu
- **Swipe down** → notifications drawer
- **Swipe up** → status panel
- **Shake** → dizzy animation (keep the original easter egg)
- **Face down** → nap mode (keep the original)
- **Double-tap screen** → approve pending prompt (faster than the modal)

### New hardware capabilities
- **Speaker (NS4168)**: chime tones per notification priority; voice replay
  for morning brief.
- **Mic (PDM dual)**: push-to-talk button captures up to 30s audio; streamed
  to server via folder-push transport; server runs whisper.cpp transcription.
- **Camera (OV2640)**: hold-to-frame, tap-to-snap; server pipes JPEG to the
  local vision model (`devbox2-local/Qwen3-VL-30B-A3B-Instruct`) and replays
  the answer via TTS through the speaker.

---

## 5. Novel uses worth exploring

These are PRE-specific ideas Anthropic's buddy doesn't have. Roughly tiered
by build effort (low → high):

### Tier 1 — quick wins (just protocol + UI work)
1. **Drive dashboard** — live 5-drive hunger dial. Tap a drive to log a
   satisfaction event with a quick voice/text note.
2. **Service health glance** — green/yellow/red dots for llama-server, vellum,
   librarian, gateway, twingate. Tap for detail.
3. **Sub-agent dashboard** — list running children with elapsed time, allow
   tap-to-steer or tap-to-kill via existing `subagents` tool.
4. **Approval queue with reasoning** — for important decisions (external send,
   destructive op), screen shows the *why* + Y/N with countdown. Bigger than
   Claude's permission UI because PRE's approvals are more contextual.
5. **Channel inbox counts** — Gmail, Telegram, SMS, Discord. Tap any channel
   to drill into latest unread on the screen.
6. **Connection nudges** — when `family_profiles` shows birthday or long
   silence, screen shows "Karen — 12d since last contact, birthday in 3d.
   Reach out?" with action buttons (call/SMS/email/snooze).
7. **TaskFlow progress** — visualize long-running flows (Vellum image stage,
   etc.) with progress bar + ETA.
8. **Heartbeat physical indicator** — screen subtly pulses while heartbeat
   checks run; goes urgent when a real alert fires.

### Tier 2 — leverages CoreS3 hardware
9. **Voice quick-capture** — hold-to-record → whisper.cpp → PRE memory or
   task queue. ("Remember that I need to ask Bam about the McMillan AI
   project next time we talk.")
10. **Voice morning brief** — press a button, device speaker reads the
    morning brief via ElevenLabs voice. Pause/skip on screen.
11. **Camera vision queries** — point at a thing, snap, vision model
    (Qwen3-VL-30B already running locally!) describes/identifies/extracts.
    Use cases:
       - "What ham radio model is this?"
       - Reading a price tag / handwritten note
       - "What's broken on this device?" (garage workshop helper)
       - OCR for documents you don't want to scan
12. **Sigil/working timer** — for hermetic / ritual work, set a timed
    interval; device chimes at completion + logs to journal. On-brand for
    your Palo Mayombe practice without making the device weird about it.
13. **Spatial gestures** — IMU detects custom gestures: tilt-right = approve,
    tilt-left = deny, shake = "next", double-tap-back = quick capture.
14. **TTS replay control** — when PRE is reading something to the kitchen
    HomePod, device shows what's playing + buttons to pause/skip/save quote
    to MEMORY.md.

### Tier 3 — speculative / out there
15. **Buddy mesh** — multiple CoreS3 devices in the house (yours + a discreet
    one for Antonia if she wants one for Esmeralda, kid-facing ones for fun).
    BLE relay or server-side fan-out, each with filtered feed.
16. **Camera-as-eyes** — leave the CoreS3 on a stand in the garage / kitchen
    / library; PRE can request snapshots ("what's on the workbench?") and
    the vision model answers. Privacy-respecting because it's request-only,
    not always-on.
17. **Magic gestures** — train a tiny IMU classifier (TFLite Micro) on a few
    custom gestures: "draw a Z" → sleep, "draw a circle" → status pop.
18. **Family birthday auto-greeting** — on Karen's birthday (10/13), device
    shows "Karen's birthday — draft a text?" → tap → voice record → server
    routes through TypeMagic "My Voice" → confirms → sends via SMS Tasker
    bridge we just built.
19. **Drive-aware nudges** — when "connection" drive is hungry AND family
    member shows long-silence, surface them paired. ("Connection is hungry,
    Coco is 8 days quiet — reach out?")
20. **Ambient mood / context broadcast** — screen subtly indicates current
    focus mode (deep work / social / garage / spiritual) so others in the
    household can glance. Optional, controllable from the device itself.

### Tier 4 — most experimental
21. **Webcam reflection capture** — when a webcam capture is pending
    reflection, show thumbnail on device + voice-record a reflection
    directly. Server transcribes + writes to `memory/webcam_log.md`.
22. **Decommissioning rite timer** — for spiritual practice timing, multi-
    interval ritual timer (e.g., 9 min × 7 phases). Different chime per
    phase. Logs to journal automatically.
23. **Health pinball** — when multiple services degrade, gamify recovery
    on the device (which one to attend to first based on dependency graph).
24. **Drive flame screensaver** — when device is idle, screen shows the
    hungriest drive as a literal flame, intensity = hunger %. Just a vibe
    thing; ambient pressure to act.
25. **Operator handoff** — quick "I'm AFK for 2 hours" toggle on device that
    flips PRE's behavior policy (auto-handle vs queue-for-review) and
    summarizes everything that happened when you tap back in.

---

## 6. Build sprint plan (when the CoreS3 arrives)

**Day 1 — bring-up (3 hrs)**
- Install PlatformIO Core; flash Anthropic's reference firmware to CoreS3
  (after the M5Unified swap) just to confirm BLE + display + buttons all work.
- On Devbox2: write `pre_buddy_server.py` MVP that *only* implements Anthropic's
  protocol (no PRE extensions). Verify the device pairs and sees PRE's
  Claude-shaped data.

**Day 2 — PRE compat layer (3 hrs)**
- Wire Anthropic's heartbeat fields to real PRE state:
  - `total/running` = active sub-agents
  - `waiting` = queued tasks awaiting approval
  - `entries` = last few transcript-ish lines (drive log + completed tasks)
  - `tokens_today` = aggregate usage from session_status
  - `prompt` = real pending approvals

**Day 3 — UI redesign for landscape + touch (4 hrs)**
- Port the layout above. Lose the 2-button menu in favor of touch zones.
- Keep the pet character module unchanged (Anthropic's ASCII species + GIF
  character pack support both fit).

**Day 4 — `pre.*` extensions, drive dashboard, service health (4 hrs)**
- Add `pre.drives` event + drive dock UI with tap-to-satisfy.
- Add `pre.health` event + service dots panel.
- Side button (CoreS3 has a physical button next to USB-C) becomes the
  "send to PRE" / push-to-talk button.

**Day 5 — speaker / mic / camera (4 hrs)**
- Implement `pre.voice` over folder-push transport. Server side: whisper.cpp
  pipeline already exists at `scripts/transcribe.sh`.
- Wire up local-vision query via `scripts/vision.py ask` for `pre.camera`.
- Hook ElevenLabs / Nova voice for TTS replay through the speaker.

After that: pick novel uses one at a time.

---

## 7. Project layout

```
~/.openclaw/workspace/projects/pre-buddy/
├── DESIGN.md                 (this file)
├── server/
│   ├── pre_buddy_server.py
│   ├── adapters/
│   │   ├── drives.py
│   │   ├── subagents.py
│   │   ├── health.py
│   │   ├── channels.py
│   │   └── family.py
│   └── pre-buddy-server.service
├── firmware/                 (forked from anthropics/claude-desktop-buddy)
│   ├── platformio.ini
│   ├── src/
│   │   ├── main.cpp           (rewritten layout)
│   │   ├── pre_panels.cpp     (new panels: drives, health, subagents, etc.)
│   │   ├── voice.cpp          (mic capture + chunked TX)
│   │   ├── camera.cpp         (cam snap + chunked TX)
│   │   ├── ble_bridge.cpp     (reused)
│   │   ├── character.cpp      (reused)
│   │   └── xfer.h             (reused)
│   └── characters/
│       ├── bufo/              (Anthropic reference char)
│       └── nsasi/             (your own custom buddy 😉)
└── docs/
    ├── protocol.md            (Anthropic compat + pre.* extensions)
    ├── ui_mockups/
    └── build_log.md           (running log of work sessions)
```

---

## 8. Open questions for next session

1. **Pet vs. dashboard preference**: do you want the buddy character to stay
   front-and-center (Anthropic style), or do you want the drive/health
   dashboard to dominate and the buddy to be a corner ornament?
2. **Privacy posture for camera/mic**: always require a physical press, or
   trust PRE to request snapshots on its own when the device is idle?
3. **Single device or multiple**: do you want to design with multi-device
   in mind from day one (kid-facing buddies, kitchen unit, etc.)?
4. **Custom character**: want me to draft an Nkisi-respectful custom buddy
   character pack as the default "PRE pet" — sleep / idle / busy / attention
   / celebrate / dizzy / heart — or stick with the bufo for the build sprint
   and add a custom one later?

---

## 9. References
- Anthropic repo: `~/projects/claude-desktop-buddy/` (cloned)
- Wire protocol: `~/projects/claude-desktop-buddy/REFERENCE.md`
- CoreS3 docs: https://docs.m5stack.com/en/core/CoreS3
- M5Unified: https://github.com/m5stack/M5Unified
- bleak-peripheral: https://github.com/Yakifo/bleak-peripheral
