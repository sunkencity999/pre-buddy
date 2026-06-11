# PRE Buddy

> **The embodied avatar of your local PRE agent.**
> A desktop robot that surfaces PRE's introspection (background agents, routing, confidence, memory, scheduler, tools) and turns your local model into something you can see and feel on your desk.

**Cloud assistants live in datacenters. Yours lives on your desk.**

---

## Table of Contents

- [What PRE Buddy is](#what-pre-buddy-is)
- [Current status](#current-status)
- [Hardware targets](#hardware-targets)
- [Architecture at a glance](#architecture-at-a-glance)
- [Repository map](#repository-map)
- [Quickstart — end users](#quickstart--end-users)
- [Quickstart — developers](#quickstart--developers)
- [Tutorial: run scenarios and inspect robot behavior](#tutorial-run-scenarios-and-inspect-robot-behavior)
- [Extended tutorial](#extended-tutorial)
- [Core concepts](#core-concepts)
- [Protocol workflow](#protocol-workflow)
- [Testing and quality gates](#testing-and-quality-gates)
- [Hardware bring-up checklist](#hardware-bring-up-checklist)
- [Developer workflows](#developer-workflows)
- [Troubleshooting](#troubleshooting)
- [Roadmap](#roadmap)
- [Contributing](#contributing)
- [License](#license)

---

## What PRE Buddy is

PRE Buddy is a **robot-first companion interface** for PRE.

It is not just a display. It is an embodiment layer on top of PRE's internal state:

- what background agents are doing,
- how model routing decisions are being made,
- where confidence is weak,
- when memory is being written,
- what tools are succeeding/failing,
- what is coming next on the scheduler.

The design deliberately keeps the robot calm and ambient most of the time (**still ~95%**) and reserves movement for meaningful events.

For full design rationale and product decisions, read [DESIGN.md](./DESIGN.md).

---

## Current status

🚧 **Late pre-hardware phase.** The entire stack is built and host-tested
end-to-end against mocks; the only remaining work is wiring real
peripherals on the M5Stack CoreS3 once a board is in hand.

**Host-buildable, in production today:**

| Area | What |
|---|---|
| Protocol | 22-event `pre.*` wire contract (system, character, introspection, audio). YAML source + generated Python/C++ stubs + CI drift check. |
| Server core | `events.py` typed payloads, `serializer.py`, `pump.py`, `serve.py` mock-BLE driver, `bridge.py` for PRE WebSocket → `pre.*` events. |
| Real BLE transport | `transport_ble.py` with `bleak`-based central client behind a sync adapter; mock backend for tests. |
| GUI onboarding | `pre-buddy setup` wizard (BLE scan, character, wake word, autostart), `pre-buddy tray` (pystray menubar app with Connect / Open Viewer / Launch-at-login / Character / Quit). |
| Browser viewer | Static HTML/SVG scenario player (24 distinct character × expression faces composed inline). `pre-buddy viewer` serves it. |
| Firmware core | Header-only C++17 `pre_buddy_core` library: `motion.h` safety + rate limit, `character.h`/`expression.h`, `protocol.h` event→embodiment mapper, `robot_loop.h`, `character_picker.h`, `boot_flow.h`, `sprite_atlas.h`, `led_palette.h`, `servo_math.h`, `line_framer.h`. |
| Firmware HAL | Interfaces for servo, LED, display, BLE transport, mic, speaker, wake-word detector, character store. In-process mocks for each. |
| Voice path | `pre.audio.*` event suite (codec hand-shake, session lifecycle, base64-in-JSON frames), `audio_bridge.py` with pluggable STT/TTS/PRE adapters. |
| Sprite atlas | `tools/generate_sprites.py` renders 24 PNG faces matching the viewer; `tools/sprites_to_header.py` emits RGB565 data into `firmware/esp32/main/sprites_data.h`. |
| Cross-platform launchers | `.command` (macOS), `.desktop` (Linux), `.bat` (Windows) for setup + tray. |

**Implemented + verified on hardware (M5Stack CoreS3 / K151-R):**

- `firmware/esp32/main/esp32_*.cpp`: Feetech SCSCL serial servos (UART), the 12-LED WS2812 ring (via the PY32 I/O expander), speaker (AW88298) + mic (ES7210) over a shared I2S bus, NimBLE NUS peripheral, ESP-SR "Hi, ESP" wake-word, and user settings persisted to NVS.
- The full **conversational loop**: wake → record → Whisper STT → PRE → spoken reply, with a "thinking" animation while PRE works (see _Conversational loop_ below).
- Adjustable live from PRE's GUI / the `buddy_control` agent tool: LED color + brightness, volume, animations, character.

**Still TODO:** button/touch input for the first-boot `CharacterPicker`; full-duplex I2S (listen while speaking); per-unit servo zero calibration.

See [Hardware bring-up checklist](#hardware-bring-up-checklist) below.

---

## Hardware targets

- **Primary (flagship):** [M5Stack StackChan Remote Kit (K151-R)](https://shop.m5stack.com/products/m5stackchan-ai-desktop-robot-kit-with-remote-control-esp32-s3)
  - CoreS3 (ESP32-S3) + animatronic body (servos, LEDs, camera, mics)
- **Secondary (headless/dev):** bare CoreS3
  - same protocol and logic, no body

One firmware architecture, two physical forms.

---

## Architecture at a glance

Everything is built around a strict separation between **host-testable
logic** and **hardware-bound glue**. The same pattern shows up in every
slice (motion, character identity, audio, BLE, persistence):

```
┌──────────────────────────────┐   ┌───────────────────────────────┐
│  PRE (the local agent)       │   │  ESP32-S3 CoreS3 robot         │
│  ws://localhost:7749         │   │  (Stack-Chan body)             │
└──────────┬───────────────────┘   └───────────────┬───────────────┘
           │ bg_agent / route /                    │ JSON-lines over
           │ memory_saved / route /                │ BLE NUS (Nordic
           │ confidence_* WS events                │ UART Service)
           ▼                                       ▲
┌──────────────────────────────────────────────────┴───────────────┐
│                       pre-buddy server (Python)                   │
│                                                                   │
│  PreBridge       AudioBridge        TrayController                │
│  (PRE WS →       (Whisper STT +     (pystray menu,                │
│   pre.* events)   say/eSpeak TTS)    BLE connect toggle)          │
│                                                                   │
│  EventPump  ──►  BleNusTransport (bleak central, NUS UUIDs)       │
│                                                                   │
│  Browser viewer (SVG faces, 24 character × expression states)     │
└────────────────────────────────────────────────────────────────────┘
```

Inside the device, the same pattern again:

```
RobotLoop (host-testable, single-file, no IDF deps)
   │
   ├─ map_event(Event, Character) ──► EmbodimentCommand
   │       motion (clamped via MotionEngine)
   │       LED color (from led_palette.h)
   │       expression (one of 8 moods)
   │
   ├─ IServoDriver         ─► Esp32ServoDriver (LEDC PWM, TODO)
   ├─ ILedDriver           ─► Esp32LedDriver   (RMT SK6812, TODO)
   ├─ IDisplayDriver       ─► Esp32DisplayDriver (ILI9342, TODO)
   ├─ IMicDriver           ─► Esp32MicDriver   (I2S PDM, TODO)
   ├─ ISpeakerDriver       ─► Esp32SpeakerDriver (I2S DAC, TODO)
   ├─ IWakeWordDetector    ─► Esp32WakeWordDetector (ESP-SR, TODO)
   ├─ IBleTransport        ─► Esp32BleTransport (NimBLE, TODO)
   └─ ICharacterStore      ─► Esp32NvsCharacterStore (NVS, TODO)
```

Everything left of the `─►` arrow has host tests. Everything right of
the arrow is the ESP-IDF code — now implemented and verified on the CoreS3.

---

## Repository map

```text
pre-buddy/
├── DESIGN.md                              Product + architecture spec
├── docs/                                  Long-form internal docs
│   ├── README.md                          Docs index
│   ├── tutorial.md                        End-to-end pre-hardware tutorial
│   ├── protocol.md                        Protocol implementation notes
│   ├── embodiment.md                      Motion/LED/expression notes
│   └── pairing.md                         Pairing flow notes
├── shared/protocol/
│   ├── events.md                          Human-readable wire contract
│   ├── events.yaml                        Machine-readable schema source
│   └── uuids.md                           Nordic NUS UUID assignments
├── tools/
│   ├── generate_protocol_stubs.py         Emits Python/C++ event-name stubs
│   ├── generate_sprites.py                Renders 24 face PNGs (Pillow)
│   └── sprites_to_header.py               PNG → RGB565 C++ array
├── server/                                Python package (`pre-buddy` CLI)
│   ├── pyproject.toml                     [transport]/[bridge]/[tray] extras
│   ├── pre_buddy/
│   │   ├── events.py                      Typed payloads (incl. audio)
│   │   ├── serializer.py                  JSON-lines encode/decode
│   │   ├── transport.py                   MockBleSession (host tests)
│   │   ├── transport_ble.py               Real bleak NUS central
│   │   ├── uuids.py                       NUS UUID constants
│   │   ├── pump.py                        EventPump (FIFO)
│   │   ├── serve.py                       BuddyServer runtime
│   │   ├── bridge.py                      PRE WebSocket → pre.* mapper
│   │   ├── audio_bridge.py                Mic → STT → PRE → TTS → speaker
│   │   ├── tray.py                        TrayController + pystray glue
│   │   ├── setup_wizard.py                `pre-buddy setup` wizard
│   │   ├── viewer.py                      Serves the static viewer
│   │   ├── config.py                      ~/.config/pre-buddy/config.json
│   │   ├── autostart.py                   Cross-platform launch-at-login
│   │   ├── mock_robot.py                  Software robot simulator
│   │   ├── simulate.py                    Timeline renderers
│   │   ├── assets/tray_icon.png           Shipped via package-data
│   │   └── cli.py                         `pre-buddy` argparse entry
│   ├── examples/                          Scenario JSONL + run_all.sh
│   └── tests/                             pytest suite (174 cases)
├── firmware/
│   ├── CMakeLists.txt                     Host-test build (cmake + ctest)
│   ├── core/include/pre_buddy/            Host-testable C++17 core
│   │   ├── character.h, expression.h
│   │   ├── motion.h                       Servo clamp + rate limit
│   │   ├── protocol.h                     Event → embodiment mapper
│   │   ├── robot_loop.h                   Glue: events ↔ HAL
│   │   ├── character_picker.h             First-boot identity selector
│   │   ├── character_store.h              ICharacterStore interface
│   │   ├── boot_flow.h                    Picker / store glue
│   │   ├── sprite_atlas.h                 (Character, Expression) → index
│   │   ├── led_palette.h                  LedColor → RGB888
│   │   ├── servo_math.h                   angle → LEDC duty
│   │   ├── line_framer.h                  Newline framing for BLE RX
│   │   ├── generated_event_kinds.h        From events.yaml (CI drift check)
│   │   └── hal/                           Interfaces + mocks
│   │       ├── i_servo.h, i_led.h, i_display.h
│   │       ├── i_ble_transport.h
│   │       ├── i_mic.h, i_speaker.h, i_wake_detector.h
│   │       ├── mock.h, mock_audio.h
│   │       ├── in_memory_character_store.h
│   │       └── uuids.h
│   ├── esp32/                             ESP-IDF skeleton, NOT in CI
│   │   ├── CMakeLists.txt
│   │   ├── README.md                      Bring-up steps
│   │   └── main/
│   │       ├── CMakeLists.txt             Component declaration
│   │       ├── main.cpp                   app_main(): wire HAL + RobotLoop
│   │       ├── esp32_servo.{h,cpp}
│   │       ├── esp32_led.{h,cpp}
│   │       ├── esp32_display.{h,cpp}
│   │       ├── esp32_ble.{h,cpp}
│   │       ├── esp32_mic.{h,cpp}
│   │       ├── esp32_speaker.{h,cpp}
│   │       ├── esp32_wake_word.{h,cpp}
│   │       ├── esp32_character_store.{h,cpp}
│   │       ├── sprites/                   24 PNGs (generated)
│   │       └── sprites_data.h             RGB565 atlas (generated)
│   └── test/                              C++ host tests (107 cases)
├── viewer/                                Static browser scenario player
│   ├── index.html
│   ├── viewer.css
│   └── viewer.js                          SVG faces × playback engine
└── launchers/                             Cross-platform clickable launchers
    ├── PRE Buddy.command, PRE Buddy Setup.command    (macOS)
    ├── pre-buddy.desktop, pre-buddy-setup.desktop    (Linux)
    └── PRE Buddy.bat, PRE Buddy Setup.bat            (Windows)
```

---

## Quickstart — end users

For people who just want to run PRE Buddy without touching the code:

```bash
# 1) install (one-time). Add 'voice' (or use 'robot' = transport+bridge+voice)
#    for the conversational loop; 'tray' for the menu-bar app.
pip install 'pre_buddy[tray,robot]'

# 2) run the setup wizard
pre-buddy setup
#   - scans for nearby BLE devices, lets you pick one
#   - asks which character identity (sage / sprout / sentinel)
#   - asks for the wake word (default "hey buddy")
#   - asks whether to launch at login

# 3) start the tray
pre-buddy tray
#   - status indicator in the menu bar / notification area
#   - Connect / Disconnect toggle
#   - Open Viewer (scenario playback in your browser)
#   - Character submenu (live-switch identity over BLE)
#   - Launch-at-login checkbox
```

Or grab the platform launcher from `launchers/` and double-click. See
[`launchers/README.md`](./launchers/README.md) for install instructions
per OS.

---

## Conversational loop (talk to PRE through the robot)

Say **"Hi, ESP"**, ask a question, and the robot speaks PRE's answer — fully
local. Mic audio streams to the host over BLE, Whisper transcribes it, PRE
answers, and macOS `say` synthesizes the reply back to the robot's speaker,
with a "thinking" animation covering PRE's latency.

```bash
# one-time: install the voice deps (faster-whisper, numpy) + BLE + WS
pip install -e 'server[robot]'
#   faster-whisper is the STT engine; if absent, falls back to the
#   `whisper` CLI (pip install openai-whisper). macOS `say` does TTS.

# with PRE running on ws://localhost:7749 and the robot powered on:
.venv/bin/python tools/converse.py            # input-only (prints the reply)
.venv/bin/python tools/converse.py --play-back # robot speaks the reply
```

For the always-on **ambient** link (robot reacts to PRE activity + exposes the
manage-bot panel in PRE's GUI) run the bridge instead:

```bash
.venv/bin/python tools/live_bridge.py
```

**Managing the robot.** While the bridge is connected, PRE's web GUI shows a
**PRE Buddy** panel in the sidebar (LED color/brightness, volume, animations,
character). PRE can also adjust the robot on request via its `buddy_control`
tool ("turn your eyes green", "lower your volume"). Settings persist in the
robot's NVS across reboots.

**Firmware.** Build + flash with ESP-IDF v5.x (see _Quickstart — firmware_).
The board needs a cold power-cycle after flashing; the mic→PRE→speaker paths,
wake-word, and settings are all on-device.

---

## Quickstart — developers

From repo root:

```bash
# 1) create dev venv
python3 -m venv .venv
. .venv/bin/activate

# 2) install server package + test deps
pip install -e 'server[dev]'

# 3) run tests
pytest -q server/tests

# 4) build + run firmware host tests
cmake -S firmware -B firmware/build
cmake --build firmware/build
firmware/build/pre_buddy_host_tests

# 5) run the mock server demo (prints outbound JSON-lines)
pre-buddy serve --demo

# 6) simulate robot responses for a scenario
pre-buddy simulate \
  --playback server/examples/alerts_scenario.jsonl \
  --character sentinel

# 7) open the browser viewer with the built-in demo timeline
pre-buddy viewer
```

### Useful subcommands

| Command | What it does |
|---|---|
| `pre-buddy setup` | Interactive first-time setup. Use `--non-interactive` with `--device-address` / `--device-name` / `--character` / `--wake-word` / `--autostart on\|off` for scripts. |
| `pre-buddy tray` | Headless tray app. `--once` boots and exits (smoke test). |
| `pre-buddy serve --demo` | Drains the built-in demo events through a mock BLE session. |
| `pre-buddy serve --transport ble --device-name pre-buddy` | Drives a *real* peripheral via `bleak`. Requires the `[transport]` extra. |
| `pre-buddy bridge --pre-url ws://localhost:7749` | Subscribes to PRE's WebSocket and emits `pre.*` events. `--from-file` for offline playback. |
| `pre-buddy simulate --playback X.jsonl --character sage --format json --out timeline.json` | Generate a timeline for the browser viewer. |
| `pre-buddy viewer [--port 7750]` | Serves the static SVG scenario player. |
| `pre-buddy emit pre.system.proximity --distance-cm 35` | Emit a single hand-built event for ad-hoc testing. |

---

## Tutorial: run scenarios and inspect robot behavior

### 1) Review a scenario in text mode

```bash
pre-buddy simulate \
  --playback server/examples/daily_flow_scenario.jsonl \
  --character sprout \
  --severity normal
```

You will see output like:

- source event
- LED color
- whether motion happened
- head pose and duration
- behavior note (`router_escalation_nod`, `error_still`, etc.)

### 2) Export a CSV for quick analysis/plotting

```bash
pre-buddy simulate \
  --playback server/examples/alerts_scenario.jsonl \
  --character sentinel \
  --severity loud \
  --format csv \
  --out /tmp/alerts_sentinel_loud.csv
```

### 3) Generate full matrix (all scenarios × all characters)

```bash
./server/examples/run_all.sh normal
# optional:
./server/examples/run_all.sh loud /tmp/pre-buddy-sim
```

This writes `.txt`, `.csv`, and `.json` bundles per scenario-character pair.

### 4) Validate behavior has not drifted

```bash
pytest -q server/tests/test_golden_scenarios.py
```

If snapshots fail unexpectedly, you changed behavior mapping. Either fix it, or intentionally regenerate snapshots in a reviewable commit.

---

## Extended tutorial

For a step-by-step pre-hardware walkthrough with command sequences, see:

- [docs/tutorial.md](./docs/tutorial.md)

For the full internal docs map, see:

- [docs/README.md](./docs/README.md)

---

## Core concepts

### Character profiles

Three profiles, picked once during setup and persisted to NVS on the
device:

- **Sage** — calm, deliberate, blue idle, slower reactions, longer blinks
- **Sprout** — curious, snappy, green idle, quick reactions, frequent blinks
- **Sentinel** — watchful, steady, white idle, returns to centre between tasks

Character selection changes expression style (timing, colors, motion
behavior), not capability. Live-switchable from the tray menu — the
new choice is persisted *and* pushed to the device via
`pre.character.set`.

### Facial expressions

Eight expressions cover the v1 event mapping without exploding the
sprite budget: **Neutral, Surprised, Thinking, Concerned, Happy,
Sleepy, Curious, Error.** Each character renders each expression with
its own visual identity (24 face states total). The viewer composes
these inline in SVG; the device blits pre-rendered PNG-derived RGB565
sprites from a generated atlas.

### Severity profiles (simulator)

- `quiet` — reduce non-critical movement
- `normal` — baseline
- `loud` — more expressive for alert-heavy contexts

### Voice (wake word + audio session)

Voice flow when the CoreS3 hardware is wired:

```
user says "hey buddy"
  → on-device wake detector fires → pre.audio.wake_word_detected
  → device opens session: pre.audio.input_start
  → mic captures PCM @ 16 kHz mono, Opus-encoded 20 ms frames
  → base64 in pre.audio.input_frame × N
  → VAD silence → pre.audio.input_stop
server (AudioBridge)
  → STT → PRE WebSocket → assistant reply text
  → TTS → Opus → pre.audio.output_frame × N
  → pre.audio.output_stop
device → I2S DAC → speaker
```

The wake word is configurable per user in `config.json`. ESP-SR's
catalog is fixed at compile time today; phrases outside it silently
fall back to "hey buddy" (mirrored by `MockWakeWordDetector` so host
tests cover that path).

### Motion safety invariants

- Y-axis clamp: **10°..80°**
- X-axis rate-limited (180°/s default) in firmware core
- Error responses are red + still (no shaking)
- All servo commands flow through `MotionEngine.clamp()` before reaching the driver

See [docs/embodiment.md](./docs/embodiment.md) for detail.

---

## Protocol workflow

`shared/protocol/events.yaml` is the machine source of truth.

When changing events:

1. Update `shared/protocol/events.md` and `shared/protocol/events.yaml`
2. Regenerate stubs:
   ```bash
   python3 tools/generate_protocol_stubs.py
   ```
3. Update Python typed payloads (`server/pre_buddy/events.py`)
4. Update C++ event parsing/mapping (`firmware/core/include/pre_buddy/protocol.h`)
5. Add/update tests on both sides

CI enforces generated file freshness (`--check`).

---

## Testing and quality gates

### Server tests

```bash
pytest -q server/tests
```

Covers:

- event validation/hydration (incl. `pre.audio.*` round-trip)
- serializer behavior
- mock transport + real BLE transport (against `FakeBleBackend`)
- CLI behavior (every subcommand)
- setup wizard prompts (driven via `io.StringIO`)
- tray controller state machine (no pystray needed)
- config storage round-trip + forward-compat extras
- cross-platform autostart (macOS/Linux/Windows paths via overrides)
- bridge mapping (PRE WS events → `pre.*`)
- audio bridge end-to-end (mock STT/TTS/PRE)
- simulator logic + golden scenario snapshots

### Firmware host tests

```bash
cmake -S firmware -B firmware/build -DCMAKE_BUILD_TYPE=Debug
cmake --build firmware/build -j
ctest --test-dir firmware/build --output-on-failure
```

Covers:

- character profiles + expression enum round-trip
- motion safety/rate limiting
- protocol event parsing + event → embodiment mapping
- character picker state machine
- character store + boot flow
- sprite atlas index table
- LED palette + servo math + line framer
- HAL interface contracts (servo/LED/display/BLE/mic/speaker/wake-word mocks)
- RobotLoop dispatch (every event kind through every output channel)

### CI

GitHub Actions (`.github/workflows/ci.yml`) runs:

- Python tests (3.10/3.11/3.12) — **174 server tests**
- generated-protocol drift check
- C++ host build + ctest — **107 firmware tests**

Total: **281 host tests, all green.**

---

## Hardware bring-up checklist

When the M5Stack CoreS3 arrives, the remaining work is purely the
ESP-IDF API calls inside each `firmware/esp32/main/esp32_*.cpp` stub.
The values they consume (LED palette, servo duty math, NUS UUIDs,
sprite indices, codec params) are already host-tested.

### 0. One-time setup

```bash
# Install ESP-IDF v5.x. Then:
cd firmware/esp32
idf.py set-target esp32s3
idf.py menuconfig    # enable ESP-SR + NimBLE host
```

### 1. Smoke test (`hello, ble`)

Goal: prove the build + flash + advertise path works.

- [ ] `idf.py build flash monitor` — must reach `app_main()` cleanly
- [ ] Fill in `Esp32BleTransport::start()` — NimBLE init + advertise NUS service
- [ ] Verify advertisement with `nRF Connect` (phone) or `bluetoothctl` (Linux)
- [ ] From the host: `pre-buddy serve --transport ble --device-name pre-buddy` — should connect

### 2. Wire each peripheral

Order matters — every stage builds on the previous one being verified.

- [ ] **Display** (`esp32_display.cpp`):
  - `init()`: panel reset + LovyanGFX/M5Unified begin
  - `show_character()`: blit the corresponding sprite from `sprites_data.h::SPRITE_TABLE[sprite_index(ch, Expression::Neutral)]`
  - `show_expression()`: same, with the right expression index
  - `show_passkey()`, `clear()`: standard panel calls
- [ ] **LED** (`esp32_led.cpp`):
  - `init()`: RMT TX channel for SK6812 (W-byte order on the M5Stack ring)
  - `set_color()` already calls `to_rgb888 + apply_brightness` — just feed the RGB into the RMT frame
- [ ] **Servos** (`esp32_servo.cpp`):
  - `init()`: LEDC timer at 50 Hz, 16-bit resolution, two channels for X/Y
  - `move()` already calls `angle_to_duty()` — feed the result into `ledc_set_fade_with_time()` for smooth motion
- [ ] **Character store** (`esp32_character_store.cpp`):
  - `init()`: `nvs_flash_init()` + `nvs_open("pre_buddy", NVS_READWRITE, &handle_)`
  - `load/save/clear`: standard `nvs_get_str / nvs_set_str / nvs_erase_key`
- [ ] **Mic** (`esp32_mic.cpp`):
  - `start_capture()`: I2S PDM RX channel, spawn a task that calls `i2s_channel_read()` in a loop and invokes the callback
- [ ] **Speaker** (`esp32_speaker.cpp`):
  - `start_playback()`: I2S TX channel + toggle the CoreS3's AW88298 amp via I2C
  - `play_frame()`: `i2s_channel_write()`
- [ ] **Wake word** (`esp32_wake_word.cpp`):
  - `start()`: `esp_srmodel_init()` + spawn the AFE feed/fetch task. Same PCM ring buffer as the mic capture; tap it for the wake detector input.

### 3. Wire the first-boot picker input

The picker state machine in `character_picker.h` is host-tested. The
only missing piece is the input loop inside the lambda in
`firmware/esp32/main/main.cpp`. Replace the placeholder body with:

```cpp
while (!picker.is_confirmed()) {
    display.show_character(picker.current());
    if (button_short_pressed())  picker.next();
    else if (button_long_pressed())  return picker.confirm();
    vTaskDelay(pdMS_TO_TICKS(50));
}
```

The CoreS3's three buttons (`BtnA`/`BtnB`/`BtnC` via `M5Unified`) are
the obvious choice; alternatively, the IPS panel's touch driver.

### 4. End-to-end test

- [ ] Power on a fresh device → first-boot picker appears on the IPS panel
- [ ] Pick a character → confirm → device boots into RobotLoop and shows the chosen idle face
- [ ] Power-cycle → device skips the picker and loads from NVS
- [ ] `pre-buddy serve --transport ble --device-name pre-buddy` → device LED + face react to demo events
- [ ] Say "hey buddy" → device opens an audio session → server transcribes via Whisper → PRE replies → device speaks the response

### 5. Polish

- [ ] Per-unit servo calibration (write to NVS, surfaced in the tray menu eventually)
- [ ] Quiet-hours schedule (reduce motion + dim LED on a cron)
- [ ] Stall detection → graceful degrade to "still mode" + alert via `pre.system.error`
- [ ] Battery state event surfaced to the tray status badge

---

## Developer workflows

### Add a new scenario

1. Create `server/examples/<name>_scenario.jsonl`
2. Run simulation:
   ```bash
   pre-buddy simulate --playback server/examples/<name>_scenario.jsonl --character sage
   ```
3. If this should become a locked behavior expectation, add a golden snapshot test.

### Update golden snapshots intentionally

1. Regenerate expected output from the current simulator.
2. Commit snapshot updates with a clear message describing why behavior changed.
3. Keep snapshot diffs small and reviewable.

### Batch generate comparison artifacts

```bash
./server/examples/run_all.sh normal
```

Artifacts are written to `server/examples/output/` (gitignored).

---

## Troubleshooting

### `pre-buddy: command not found`

Use the venv binary directly:

```bash
.venv/bin/pre-buddy version
```

Or activate venv:

```bash
. .venv/bin/activate
```

### `generated files are stale` in CI

Run:

```bash
python3 tools/generate_protocol_stubs.py
```

Then commit regenerated files.

### Snapshot test failures

Run the scenario locally and inspect output diff first. If intentional, update golden files in same PR with rationale.

---

## Roadmap

The big near-term items from earlier drafts are now landed:

- ✅ Real BLE/NUS transport (bleak central + `pre-buddy serve --transport ble`)
- ✅ Live PRE event bus integration (`pre-buddy bridge`)
- ✅ UI simulator pass (browser viewer with 24 faces)
- ✅ HAL interfaces + mocks for every peripheral; ESP-IDF stubs for each
- ✅ Voice protocol + server STT/TTS bridge + ESP32 mic/speaker/wake-word stubs
- ✅ On-device first-boot character picker (host-testable state machine)
- ✅ NVS-backed character persistence
- ✅ Cross-platform GUI onboarding (setup wizard, tray, launchers, autostart)
- ✅ Sprite atlas generator + RGB565 header for the IPS panel

What's left (all hardware-bound — see
[Hardware bring-up checklist](#hardware-bring-up-checklist)):

1. Flash the M5Stack CoreS3 with the assembled firmware and verify each
   peripheral works against its host-tested abstraction.
2. Train or license a custom wake-word model if "hey buddy" isn't the
   final choice (ESP-SR custom phrase / Porcupine paid tier).
3. Per-unit device calibration (servo travel, panel orientation).
4. Vision (camera capture + on-device or server-side inference) — pure
   roadmap item; the protocol doesn't reserve event names for it yet.

---

## Contributing

Until public contributor policy is finalized:

- keep commits focused and test-backed
- update docs/spec/tests together
- avoid large mixed refactors in one PR
- favor additive changes + explicit migration notes

---

## License

**TBD** (to be selected before public collaboration).