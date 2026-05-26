# PRE Buddy

> The embodied avatar of your local PRE agent — a small desktop robot that watches over your work, surfaces what PRE knows about itself, and turns a personal language model into something that lives on your desk.

**Cloud assistants live in datacenters. Yours lives on your desk.**

## Status

🚧 Pre-hardware build phase. Implementing protocol, character system, motion engine, and server-side `pre buddy serve` with full host-side test coverage. Hardware (M5Stack StackChan, SKU K151-R) integration begins when device arrives.

See [DESIGN.md](DESIGN.md) for the full design document.

## Hardware

- **Primary:** [M5Stack StackChan Remote Kit (K151-R)](https://shop.m5stack.com/products/m5stackchan-ai-desktop-robot-kit-with-remote-control-esp32-s3) — ESP32-S3 CoreS3 + animatronic body with servos, LEDs, camera, mics
- **Secondary (headless / dev):** bare M5Stack CoreS3 — same firmware, no body

## Repo Layout

```
pre-buddy/
├── DESIGN.md                  Design document (read this first)
├── server/                    Python `pre buddy serve` subcommand
│   ├── pre_buddy/             package (events, serializer, cli)
│   └── tests/                 pytest suite
├── firmware/                  C++ ESP32-S3 firmware
│   ├── core/include/pre_buddy host-testable headers (character, motion, protocol)
│   ├── test/                  host unit tests (cmake + ctest)
│   └── CMakeLists.txt
├── shared/protocol/           wire-protocol spec (single source of truth)
└── docs/                      protocol, embodiment, pairing notes
```

## Build & Test

Firmware host-testable core (no device required):

```bash
cd firmware
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Server:

```bash
cd server
python3 -m venv .venv && . .venv/bin/activate
pip install -e '.[dev]'
pytest -q
```

CI runs both suites on every push (`.github/workflows/ci.yml`).

## License

TBD (Christopher to decide before public collaboration).
