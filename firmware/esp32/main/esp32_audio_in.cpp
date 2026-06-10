// SPDX-License-Identifier: TBD
// PRE Buddy — microphone → BLE audio input streamer (see header).

#include "esp32_audio_in.h"

#include <cstdio>
#include <cstring>

#include "esp_log.h"

#include "esp32_ble.h"  // Esp32BleTransport::send_line

namespace pre_buddy::esp32 {
namespace {

constexpr const char* TAG = "pre-buddy-audioin";
constexpr std::size_t kMaxFrame = 512;        // matches mic kMaxFrame
constexpr std::uint32_t kCaptureMs = 6000;    // fixed listen window per wake
constexpr std::size_t kMsgBufBytes = 16384;   // ~10 frame lines of slack
constexpr std::size_t kRxBufBytes = 1600;     // one line + length prefix

const char kB64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// Standard padded base64. dst must hold >= 4*((n+2)/3) bytes. Returns the
// number of characters written. (Avoids an mbedtls component dependency for
// what is a dozen lines of arithmetic.)
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
    msgbuf_ = xMessageBufferCreate(kMsgBufBytes);
    if (msgbuf_ == nullptr) {
        ESP_LOGE(TAG, "message buffer alloc failed (%u B)", (unsigned)kMsgBufBytes);
        return;
    }
    running_ = true;
    xTaskCreate(tx_trampoline, "audiotx", 4096, this, 5, &tx_task_);
    ESP_LOGI(TAG, "audio-in streamer started (window %u ms)", (unsigned)kCaptureMs);
}

void Esp32AudioInStreamer::stop() noexcept {
    if (!running_) return;
    running_ = false;
    for (int i = 0; i < 30 && tx_task_ != nullptr; ++i) vTaskDelay(pdMS_TO_TICKS(10));
}

void Esp32AudioInStreamer::tx_loop() noexcept {
    // Static (not stack) so the 4 KB task stack stays roomy for send_line.
    static unsigned char rxbuf[kRxBufBytes];
    while (running_) {
        const std::size_t len = xMessageBufferReceive(
            msgbuf_, rxbuf, sizeof(rxbuf), pdMS_TO_TICKS(200));
        if (len > 0 && transport_ != nullptr) {
            transport_->send_line(
                std::string_view(reinterpret_cast<char*>(rxbuf), len));
        }
    }
    tx_task_ = nullptr;
}

void Esp32AudioInStreamer::enqueue(const char* line, std::size_t len) noexcept {
    if (msgbuf_ == nullptr || len == 0) return;
    // 0 timeout: if the link can't keep up, drop the frame rather than block
    // the mic task (which would drop real audio + starve the wake detector).
    xMessageBufferSend(msgbuf_, line, len, 0);
}

void Esp32AudioInStreamer::emit_start() noexcept {
    const int k = std::snprintf(
        line_buf_, sizeof(line_buf_),
        "{\"event\":\"pre.audio.input_start\",\"data\":{\"session_id\":\"%s\","
        "\"sample_rate_hz\":16000,\"codec\":\"pcm16\"}}",
        sid_);
    if (k > 0 && static_cast<std::size_t>(k) < sizeof(line_buf_))
        enqueue(line_buf_, static_cast<std::size_t>(k));
}

void Esp32AudioInStreamer::emit_frame(const std::int16_t* samples,
                                      std::size_t n) noexcept {
    if (n > kMaxFrame) n = kMaxFrame;
    const std::size_t olen = b64_encode(
        b64_, reinterpret_cast<const unsigned char*>(samples),
        n * sizeof(std::int16_t));
    const int k = std::snprintf(
        line_buf_, sizeof(line_buf_),
        "{\"event\":\"pre.audio.input_frame\",\"data\":{\"session_id\":\"%s\","
        "\"seq\":%u,\"data\":\"%.*s\"}}",
        sid_, static_cast<unsigned>(seq_), static_cast<int>(olen), b64_);
    if (k > 0 && static_cast<std::size_t>(k) < sizeof(line_buf_))
        enqueue(line_buf_, static_cast<std::size_t>(k));
    ++seq_;
}

void Esp32AudioInStreamer::emit_stop(const char* reason) noexcept {
    const int k = std::snprintf(
        line_buf_, sizeof(line_buf_),
        "{\"event\":\"pre.audio.input_stop\",\"data\":{\"session_id\":\"%s\","
        "\"reason\":\"%s\"}}",
        sid_, reason);
    if (k > 0 && static_cast<std::size_t>(k) < sizeof(line_buf_))
        enqueue(line_buf_, static_cast<std::size_t>(k));
}

void Esp32AudioInStreamer::feed(const std::int16_t* samples,
                                std::size_t n) noexcept {
    if (!running_ || samples == nullptr || n == 0) return;

    if (state_ == State::Idle) {
        if (!request_) return;
        request_ = false;
        std::snprintf(sid_, sizeof(sid_), "buddy-%lu",
                      static_cast<unsigned long>(++session_n_));
        seq_ = 0;
        emit_start();
        start_tick_ = xTaskGetTickCount();
        state_ = State::Streaming;
        ESP_LOGI(TAG, "capture start (session %s)", sid_);
    }

    if (state_ == State::Streaming) {
        emit_frame(samples, n);
        const std::uint32_t elapsed =
            (xTaskGetTickCount() - start_tick_) * portTICK_PERIOD_MS;
        if (elapsed >= kCaptureMs) {
            emit_stop("timeout");
            state_ = State::Idle;
            ESP_LOGI(TAG, "capture stop (%u frames, %u ms)",
                     static_cast<unsigned>(seq_), static_cast<unsigned>(elapsed));
        }
    }
}

}  // namespace pre_buddy::esp32
