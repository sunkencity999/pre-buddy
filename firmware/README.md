# PRE Buddy firmware

C++17 firmware for the M5Stack StackChan (CoreS3) and headless CoreS3 builds.

## Layers

- **Host-testable core** (`core/include/pre_buddy/`):
  - `character.h` — Sage / Sprout / Sentinel profiles
  - `motion.h`    — safety clamp + rate limiter
  - `protocol.h`  — event kinds + event→embodiment mapping
- **Host tests** (`test/`): zero-dep harness, runs under `ctest` on Linux/macOS.
- **Device code** (TBD): BLE NUS peripheral, panels, voice, vision. Lands once
  the StackChan kit arrives. Will use PlatformIO or ESP-IDF with the same
  `core/` headers reused unchanged.

## Build host tests

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Direct binary: `./build/pre_buddy_host_tests`.
