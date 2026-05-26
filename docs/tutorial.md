# PRE Buddy Tutorial (Pre-Hardware)

This tutorial walks through the current software-only development loop.

## Prerequisites

- Python 3.10+
- CMake + C++ compiler (for firmware host tests)

## Setup

From repo root:

```bash
python3 -m venv .venv
. .venv/bin/activate
pip install -e server[dev]
```

## Step 1 — Run full tests

```bash
pytest -q server/tests

cd firmware
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
cd ..
```

## Step 2 — Inspect mock server outbound flow

```bash
pre-buddy serve --demo
```

This replays a deterministic event sequence through the mock BLE session and prints outbound JSON-lines.

## Step 3 — Simulate robot behavior for a scenario

```bash
pre-buddy simulate \
  --playback server/examples/alerts_scenario.jsonl \
  --character sentinel \
  --severity normal
```

Try multiple profiles:

```bash
pre-buddy simulate --playback server/examples/alerts_scenario.jsonl --character sage --severity quiet
pre-buddy simulate --playback server/examples/alerts_scenario.jsonl --character sprout --severity loud
```

## Step 4 — Export results for analysis

CSV:

```bash
pre-buddy simulate \
  --playback server/examples/daily_flow_scenario.jsonl \
  --character sprout \
  --severity normal \
  --format csv \
  --out /tmp/daily_sprout.csv
```

JSON:

```bash
pre-buddy simulate \
  --playback server/examples/daily_flow_scenario.jsonl \
  --character sprout \
  --severity normal \
  --format json \
  --out /tmp/daily_sprout.json
```

## Step 5 — Generate full comparison matrix

```bash
./server/examples/run_all.sh normal
```

Outputs are generated under `server/examples/output/` (gitignored).

## Step 6 — Golden snapshot confidence check

```bash
pytest -q server/tests/test_golden_scenarios.py
```

If this fails, behavior changed. Confirm whether it is intentional before updating snapshots.

## Step 7 — Add your own scenario

1. Create `server/examples/my_scenario.jsonl`
2. Use valid `pre.*` event names and payloads from `shared/protocol/events.md`
3. Simulate:
   ```bash
   pre-buddy simulate --playback server/examples/my_scenario.jsonl --character sentinel
   ```
4. If needed, add a golden snapshot test for regression protection.

## Next

When hardware arrives, this exact loop remains useful:

- keep the simulator as a reference model,
- validate hardware behavior against expected timeline outputs,
- tighten servo/LED timing using measured diffs.