// SPDX-License-Identifier: TBD
// PRE Buddy — ESP32-S3 entry point.
//
// Skeleton bring-up: instantiate every HAL driver, wire them into
// RobotLoop, advertise the NUS service, and pump events on the main task.
//
// NOT compiled by the host-test CMake build. Picked up only when this
// directory is opened as an ESP-IDF project (idf.py build).

#include "esp32_ble.h"
#include "esp32_character_store.h"
#include "esp32_display.h"
#include "esp32_led.h"
#include "esp32_servo.h"

#include "pre_buddy/boot_flow.h"
#include "pre_buddy/character.h"
#include "pre_buddy/character_picker.h"
#include "pre_buddy/protocol.h"
#include "pre_buddy/robot_loop.h"

#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

extern "C" {
// ESP-IDF entry point.
void app_main();
}

namespace pb = pre_buddy;
namespace pb_esp = pre_buddy::esp32;

namespace {

constexpr const char* kTag = "pre-buddy";

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

    pb_esp::Esp32NvsCharacterStore store;
    store.init();

    // Boot flow: load the saved character if there is one; otherwise
    // walk the user through the first-boot picker. The picker state
    // machine is host-testable — only the input loop (short-press =
    // next, long-press = confirm) waits on the CoreS3 buttons.
    auto outcome = pb::determine_initial_character(
        store,
        [&display](pb::CharacterPicker& picker) {
            // TODO when CoreS3 lands: replace this with the real input
            // loop. The shape is:
            //
            //   while (!picker.is_confirmed()) {
            //       display.show_character(picker.current());
            //       if (short_press()) picker.next();
            //       if (long_press())  return picker.confirm();
            //       vTaskDelay(pdMS_TO_TICKS(50));
            //   }
            //   return picker.confirm();
            (void)display;
            return picker.confirm();  // default = Sage
        });

    ble.start("pre-buddy");

    pb::RobotLoop loop(outcome.character, servo, led, display);
    loop.reset_to_idle();

    ESP_LOGI(kTag, "app_main: PRE Buddy stub booted; character=%d, idle face set",
             static_cast<int>(outcome.character));

    // Main pump. Yield ~every 10 ms so the IDLE task runs (and feeds the
    // task watchdog) instead of busy-spinning a core. The real version
    // will block on an event group signalled from the BLE RX callback.
    constexpr std::size_t kBufSize = 256;
    char buf[kBufSize];
    std::uint32_t ticks = 0;
    while (true) {
        if (ble.has_incoming()) {
            std::size_t n = ble.pop_incoming(buf, kBufSize);
            if (n > 0) {
                pb::Event ev = decode_event_json(buf, n);
                loop.dispatch(ev);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
        if (++ticks % 500 == 0) {  // heartbeat ~every 5 s
            ESP_LOGI(kTag, "alive: uptime ~%us", static_cast<unsigned>(ticks / 100));
        }
    }
}
