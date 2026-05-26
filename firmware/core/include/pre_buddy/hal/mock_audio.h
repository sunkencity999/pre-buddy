// SPDX-License-Identifier: TBD
// Host-testable mocks for the audio HAL trio (mic / speaker / wake).
//
// Each mock records every call and exposes test-only knobs to drive
// callbacks deterministically. No threading, no real audio.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "pre_buddy/hal/i_mic.h"
#include "pre_buddy/hal/i_speaker.h"
#include "pre_buddy/hal/i_wake_detector.h"

namespace pre_buddy::hal {

class MockMicDriver : public IMicDriver {
   public:
    bool started = false;
    std::uint32_t last_sample_rate_hz = 0;
    std::size_t last_frame_size_samples = 0;
    MicFrameFn callback = nullptr;
    void* callback_user_data = nullptr;
    int start_calls = 0;
    int stop_calls = 0;

    void start_capture(std::uint32_t sample_rate_hz,
                       std::size_t frame_size_samples,
                       MicFrameFn cb,
                       void* user_data) noexcept override {
        ++start_calls;
        last_sample_rate_hz = sample_rate_hz;
        last_frame_size_samples = frame_size_samples;
        callback = cb;
        callback_user_data = user_data;
        started = true;
    }

    void stop_capture() noexcept override {
        ++stop_calls;
        started = false;
    }

    bool is_capturing() const noexcept override { return started; }

    // Test helper: synthesise a captured frame.
    void inject_frame(const std::int16_t* samples, std::size_t n) noexcept {
        if (callback) callback(samples, n, callback_user_data);
    }
};


class MockSpeakerDriver : public ISpeakerDriver {
   public:
    bool playing = false;
    std::uint32_t last_sample_rate_hz = 0;
    int start_calls = 0;
    int stop_calls = 0;
    std::vector<std::int16_t> samples_received;
    // Returned by play_frame() — tests can lower this to simulate a
    // backpressured ring buffer that only accepts part of a frame.
    std::size_t accept_limit_per_call = static_cast<std::size_t>(-1);

    void start_playback(std::uint32_t sample_rate_hz) noexcept override {
        ++start_calls;
        last_sample_rate_hz = sample_rate_hz;
        playing = true;
    }

    std::size_t play_frame(const std::int16_t* samples,
                           std::size_t num_samples) noexcept override {
        if (!playing) return 0;
        std::size_t n = num_samples;
        if (n > accept_limit_per_call) n = accept_limit_per_call;
        samples_received.insert(samples_received.end(),
                                samples, samples + n);
        return n;
    }

    void stop_playback() noexcept override {
        ++stop_calls;
        playing = false;
    }

    bool is_playing() const noexcept override { return playing; }
};


class MockWakeWordDetector : public IWakeWordDetector {
   public:
    bool running = false;
    std::string requested_phrase;
    std::string active_phrase_;
    WakeWordFn callback = nullptr;
    void* callback_user_data = nullptr;
    int start_calls = 0;
    int stop_calls = 0;
    // Test hook: when start() is called with an unsupported phrase the
    // mock falls back to this default, mirroring ESP-SR's behaviour.
    std::vector<std::string> supported_phrases = {"hey buddy"};

    void start(std::string_view phrase,
               WakeWordFn cb,
               void* user_data) noexcept override {
        ++start_calls;
        requested_phrase.assign(phrase);
        bool supported = false;
        for (const auto& p : supported_phrases) {
            if (p == requested_phrase) { supported = true; break; }
        }
        active_phrase_ = supported ? requested_phrase : "hey buddy";
        callback = cb;
        callback_user_data = user_data;
        running = true;
    }

    void stop() noexcept override {
        ++stop_calls;
        running = false;
        active_phrase_.clear();
    }

    bool is_running() const noexcept override { return running; }

    std::string_view active_phrase() const noexcept override {
        return active_phrase_;
    }

    // Test helper: pretend the detector fired.
    void fire(float confidence) noexcept {
        if (callback) callback(active_phrase_, confidence, callback_user_data);
    }
};

}  // namespace pre_buddy::hal
