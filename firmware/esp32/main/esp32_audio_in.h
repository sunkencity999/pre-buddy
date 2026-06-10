// SPDX-License-Identifier: TBD
// PRE Buddy — microphone → BLE audio input streamer.
//
// Turns a wake-word trigger into a `pre.audio.input_*` event stream:
//
//     request_capture()            (on wake, any task)
//        └─ feed() sees the request on the next mic frame and emits
//           input_start, then base64 PCM16 input_frame events for a fixed
//           window, then input_stop("timeout").
//
// feed() runs on the mic capture task and must stay quick, so it only builds
// JSON lines and hands them to a FreeRTOS message buffer. A dedicated TX task
// drains the buffer and calls Esp32BleTransport::send_line — that's where the
// (potentially slow / back-pressured) BLE notifies happen, so a congested
// link drops frames instead of stalling audio capture.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "freertos/FreeRTOS.h"
#include "freertos/message_buffer.h"
#include "freertos/task.h"

namespace pre_buddy::esp32 {

class Esp32BleTransport;  // send_line target

class Esp32AudioInStreamer {
   public:
    // Start the TX task + message buffer. `transport` must outlive this.
    void start(Esp32BleTransport* transport) noexcept;
    void stop() noexcept;

    // Arm one capture session. Safe to call from any task (e.g. wake cb).
    void request_capture() noexcept { request_ = true; }

    // Feed one mono PCM16 frame from the mic capture task. Drives the
    // start/stream/stop state machine and enqueues event lines.
    void feed(const std::int16_t* samples, std::size_t n) noexcept;

    bool is_streaming() const noexcept { return state_ == State::Streaming; }

    // TX task body — public only so the C task trampoline can reach it.
    void tx_loop() noexcept;

   private:
    enum class State { Idle, Streaming };

    void enqueue(const char* line, std::size_t len) noexcept;
    void emit_start() noexcept;
    void emit_frame(const std::int16_t* samples, std::size_t n) noexcept;
    void emit_stop(const char* reason) noexcept;

    Esp32BleTransport* transport_ = nullptr;
    MessageBufferHandle_t msgbuf_ = nullptr;
    TaskHandle_t tx_task_ = nullptr;
    volatile bool running_ = false;
    volatile bool request_ = false;
    State state_ = State::Idle;
    std::uint32_t seq_ = 0;
    std::uint32_t start_tick_ = 0;
    std::uint32_t session_n_ = 0;
    char sid_[24] = {0};
    // Scratch used only on the mic task (feed() and its emit_* helpers).
    char line_buf_[1600];
    char b64_[1400];
};

}  // namespace pre_buddy::esp32
