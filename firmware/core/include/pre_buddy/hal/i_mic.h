// SPDX-License-Identifier: TBD
// PRE Buddy — microphone driver interface.
//
// The CoreS3 has an on-package PDM microphone exposed via I2S. The
// concrete ESP-IDF driver is in firmware/esp32/main/esp32_mic.cpp.
// Host tests use ``MockMicDriver`` from hal/mock_audio.h.

#pragma once

#include <cstddef>
#include <cstdint>

namespace pre_buddy::hal {

// Called by the driver with one captured frame of PCM16 mono samples.
// ``frame`` points at ``num_samples`` interleaved 16-bit ints. The
// pointer is owned by the driver and only valid for the duration of
// the callback — implementations MUST NOT retain it past return.
using MicFrameFn = void (*)(const std::int16_t* frame,
                            std::size_t num_samples,
                            void* user_data);

class IMicDriver {
   public:
    virtual ~IMicDriver() = default;

    // Begin pulling PCM16 frames at ``sample_rate_hz`` (typically 16000
    // for voice). ``frame_size_samples`` controls the granularity — 320
    // samples = 20 ms of audio at 16 kHz, matches Opus's frame size and
    // keeps every callback small enough to encode-and-send in one
    // notification.
    //
    // Idempotent if called twice with the same params; a second start
    // with different params MUST stop+restart cleanly.
    virtual void start_capture(std::uint32_t sample_rate_hz,
                               std::size_t frame_size_samples,
                               MicFrameFn callback,
                               void* user_data) noexcept = 0;

    virtual void stop_capture() noexcept = 0;
    virtual bool is_capturing() const noexcept = 0;
};

}  // namespace pre_buddy::hal
