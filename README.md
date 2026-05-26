# PRE Buddy

> **The embodied avatar of your local PRE agent.**
> A desktop robot that surfaces PRE's introspection (background agents, routing, confidence, memory, scheduler, tools) and turns your local model into something you can see and feel on your desk.

**Cloud assistants live in datacenters. Yours lives on your desk.**

---

## Table of Contents

- [What PRE Buddy is](#what-pre-buddy-is)
- [Current status](#current-status)
- [Hardware targets](#hardware-targets)
- [Repository map](#repository-map)
- [Quickstart (5 minutes)](#quickstart-5-minutes)
- [Tutorial: run scenarios and inspect robot behavior](#tutorial-run-scenarios-and-inspect-robot-behavior)
- [Extended tutorial](#extended-tutorial)
- [Core concepts](#core-concepts)
- [Protocol workflow](#protocol-workflow)
- [Testing and quality gates](#testing-and-quality-gates)
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

🚧 **Pre-hardware build phase (active).**

Implemented today:

- Shared protocol contract (`shared/protocol/events.md` + `events.yaml`)
- Generated protocol stubs (Python + C++) with CI drift check
- Server package + CLI (`pre-buddy`)
- Mock BLE session and event pump (`pre-buddy serve`)
- Software robot simulator (`pre-buddy simulate`)
- Scenario examples + matrix generator (`server/examples/run_all.sh`)
- Golden behavior snapshots to catch regressions
- Host-side test coverage (Python + C++ core)

Not yet implemented:

- Real BLE/NUS peripheral transport
- ESP32 hardware driver integration (servos, LEDs, mics, camera)
- On-device UI and physical calibration

---

## Hardware targets

- **Primary (flagship):** [M5Stack StackChan Remote Kit (K151-R)](https://shop.m5stack.com/products/m5stackchan-ai-desktop-robot-kit-with-remote-control-esp32-s3)
  - CoreS3 (ESP32-S3) + animatronic body (servos, LEDs, camera, mics)
- **Secondary (headless/dev):** bare CoreS3
  - same protocol and logic, no body

One firmware architecture, two physical forms.

---

## Repository map

```text
pre-buddy/
├── DESIGN.md                         Product and architecture spec
├── docs/
│   ├── protocol.md                   Protocol implementation notes
│   ├── embodiment.md                 Motion/LED behavior notes
│   └── pairing.md                    Pairing flow notes
├── shared/protocol/
│   ├── events.md                     Human-readable protocol contract
│   └── events.yaml                   Machine-readable schema source
├── tools/
│   └── generate_protocol_stubs.py    Generates Python/C++ protocol stubs
├── server/
│   ├── pre_buddy/
│   │   ├── events.py                 Typed event model
│   │   ├── serializer.py             JSON-lines encode/decode
│   │   ├── transport.py              Mock BLE session
│   │   ├── pump.py                   Outbound event queue
│   │   ├── serve.py                  Server runtime skeleton
│   │   ├── mock_robot.py             Behavior simulator logic
│   │   ├── simulate.py               Timeline renderers (text/json/csv)
│   │   └── cli.py                    `pre-buddy` CLI
│   ├── examples/
│   │   ├── *_scenario.jsonl          Playback scenarios
│   │   └── run_all.sh                Batch scenario generator
│   └── tests/
│       ├── golden/*.json             Snapshot expectations
│       └── test_*.py                 pytest suite
└── firmware/
    ├── core/include/pre_buddy/       Host-testable C++ core
    ├── test/                          C++ tests
    └── CMakeLists.txt
```

---

## Quickstart (5 minutes)

From repo root:

```bash
# 1) create dev venv
python3 -m venv .venv
. .venv/bin/activate

# 2) install server package + test deps
pip install -e server[dev]

# 3) run tests
pytest -q server/tests

# 4) run mock server demo (prints outbound JSON-lines)
pre-buddy serve --demo

# 5) simulate robot responses for a scenario
pre-buddy simulate \
  --playback server/examples/alerts_scenario.jsonl \
  --character sentinel
```

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

---

## Core concepts

### Character profiles

Three profiles are implemented in the design/model:

- **Sage** (calm, deliberate)
- **Sprout** (curious, energetic)
- **Sentinel** (alert, minimal)

Character selection changes expression style (timing, colors, motion behavior), not capability.

### Severity profiles

Simulation supports:

- `quiet` — reduce non-critical movement
- `normal` — baseline
- `loud` — more expressive for alert-heavy contexts

### Motion safety invariants

- Y-axis clamp: **10°..80°**
- X-axis rate-limited in firmware core
- Error responses are red + still (no shaking)

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

- event validation/hydration
- serializer behavior
- mock transport + event pump
- CLI behavior
- simulator logic
- golden scenario snapshots

### Firmware host tests

```bash
cd firmware
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Covers:

- C++ character profiles
- motion safety/rate limiting
- protocol event parsing
- event → embodiment mapping

### CI

GitHub Actions (`.github/workflows/ci.yml`) runs:

- Python tests (3.10/3.11/3.12)
- generated-protocol drift check
- C++ host build + ctest

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

Near-term:

1. Real BLE/NUS transport in `serve`
2. ESP32 HAL integrations (servos, LEDs, display, mic/camera)
3. UI simulator pass (panel rendering + response overlays)
4. Device calibration profiles (motion/servo limits per hardware variance)

Longer-term:

- live PRE event bus integration
- on-device pairing UX parity
- voice and vision end-to-end on hardware

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