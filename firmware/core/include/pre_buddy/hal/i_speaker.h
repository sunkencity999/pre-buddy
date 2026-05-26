// SPDX-License-Identifier: TBD
// PRE Buddy — speaker driver interface.
//
// CoreS3 ships an I2S DAC + small built-in speaker. Concrete driver in
// firmware/esp32/main/esp32_speaker.cpp. The host-side mock recording
// API is in hal/mock_audio.h.

#pragma once

#include <cstddef>
#include <cstdint>

namespace pre_buddy::hal {

class ISpeakerDriver {
   public:
    virtual ~ISpeakerDriver() = default;

    // Prepare the DAC pipeline at ``sample_rate_hz``. Must be called
    // before any play_frame(). Sample rate switches require a
    // start/stop pair so the I2S clock can re-lock.
    virtual void start_playback(std::uint32_t sample_rate_hz) noexcept = 0;

    // Enqueue ``num_samples`` PCM16 mono samples for playback. The
    // driver is expected to copy the data immediately (it does NOT
    // borrow ``samples`` past return). Returns the number of samples
    // actually accepted — fewer than ``num_samples`` means the internal
    // ring buffer is full and the caller should back off.
    virtual std::size_t play_frame(const std::int16_t* samples,
                                   std::size_t num_samples) noexcept = 0;

    // Drain the buffer and shut down the I2S clock.
    virtual void stop_playback() noexcept = 0;

    virtual bool is_playing() const noexcept = 0;
};

}  // namespace pre_buddy::hal
