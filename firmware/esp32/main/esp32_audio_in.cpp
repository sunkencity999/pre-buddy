// SPDX-License-Identifier: TBD
// PRE Buddy — microphone → BLE audio input streamer (see header).

#include "esp32_audio_in.h"

#include <cstdio>
#include <cstring>

#include "esp_heap_caps.h"
#include "esp_log.h"

#include "esp32_ble.h"  // Esp32BleTransport::send_line

namespace pre_buddy::esp32 {
namespace {

constexpr const char* TAG = "pre-buddy-audioin";
constexpr std::uint32_t kSampleRate = 16000;
constexpr std::uint32_t kCaptureMs = 6000;             // MAX listen window (safety cap)
constexpr std::size_t kChunkSamples = 512;             // PCM samples per frame line
constexpr std::size_t kCapSamples = kSampleRate * kCaptureMs / 1000;  // 96000
// End-of-speech (VAD-ish): once a frame peaks above kSpeechPeak we're hearing
// speech; after kSilenceSamples of trailing quiet we end the capture early.
constexpr std::int32_t kSpeechPeak = 3500;             // |sample| above this = voice
constexpr std::size_t kSilenceSamples = kSampleRate * 900 / 1000;  // 0.9 s of quiet
constexpr std::uint32_t kMinCaptureMs = 500;           // never end before this

const char kB64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// Standard padded base64. dst must hold >= 4*((n+2)/3) bytes.
std::size_t b64_encode(char* dst, const unsigned char* src, std::size_t n) {
    std::size_t o = 0, i = 0;
    for (; i + 3 <= n; i += 3) {
        const unsigned v = (static_cast<unsigned>(src[i]) << 16) |
                           (static_cast<unsigned>(src[i + 1]) << 8) | src[i + 2];
        dst[o++] = kB64[(v >> 18) & 0x3F];
        dst[o++] = kB64[(v >> 12) & 0x3F];
        dst[o++] = kB64[(v >> 6) & 0x3F];
        dst[o++] = kB64[v & 0x3F];
    }
    const std::size_t rem = n - i;
    if (rem == 1) {
        const unsigned v = static_cast<unsigned>(src[i]) << 16;
        dst[o++] = kB64[(v >> 18) & 0x3F];
        dst[o++] = kB64[(v >> 12) & 0x3F];
        dst[o++] = '=';
        dst[o++] = '=';
    } else if (rem == 2) {
        const unsigned v = (static_cast<unsigned>(src[i]) << 16) |
                           (static_cast<unsigned>(src[i + 1]) << 8);
        dst[o++] = kB64[(v >> 18) & 0x3F];
        dst[o++] = kB64[(v >> 12) & 0x3F];
        dst[o++] = kB64[(v >> 6) & 0x3F];
        dst[o++] = '=';
    }
    return o;
}

void tx_trampoline(void* arg) {
    static_cast<Esp32AudioInStreamer*>(arg)->tx_loop();
    vTaskDelete(nullptr);
}

}  // namespace

void Esp32AudioInStreamer::start(Esp32BleTransport* transport) noexcept {
    if (running_) return;
    transport_ = transport;
    cap_cap_ = kCapSamples;
    cap_buf_ = static_cast<std::int16_t*>(
        heap_caps_malloc(cap_cap_ * sizeof(std::int16_t), MALLOC_CAP_SPIRAM));
    if (cap_buf_ == nullptr) {
        ESP_LOGE(TAG, "capture buffer alloc failed (%u samples in PSRAM)",
                 static_cast<unsigned>(cap_cap_));
        return;
    }
    drain_sig_ = xSemaphoreCreateBinary();
    if (drain_sig_ == nullptr) {
        ESP_LOGE(TAG, "semaphore alloc failed");
        heap_caps_free(cap_buf_);
        cap_buf_ = nullptr;
        return;
    }
    running_ = true;
    xTaskCreate(tx_trampoline, "audiotx", 4096, this, 5, &tx_task_);
    ESP_LOGI(TAG, "audio-in streamer started (%u ms window, %u KB PSRAM)",
             static_cast<unsigned>(kCaptureMs),
             static_cast<unsigned>(cap_cap_ * sizeof(std::int16_t) / 1024));
}

void Esp32AudioInStreamer::stop() noexcept {
    if (!running_) return;
    running_ = false;
    if (drain_sig_ != nullptr) xSemaphoreGive(drain_sig_);  // unblock tx task
    for (int i = 0; i < 50 && tx_task_ != nullptr; ++i) vTaskDelay(pdMS_TO_TICKS(10));
}

void Esp32AudioInStreamer::feed(const std::int16_t* samples,
                                std::size_t n) noexcept {
    if (!running_ || samples == nullptr || n == 0 || cap_buf_ == nullptr) return;

    if (state_ == State::Idle) {
        if (!request_) return;
        request_ = false;
        reply_pending_ = false;  // new question supersedes any prior wait
        cap_len_ = 0;
        speech_started_ = false;
        silence_run_ = 0;
        vad_stop_ = false;
        start_tick_ = xTaskGetTickCount();
        std::snprintf(sid_, sizeof(sid_), "buddy-%lu",
                      static_cast<unsigned long>(++session_n_));
        state_ = State::Capturing;
        ESP_LOGI(TAG, "capture start (session %s)", sid_);
    }

    if (state_ == State::Capturing) {
        const std::size_t room = cap_cap_ - cap_len_;
        const std::size_t take = n < room ? n : room;
        std::memcpy(cap_buf_ + cap_len_, samples, take * sizeof(std::int16_t));
        cap_len_ += take;

        // Peak-track this frame for end-of-speech detection.
        std::int32_t peak = 0;
        for (std::size_t i = 0; i < take; ++i) {
            std::int32_t a = samples[i];
            if (a < 0) a = -a;
            if (a > peak) peak = a;
        }
        if (peak > kSpeechPeak) {
            speech_started_ = true;
            silence_run_ = 0;
        } else if (speech_started_) {
            silence_run_ += take;
        }

        const std::uint32_t elapsed =
            (xTaskGetTickCount() - start_tick_) * portTICK_PERIOD_MS;
        const bool vad_done = speech_started_ && silence_run_ >= kSilenceSamples &&
                              elapsed >= kMinCaptureMs;
        const bool maxed = cap_len_ >= cap_cap_ || elapsed >= kCaptureMs;
        if (vad_done || maxed) {
            vad_stop_ = vad_done;
            state_ = State::Sending;  // hand the buffer to the TX task
            xSemaphoreGive(drain_sig_);
            ESP_LOGI(TAG, "capture done (%u samples, %u ms, %s) — draining",
                     static_cast<unsigned>(cap_len_), static_cast<unsigned>(elapsed),
                     vad_done ? "vad_silence" : "timeout");
        }
    }
    // State::Sending — TX task owns cap_buf_; drop frames until it's Idle again.
}

void Esp32AudioInStreamer::tx_loop() noexcept {
    while (running_) {
        if (xSemaphoreTake(drain_sig_, pdMS_TO_TICKS(200)) != pdTRUE) continue;
        if (!running_) break;

        const std::size_t total = cap_len_;
        seq_ = 0;
        emit_start();
        for (std::size_t off = 0; off < total && running_; off += kChunkSamples) {
            std::size_t chunk = total - off;
            if (chunk > kChunkSamples) chunk = kChunkSamples;
            emit_frame(cap_buf_ + off, chunk);
        }
        emit_stop(vad_stop_ ? "vad_silence" : "timeout");
        ESP_LOGI(TAG, "drained %u frames (%u samples)",
                 static_cast<unsigned>(seq_), static_cast<unsigned>(total));
        reply_pending_ = true;  // question sent → main loop shows "thinking"
        state_ = State::Idle;   // allow the next capture to arm
    }
    tx_task_ = nullptr;
}

void Esp32AudioInStreamer::emit_start() noexcept {
    const int k = std::snprintf(
        line_buf_, sizeof(line_buf_),
        "{\"event\":\"pre.audio.input_start\",\"data\":{\"session_id\":\"%s\","
        "\"sample_rate_hz\":%u,\"codec\":\"pcm16\"}}",
        sid_, static_cast<unsigned>(kSampleRate));
    if (k > 0 && static_cast<std::size_t>(k) < sizeof(line_buf_))
        transport_->send_line(std::string_view(line_buf_, static_cast<std::size_t>(k)));
}

void Esp32AudioInStreamer::emit_frame(const std::int16_t* samples,
                                      std::size_t n) noexcept {
    const std::size_t olen = b64_encode(
        b64_, reinterpret_cast<const unsigned char*>(samples),
        n * sizeof(std::int16_t));
    const int k = std::snprintf(
        line_buf_, sizeof(line_buf_),
        "{\"event\":\"pre.audio.input_frame\",\"data\":{\"session_id\":\"%s\","
        "\"seq\":%u,\"data\":\"%.*s\"}}",
        sid_, static_cast<unsigned>(seq_), static_cast<int>(olen), b64_);
    if (k > 0 && static_cast<std::size_t>(k) < sizeof(line_buf_))
        transport_->send_line(std::string_view(line_buf_, static_cast<std::size_t>(k)));
    ++seq_;
}

void Esp32AudioInStreamer::emit_stop(const char* reason) noexcept {
    const int k = std::snprintf(
        line_buf_, sizeof(line_buf_),
        "{\"event\":\"pre.audio.input_stop\",\"data\":{\"session_id\":\"%s\","
        "\"reason\":\"%s\"}}",
        sid_, reason);
    if (k > 0 && static_cast<std::size_t>(k) < sizeof(line_buf_))
        transport_->send_line(std::string_view(line_buf_, static_cast<std::size_t>(k)));
}

}  // namespace pre_buddy::esp32
