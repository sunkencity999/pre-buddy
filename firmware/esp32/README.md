# ESP32-S3 firmware skeleton

> 🚧 **Bring-up scaffolding.** This subtree is intentionally **not** compiled
> by the root `firmware/CMakeLists.txt` host-test build, and **not** wired
> into CI. It lands ahead of the M5Stack CoreS3 so the wiring is reviewable
> in PR form before silicon arrives.

This directory targets the device-side runtime that talks BLE NUS as a
peripheral, drives the two servos, the LED ring, and the 2" IPS screen,
and consumes `pre.*` events via [`RobotLoop`](../core/include/pre_buddy/robot_loop.h).

## Build target

| Knob | Value |
|---|---|
| Board | M5Stack CoreS3 (ESP32-S3, 16MB Flash, 8MB PSRAM) |
| Framework | ESP-IDF v5.x (NimBLE selected for the BLE stack) |
| C++ standard | C++17 (matches core/) |
| BLE role | Peripheral, advertising the Nordic UART Service |

## Layout

```
esp32/
├── CMakeLists.txt           # top-level ESP-IDF project
├── main/
│   ├── CMakeLists.txt       # idf_component_register(...) for the app
│   ├── main.cpp             # entry point: wires HAL drivers + RobotLoop
│   ├── esp32_servo.h/.cpp   # IServoDriver via LEDC PWM
│   ├── esp32_led.h/.cpp     # ILedDriver via SK6812 / NeoPixel
│   ├── esp32_display.h/.cpp # IDisplayDriver via ILI9342 (CoreS3 panel)
│   └── esp32_ble.h/.cpp     # IBleTransport via NimBLE NUS peripheral
└── partitions.csv           # standard ESP-IDF partition table
```

Each `esp32_*.cpp` file implements the interface from
`firmware/core/include/pre_buddy/hal/i_*.h`. The interfaces are the
contract; the device-side files are the "fill in real hardware" half.

## How `RobotLoop` gets driven

```cpp
// main.cpp shape (see source for the actual code)
Esp32ServoDriver servo;
Esp32LedDriver led;
Esp32DisplayDriver display;
Esp32BleTransport ble;

ble.start("pre-buddy");
RobotLoop loop(Character::Sage, servo, led, display);
loop.reset_to_idle();

while (true) {
    if (ble.has_incoming()) {
        char buf[256];
        size_t n = ble.pop_incoming(buf, sizeof(buf));
        Event ev = decode_event_json({buf, n});  // TODO: ArduinoJson or cJSON
        loop.dispatch(ev);
    }
    vTaskDelay(pdMS_TO_TICKS(10));
}
```

## What's stubbed vs. what works today

| File | State |
|---|---|
| `main.cpp` | Skeleton with the wire-up shape but ESP-IDF API calls TODO |
| `esp32_servo.cpp` | Header + class shape; LEDC PWM config TODO |
| `esp32_led.cpp` | Header + class shape; SK6812 driver call TODO |
| `esp32_display.cpp` | Header + class shape; LovyanGFX/M5Unified call TODO |
| `esp32_ble.cpp` | Header + class shape; NimBLE NUS peripheral TODO |

All five files compile against the existing host-testable interfaces in
`firmware/core/include/pre_buddy/hal/` — the contract is locked in.
What's missing is purely the ESP-IDF API code, which lands once the
board is on the bench.

## When the board arrives

1. Install ESP-IDF v5.x and source `export.sh`.
2. From this directory: `idf.py set-target esp32s3 && idf.py build`.
3. Flash + monitor: `idf.py -p /dev/cu.usbmodem* flash monitor`.
4. Use `nRF Connect` (iOS/Android) to pair as a NUS central and verify
   the device shows up advertising "pre-buddy".
5. Switch the Python server to `pre-buddy serve --transport ble
   --device-name pre-buddy` and confirm round-trip events.
