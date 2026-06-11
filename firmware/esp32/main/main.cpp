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
#include "esp32_wake_word.h"
#include "esp32_audio_in.h"

#include "pre_buddy/boot_flow.h"
#include "pre_buddy/character.h"
#include "pre_buddy/character_picker.h"
#include "pre_buddy/expression.h"
#include "pre_buddy/protocol.h"
#include "pre_buddy/robot_loop.h"

#include <cstdint>
#include <cmath>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
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

// Wake-word plumbing: the esp-sr fetch task fires on_wake_word (off the main
// task), which just sets a flag the main loop turns into a WakeWord event.
// mic_to_afe routes captured mic frames into the detector's AFE *and* into the
// audio-in streamer (which only forwards them once a capture is armed).
volatile bool g_wake_fired = false;
pb_esp::Esp32AudioInStreamer* g_audio_in = nullptr;
void on_wake_word(std::string_view, float, void*) noexcept { g_wake_fired = true; }
void mic_to_afe(const std::int16_t* frame, std::size_t n, void* user) noexcept {
    static_cast<pb_esp::Esp32WakeWordDetector*>(user)->feed(frame, n);
    if (g_audio_in != nullptr) g_audio_in->feed(frame, n);
}

// ── Audio output (PRE's spoken reply) ───────────────────────────────────
// pre.audio.output_* lines arrive over BLE; we base64-decode the PCM16 into a
// PSRAM buffer and, on output_stop, take over the shared I2S bus to play it.
// Speaker and mic share I2S_NUM_1, so mic capture must pause during playback.
constexpr std::size_t kOutCapSamples = 16000 * 20;  // 20 s @ 16 kHz, in PSRAM
std::int16_t* g_out_buf = nullptr;
std::size_t g_out_cap = 0;
std::size_t g_out_len = 0;
std::uint32_t g_out_rate = 16000;
bool g_out_active = false;
int g_out_frames = 0;            // output_frame events accepted this session (debug)
volatile bool g_played = false;  // set after a reply finishes playing

int b64_val(char c) noexcept {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;  // '=' padding or stray char
}

// Decode base64 `src` into `dst` (capacity dstcap bytes). Returns bytes written.
std::size_t b64_decode(const char* src, std::size_t len,
                       std::uint8_t* dst, std::size_t dstcap) noexcept {
    int acc = 0, bits = 0;
    std::size_t o = 0;
    for (std::size_t i = 0; i < len; ++i) {
        const int v = b64_val(src[i]);
        if (v < 0) continue;
        acc = (acc << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (o < dstcap) dst[o++] = static_cast<std::uint8_t>((acc >> bits) & 0xFF);
        }
    }
    return o;
}

// Take over I2S to play the buffered reply, then hand the bus back to the mic.
void play_output(pb_esp::Esp32MicDriver& mic, pb_esp::Esp32SpeakerDriver& speaker,
                 pb_esp::Esp32WakeWordDetector& wake) noexcept {
    if (g_out_buf == nullptr || g_out_len == 0) return;
    ESP_LOGI(kTag, "playing %u samples @ %u Hz", static_cast<unsigned>(g_out_len),
             static_cast<unsigned>(g_out_rate));
    mic.stop_capture();                  // release I2S_NUM_1 (RX)
    speaker.start_playback(g_out_rate);  // claim I2S_NUM_1 (TX)
    constexpr std::size_t kChunk = 512;
    for (std::size_t off = 0; off < g_out_len; off += kChunk) {
        std::size_t m = g_out_len - off;
        if (m > kChunk) m = kChunk;
        speaker.play_frame(g_out_buf + off, m);
    }
    speaker.stop_playback();             // release I2S_NUM_1
    mic.start_capture(16000, wake.feed_chunksize(), mic_to_afe, &wake);  // re-claim RX
    g_out_len = 0;
    g_played = true;  // main loop returns to the calm listening pose
    ESP_LOGI(kTag, "playback done; mic + wake resumed");
}

// If `buf` is a pre.audio.output_* event, handle it (buffer / play) and return
// true. Otherwise return false so the caller routes it as a normal event.
bool handle_output_line(const char* buf, std::size_t n,
                        pb_esp::Esp32MicDriver& mic, pb_esp::Esp32SpeakerDriver& speaker,
                        pb_esp::Esp32WakeWordDetector& wake) noexcept {
    cJSON* root = cJSON_ParseWithLength(buf, n);
    if (root == nullptr) return false;
    const cJSON* ev = cJSON_GetObjectItemCaseSensitive(root, "event");
    if (!cJSON_IsString(ev) || ev->valuestring == nullptr) {
        cJSON_Delete(root);
        return false;
    }
    const char* e = ev->valuestring;
    bool handled = true;
    if (std::strcmp(e, "pre.audio.output_start") == 0) {
        const cJSON* data = cJSON_GetObjectItemCaseSensitive(root, "data");
        const cJSON* sr = data ? cJSON_GetObjectItemCaseSensitive(data, "sample_rate_hz") : nullptr;
        g_out_rate = cJSON_IsNumber(sr) ? static_cast<std::uint32_t>(sr->valuedouble) : 16000;
        g_out_len = 0;
        g_out_frames = 0;
        g_out_active = true;
        ESP_LOGI(kTag, "audio output start (rate %u)", static_cast<unsigned>(g_out_rate));
    } else if (std::strcmp(e, "pre.audio.output_frame") == 0) {
        if (g_out_active && g_out_buf != nullptr) {
            const cJSON* data = cJSON_GetObjectItemCaseSensitive(root, "data");
            const cJSON* d = data ? cJSON_GetObjectItemCaseSensitive(data, "data") : nullptr;
            if (cJSON_IsString(d) && d->valuestring != nullptr) {
                const std::size_t room = (g_out_cap - g_out_len) * sizeof(std::int16_t);
                const std::size_t got = b64_decode(
                    d->valuestring, std::strlen(d->valuestring),
                    reinterpret_cast<std::uint8_t*>(g_out_buf + g_out_len), room);
                g_out_len += got / sizeof(std::int16_t);
                ++g_out_frames;
            }
        }
    } else if (std::strcmp(e, "pre.audio.output_stop") == 0) {
        g_out_active = false;
        ESP_LOGI(kTag, "audio output stop: %d frames recv, %u samples buffered",
                 g_out_frames, static_cast<unsigned>(g_out_len));
        play_output(mic, speaker, wake);
    } else {
        handled = false;  // not an audio output event
    }
    cJSON_Delete(root);
    return handled;
}

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
    pb_esp::Esp32WakeWordDetector wake;
    pb_esp::Esp32AudioInStreamer audio_in;
    g_audio_in = &audio_in;  // mic_to_afe forwards frames here once a capture is armed

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

    // Wake-word: create the AFE first (so we know its feed chunk size), then
    // start the mic feeding mono frames straight into it. The chime above
    // released I2S_NUM_1 via stop_playback, so RX can claim it now. Say
    // "Hi, ESP" to trigger.
    wake.start("hi esp", on_wake_word, nullptr);
    mic.start_capture(16000, wake.feed_chunksize(), mic_to_afe, &wake);

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
    audio_in.start(&ble);  // TX task + msg buffer for pre.audio.input_* events

    // PSRAM buffer for PRE's spoken reply (pre.audio.output_* frames).
    g_out_buf = static_cast<std::int16_t*>(
        heap_caps_malloc(kOutCapSamples * sizeof(std::int16_t), MALLOC_CAP_SPIRAM));
    if (g_out_buf != nullptr) {
        g_out_cap = kOutCapSamples;
    } else {
        ESP_LOGE(kTag, "audio output buffer alloc failed (%u KB PSRAM)",
                 static_cast<unsigned>(kOutCapSamples * sizeof(std::int16_t) / 1024));
    }

    pb::RobotLoop loop(outcome.character, servo, led, display);
    loop.reset_to_idle();

    ESP_LOGI(kTag, "app_main: PRE Buddy stub booted; character=%d, idle face set",
             static_cast<int>(outcome.character));

    // Main pump. Yield ~every 10 ms so the IDLE task runs (and feeds the
    // task watchdog) instead of busy-spinning a core. The real version
    // will block on an event group signalled from the BLE RX callback.
    constexpr std::size_t kBufSize = 2048;  // matches the RX framer; fits audio lines
    char buf[kBufSize];
    std::uint32_t ticks = 0;            // 10 ms ticks
    std::uint32_t idle_ticks = 0;       // ticks since the last event
    bool idled = false;                 // have we relaxed to the idle pose?
    std::uint32_t next_ambient = 0;     // idle_ticks at which to do the next glance
    int ambient_dir = 1;
    constexpr std::uint32_t kIdleReturn = 800;   // ~8 s of quiet → relax to neutral
    constexpr std::uint32_t kAmbientGap = 1700;  // ~17 s between subtle glances

    // "Thinking" animation: from when a question is fully sent until PRE's
    // reply plays, hold the thinking face + a gentle pondering head sway so
    // the user can see it's working (PRE can take ~20 s).
    bool thinking = false;
    std::uint32_t think_tick = 0;
    std::uint32_t next_think_move = 0;
    int think_dir = 1;
    constexpr std::uint32_t kThinkMoveGap = 130;   // ~1.3 s between ponder sways
    constexpr std::uint32_t kThinkTimeout = 6000;  // ~60 s safety cap

    while (true) {
        // Drain every framed line this tick (audio output arrives as a burst
        // of frames). Audio output events are handled inline (buffer/play);
        // everything else routes through the embodiment loop.
        while (ble.has_incoming()) {
            std::size_t n = ble.pop_incoming(buf, kBufSize);
            if (n == 0) break;
            if (handle_output_line(buf, n, mic, speaker, wake)) {
                idle_ticks = 0;
                idled = false;
                continue;
            }
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

        // Wake word ("Hi, ESP") detected on the esp-sr fetch task → react.
        if (g_wake_fired) {
            g_wake_fired = false;
            pb::Event ev;
            ev.kind = pb::EventKind::WakeWord;
            const pb::EmbodimentCommand cmd = loop.dispatch(ev);
            ESP_LOGI(kTag, "wake word -> expr=%d motion=%d",
                     static_cast<int>(cmd.expression), static_cast<int>(cmd.has_motion));
            // Arm a mic-capture session so the question gets streamed to PRE.
            // Only worth it if a central is actually connected to receive it.
            if (ble.is_connected()) audio_in.request_capture();
            thinking = false;  // a fresh question supersedes any pending wait
            idle_ticks = 0;
            idled = false;
        }

        // Finished speaking the reply → relax to the calm listening pose.
        if (g_played) {
            g_played = false;
            thinking = false;
            loop.reset_to_idle();
            idle_ticks = 0;
            idled = true;
            next_ambient = idle_ticks + kAmbientGap;
        }
        // Question fully sent → enter the "thinking" pose until the reply plays.
        if (audio_in.reply_pending()) {
            audio_in.clear_reply_pending();
            thinking = true;
            think_tick = ticks;
            next_think_move = ticks;  // ponder right away
            display.show_expression(loop.character(), pb::Expression::Thinking);
            led.set_color(pb::LedColor::Cyan);
            ESP_LOGI(kTag, "thinking — awaiting PRE reply");
        }
        // Gentle "pondering" sway while thinking; bail after the safety cap.
        // Hold the sway while output frames are streaming in (g_out_active) so
        // servo traffic can't compete with draining the RX burst.
        if (thinking) {
            if (!g_out_active && ticks >= next_think_move) {
                pb::MotionCommand m;
                m.head_x_deg = static_cast<float>(think_dir) * 8.0f;
                m.head_y_deg = 58.0f;  // glance slightly up, as if pondering
                m.duration_ms = 850;
                servo.move(m);
                think_dir = -think_dir;
                next_think_move = ticks + kThinkMoveGap;
            }
            if (ticks - think_tick > kThinkTimeout) {
                thinking = false;
                loop.reset_to_idle();
                idle_ticks = 0;
                idled = true;
                ESP_LOGW(kTag, "thinking timed out — back to idle");
            }
        }

        // After a quiet spell, relax everything to the calm idle pose. Held off
        // while capturing/draining a question or showing the thinking pose.
        if (!thinking && !audio_in.is_busy() && !idled && idle_ticks >= kIdleReturn) {
            loop.reset_to_idle();
            idled = true;
            next_ambient = idle_ticks + kAmbientGap;
        }
        // Subtle ambient "looking around": mostly still, a small head glance
        // every ~17 s, alternating sides — the calm alive-but-idle loop.
        if (!thinking && !audio_in.is_busy() && idled && idle_ticks >= next_ambient) {
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
