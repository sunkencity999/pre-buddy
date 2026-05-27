# PRE Buddy Docs Index

This directory contains implementation-focused documentation for PRE Buddy.

## Start here

- **Project overview:** [`../README.md`](../README.md)
- **Design spec:** [`../DESIGN.md`](../DESIGN.md)
- **Hands-on tutorial:** [`tutorial.md`](./tutorial.md)

## Core docs

- [`protocol.md`](./protocol.md)
  - How protocol changes flow through schema, generated stubs, server, firmware, and tests.
- [`embodiment.md`](./embodiment.md)
  - Motion budget, character behavior mapping, and safety constraints.
- [`pairing.md`](./pairing.md)
  - Pairing UX and protocol notes (currently planning-stage).
- [`hardware-bringup.md`](./hardware-bringup.md)
  - Stage-by-stage playbook for wiring a real CoreS3 once one is on the bench.

## Protocol source of truth

- Human-readable contract: [`../shared/protocol/events.md`](../shared/protocol/events.md)
- Machine-readable schema: [`../shared/protocol/events.yaml`](../shared/protocol/events.yaml)
- Code generator: [`../tools/generate_protocol_stubs.py`](../tools/generate_protocol_stubs.py)

Regenerate stubs after schema edits:

```bash
python3 tools/generate_protocol_stubs.py
```

## Simulation and scenario tooling

- Scenario files: [`../server/examples/`](../server/examples/)
- Batch matrix script: [`../server/examples/run_all.sh`](../server/examples/run_all.sh)
- Golden snapshots: [`../server/tests/golden/`](../server/tests/golden/)

Useful commands:

```bash
# quick simulation
pre-buddy simulate --playback server/examples/alerts_scenario.jsonl --character sentinel

# all scenarios × all characters
./server/examples/run_all.sh normal

# golden snapshot check
pytest -q server/tests/test_golden_scenarios.py
```

## Testing quick reference

```bash
# server
pytest -q server/tests

# firmware host tests
cd firmware
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Updating docs

When behavior changes, update docs and tests in the same commit:

1. `shared/protocol/events.md` and `events.yaml` (if event contract changed)
2. regenerate stubs (`tools/generate_protocol_stubs.py`)
3. update implementation docs (`protocol.md`, `embodiment.md`, `tutorial.md`)
4. update/add tests and golden snapshots

Keeping docs and behavior synchronized is part of the quality bar for this repo.
