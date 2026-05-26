// SPDX-License-Identifier: TBD
// ESP32-S3 ISpeakerDriver implementation — CoreS3 built-in I2S DAC + amp.
//
// Skeleton — not compiled in host-test CI.

#pragma once

#include "pre_buddy/hal/i_speaker.h"

namespace pre_buddy::esp32 {

class Esp32SpeakerDriver : public hal::ISpeakerDriver {
   public:
    Esp32SpeakerDriver() noexcept = default;

    void start_playback(std::uint32_t sample_rate_hz) noexcept override;
    std::size_t play_frame(const std::int16_t* samples,
                           std::size_t num_samples) noexcept override;
    void stop_playback() noexcept override;
    bool is_playing() const noexcept override;

   private:
    bool playing_ = false;
};

}  // namespace pre_buddy::esp32
