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
#include "esp32_speaker.h"
#include "esp32_mic.h"

#include "pre_buddy/boot_flow.h"
#include "pre_buddy/character.h"
#include "pre_buddy/character_picker.h"
#include "pre_buddy/protocol.h"
#include "pre_buddy/robot_loop.h"

#include <cstdint>
#include <cmath>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "cJSON.h"

#include <cstring>

extern "C" {
// ESP-IDF entry point.
void app_main();
}

namespace pb = pre_buddy;
namespace pb_esp = pre_buddy::esp32;

namespace {

constexpr const char* kTag = "pre-buddy";

pb::Event::Tier tier_from(const cJSON* data, const char* key) noexcept {
    const cJSON* t = cJSON_GetObjectItemCaseSensitive(data, key);
    if (cJSON_IsString(t) && t->valuestring != nullptr) {
        if (std::strcmp(t->valuestring, "standard") == 0) return pb::Event::Tier::Standard;
        if (std::strcmp(t->valuestring, "frontier") == 0) return pb::Event::Tier::Frontier;
    }
    return pb::Event::Tier::Fast;
}

// Parse one NUS JSON line ({"event": "...", "data": {...}}) into a pb::Event.
// Unknown / malformed input yields EventKind::Unknown (RobotLoop idles).
pb::Event decode_event_json(const char* buf, std::size_t n) noexcept {
    pb::Event ev{};
    cJSON* root = cJSON_ParseWithLength(buf, n);
    if (root == nullptr) return ev;

    const cJSON* type = cJSON_GetObjectItemCaseSensitive(root, "event");
    if (cJSON_IsString(type) && type->valuestring != nullptr) {
        ev.kind = pb::parse_event_kind(type->valuestring);
    }

    const cJSON* data = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (cJSON_IsObject(data)) {
        switch (ev.kind) {
            case pb::EventKind::WakeWord: {
                const cJSON* m = cJSON_GetObjectItemCaseSensitive(data, "source_mic");
                if (cJSON_IsString(m) && m->valuestring != nullptr) {
                    if (std::strcmp(m->valuestring, "left") == 0)
                        ev.source_mic = pb::Event::Mic::Left;
                    else if (std::strcmp(m->valuestring, "right") == 0)
                        ev.source_mic = pb::Event::Mic::Right;
                }
                break;
            }
            case pb::EventKind::BgAgentChange:
                ev.tier = tier_from(data, "tier");
                break;
            case pb::EventKind::RouterDecision:
                ev.from_tier = tier_from(data, "from_tier");
                ev.to_tier = tier_from(data, "to_tier");
                break;
            case pb::EventKind::ConfidenceWarning: {
                const cJSON* c = cJSON_GetObjectItemCaseSensitive(data, "confidence");
                const cJSON* t = cJSON_GetObjectItemCaseSensitive(data, "threshold");
                if (cJSON_IsNumber(c)) ev.confidence = static_cast<float>(c->valuedouble);
                if (cJSON_IsNumber(t)) ev.threshold = static_cast<float>(t->valuedouble);
                break;
            }
            case pb::EventKind::ConfidenceSnapshot: {
                const cJSON* c = cJSON_GetObjectItemCaseSensitive(data, "confidence");
                if (cJSON_IsNumber(c)) ev.confidence = static_cast<float>(c->valuedouble);
                break;
            }
            case pb::EventKind::SchedulerUpcoming: {
                const cJSON* mu = cJSON_GetObjectItemCaseSensitive(data, "minutes_until");
                if (cJSON_IsNumber(mu)) ev.minutes_until = static_cast<std::int32_t>(mu->valuedouble);
                break;
            }
            case pb::EventKind::Proximity: {
                const cJSON* d = cJSON_GetObjectItemCaseSensitive(data, "distance_cm");
                if (cJSON_IsNumber(d)) ev.distance_cm = static_cast<float>(d->valuedouble);
                break;
            }
            case pb::EventKind::CharacterSet: {
                const cJSON* ch = cJSON_GetObjectItemCaseSensitive(data, "character");
                if (cJSON_IsString(ch) && ch->valuestring != nullptr) {
                    pb::Character parsed;
                    if (pb::parse_character(ch->valuestring, parsed)) ev.character = parsed;
                }
                break;
            }
            default:
                break;
        }
    }

    cJSON_Delete(root);
    return ev;
}

}  // namespace

void app_main() {
    pb_esp::Esp32ServoDriver servo;
    pb_esp::Esp32LedDriver led;
    pb_esp::Esp32DisplayDriver display;
    pb_esp::Esp32BleTransport ble;
    pb_esp::Esp32SpeakerDriver speaker;
    pb_esp::Esp32MicDriver mic;

    // Order matters: display brings up the shared internal I2C bus (M5GFX);
    // led init enables the 5V boost + VM_EN that power the body; servo init
    // then has power for torque-enable + its bring-up sweep.
    display.init();
    led.init();
    servo.init();

    // Boot chime — a quick two-note rise so we can hear the speaker (AW88298
    // amp + I2S). Generated as 16 kHz mono sine; stop_playback releases I2S1
    // so it's free for the mic later.
    speaker.start_playback(16000);
    {
        constexpr int kSR = 16000;
        constexpr int kN = kSR / 5;  // 200 ms
        static std::int16_t note[kN];
        const auto play = [&](float freq) {
            for (int i = 0; i < kN; ++i) {
                note[i] = static_cast<std::int16_t>(
                    7000.0f * std::sin(2.0f * 3.14159265f * freq * i / kSR));
            }
            speaker.play_frame(note, kN);
        };
        play(660.0f);
        play(880.0f);
    }
    speaker.stop_playback();
    ESP_LOGI(kTag, "boot chime played");

    // Phase 2 mic bring-up: capture continuously and log a peak level each
    // second to verify the mic (clap/talk → the number jumps). The chime
    // above released I2S_NUM_1 via stop_playback, so RX can claim it now.
    mic.start_capture(16000, 320, nullptr, nullptr);

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
    std::uint32_t ticks = 0;            // 10 ms ticks
    std::uint32_t idle_ticks = 0;       // ticks since the last event
    bool idled = false;                 // have we relaxed to the idle pose?
    std::uint32_t next_ambient = 0;     // idle_ticks at which to do the next glance
    int ambient_dir = 1;
    constexpr std::uint32_t kIdleReturn = 800;   // ~8 s of quiet → relax to neutral
    constexpr std::uint32_t kAmbientGap = 1700;  // ~17 s between subtle glances

    while (true) {
        if (ble.has_incoming()) {
            std::size_t n = ble.pop_incoming(buf, kBufSize);
            if (n > 0) {
                pb::Event ev = decode_event_json(buf, n);
                const pb::EmbodimentCommand cmd = loop.dispatch(ev);
                ESP_LOGI(kTag, "event kind=%d -> expr=%d motion=%d led=%d",
                         static_cast<int>(ev.kind),
                         static_cast<int>(cmd.expression),
                         static_cast<int>(cmd.has_motion),
                         static_cast<int>(cmd.led));
                idle_ticks = 0;
                idled = false;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
        ++ticks;
        ++idle_ticks;

        if (ticks % 500 == 0) {  // heartbeat ~every 5 s
            ESP_LOGI(kTag, "alive: uptime ~%us", static_cast<unsigned>(ticks / 100));
        }

        // Graceful shutdown: a power-key long-press blanks the LEDs, cuts the
        // 5V boost + servo power, and powers the AXP2101 off. Poll ~every 200ms.
        if (ticks % 20 == 0 && led.poll_power_off_request()) {
            ESP_LOGI(kTag, "power-key long press → graceful shutdown");
            servo.disable();
            led.shutdown();
        }

        // After a quiet spell, relax everything to the calm idle pose.
        if (!idled && idle_ticks >= kIdleReturn) {
            loop.reset_to_idle();
            idled = true;
            next_ambient = idle_ticks + kAmbientGap;
        }
        // Subtle ambient "looking around": mostly still, a small head glance
        // every ~17 s, alternating sides — the calm alive-but-idle loop.
        if (idled && idle_ticks >= next_ambient) {
            pb::MotionCommand m;
            m.head_x_deg = static_cast<float>(ambient_dir) * 12.0f;
            m.head_y_deg = 45.0f + (ambient_dir > 0 ? 3.0f : -3.0f);
            m.duration_ms = 1500;
            servo.move(m);
            ambient_dir = -ambient_dir;
            next_ambient = idle_ticks + kAmbientGap;
        }
    }
}
