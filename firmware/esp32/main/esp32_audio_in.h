// SPDX-License-Identifier: TBD
// PRE Buddy — microphone → BLE audio input streamer.
//
// Turns a wake-word trigger into a `pre.audio.input_*` event stream. The
// conversation is turn-based, so rather than stream in real time (which
// oversubscribes BLE — 16 kHz PCM16 is ~42 KB/s of base64, well past what a
// macOS central sustains, causing dropped/truncated frames), we:
//
//     request_capture()       (on wake, any task)
//        └─ feed() records the next ~6 s of mic audio into a PSRAM buffer,
//           then hands off to a TX task that drains the whole buffer as
//           input_start → input_frame×N → input_stop, losslessly and as fast
//           as the link allows (which may be slower than real time — fine,
//           the user just waits a beat longer for the reply).
//
// feed() runs on the mic capture task and only memcpy's into the buffer, so
// it never blocks audio capture / the wake detector. All BLE sends happen on
// the TX task. Capture and drain are disjoint phases over the shared buffer
// (the semaphore handoff is the memory barrier), so no locking is needed.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace pre_buddy::esp32 {

class Esp32BleTransport;  // send_line target

class Esp32AudioInStreamer {
   public:
    // Allocate the capture buffer + start the TX task. `transport` must
    // outlive this object.
    void start(Esp32BleTransport* transport) noexcept;
    void stop() noexcept;

    // Arm one capture session. Safe to call from any task (e.g. wake cb).
    void request_capture() noexcept { request_ = true; }

    // Feed one mono PCM16 frame from the mic capture task. Accumulates into
    // the capture buffer and triggers the drain when the window fills.
    void feed(const std::int16_t* samples, std::size_t n) noexcept;

    bool is_busy() const noexcept { return state_ != State::Idle; }

    // TX task body — public only so the C task trampoline can reach it.
    void tx_loop() noexcept;

   private:
    enum class State { Idle, Capturing, Sending };

    void emit_start() noexcept;
    void emit_frame(const std::int16_t* samples, std::size_t n) noexcept;
    void emit_stop(const char* reason) noexcept;

    Esp32BleTransport* transport_ = nullptr;
    std::int16_t* cap_buf_ = nullptr;   // PSRAM capture buffer
    std::size_t cap_cap_ = 0;           // capacity in samples
    std::size_t cap_len_ = 0;           // samples captured this session
    SemaphoreHandle_t drain_sig_ = nullptr;
    TaskHandle_t tx_task_ = nullptr;
    volatile bool running_ = false;
    volatile bool request_ = false;
    volatile State state_ = State::Idle;
    std::uint32_t seq_ = 0;
    std::uint32_t start_tick_ = 0;
    std::uint32_t session_n_ = 0;
    // Energy-based end-of-speech: once speech has started, a run of quiet
    // frames ends the capture early (so short questions don't pad to the max).
    bool speech_started_ = false;
    std::size_t silence_run_ = 0;  // samples of trailing quiet since last voice
    bool vad_stop_ = false;        // true if VAD (not the max cap) ended capture
    char sid_[24] = {0};
    // Scratch used only by the TX task (emit_* during drain).
    char line_buf_[1600];
    char b64_[1400];
};

}  // namespace pre_buddy::esp32
