// SPDX-License-Identifier: TBD
// ESP32-S3 IWakeWordDetector implementation — Espressif's ESP-SR engine.
//
// ESP-SR ships with a curated set of wake-word models compiled in at
// build time (configured via menuconfig). Custom phrases require either
// a paid Porcupine licence or running Espressif's online keyword
// training service. For v1 we ship with "hey buddy" and silently fall
// back to it when an unsupported phrase is requested — see the
// host-side MockWakeWordDetector for the same behaviour.
//
// Skeleton — not compiled in host-test CI.

#pragma once

#include <string>

#include "pre_buddy/hal/i_wake_detector.h"

namespace pre_buddy::esp32 {

class Esp32WakeWordDetector : public hal::IWakeWordDetector {
   public:
    Esp32WakeWordDetector() noexcept = default;

    void start(std::string_view phrase,
               hal::WakeWordFn callback,
               void* user_data) noexcept override;

    void stop() noexcept override;
    bool is_running() const noexcept override;
    std::string_view active_phrase() const noexcept override;

    // Feed mono PCM16 (from the mic capture) into the AFE. Call with frames of
    // feed_chunksize() samples; set the mic frame size to match.
    void feed(const std::int16_t* samples, std::size_t num_samples) noexcept;

    // The AFE's required feed chunk size in mono samples (0 until start()).
    std::size_t feed_chunksize() const noexcept;

   private:
    bool running_ = false;
    std::string active_phrase_;
    hal::WakeWordFn callback_ = nullptr;
    void* user_data_ = nullptr;
};

}  // namespace pre_buddy::esp32
