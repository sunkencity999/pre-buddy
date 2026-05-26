// SPDX-License-Identifier: TBD
// PRE Buddy — ESP32-S3 entry point.
//
// Skeleton bring-up: instantiate every HAL driver, wire them into
// RobotLoop, advertise the NUS service, and pump events on the main task.
//
// NOT compiled by the host-test CMake build. Picked up only when this
// directory is opened as an ESP-IDF project (idf.py build).

#include "esp32_ble.h"
#include "esp32_display.h"
#include "esp32_led.h"
#include "esp32_servo.h"

#include "pre_buddy/character.h"
#include "pre_buddy/protocol.h"
#include "pre_buddy/robot_loop.h"

extern "C" {
// ESP-IDF entry point.
void app_main();
}

namespace pb = pre_buddy;
namespace pb_esp = pre_buddy::esp32;

namespace {

// TODO: feed `buf` into a real JSON parser (ArduinoJson or cJSON) and
// translate it into a pb::Event. For now the function exists so the
// shape compiles and reviewers can see where the parser plugs in.
pb::Event decode_event_json(const char* buf, std::size_t n) noexcept {
    (void)buf;
    (void)n;
    return pb::Event{};  // EventKind::Unknown → RobotLoop will idle.
}

}  // namespace

void app_main() {
    pb_esp::Esp32ServoDriver servo;
    pb_esp::Esp32LedDriver led;
    pb_esp::Esp32DisplayDriver display;
    pb_esp::Esp32BleTransport ble;

    servo.init();
    led.init();
    display.init();
    ble.start("pre-buddy");

    pb::RobotLoop loop(pb::Character::Sage, servo, led, display);
    loop.reset_to_idle();

    // Main pump. The ESP-IDF idle task scheduler keeps us cooperative if
    // we yield ~every 10 ms. Real implementation will poll an event group
    // signalled from the BLE RX callback so we don't busy-spin.
    constexpr std::size_t kBufSize = 256;
    char buf[kBufSize];
    while (true) {
        if (ble.has_incoming()) {
            std::size_t n = ble.pop_incoming(buf, kBufSize);
            if (n > 0) {
                pb::Event ev = decode_event_json(buf, n);
                loop.dispatch(ev);
            }
        }
        // TODO: replace busy-wait with vTaskDelay once FreeRTOS headers are linked.
    }
}
