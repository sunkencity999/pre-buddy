// SPDX-License-Identifier: TBD
// ESP32-S3 I2S speaker driver — stub.

#include "esp32_speaker.h"

namespace pre_buddy::esp32 {

void Esp32SpeakerDriver::start_playback(std::uint32_t sample_rate_hz) noexcept {
    (void)sample_rate_hz;
    playing_ = true;
    // TODO when CoreS3 arrives:
    //   - i2s_std_clk_config_t with target sample_rate_hz
    //   - i2s_new_channel + i2s_channel_init_std_mode (TX direction)
    //   - i2s_channel_enable. Optionally enable the CoreS3's AW88298
    //     amp by toggling its I2C control register first.
}

std::size_t Esp32SpeakerDriver::play_frame(const std::int16_t* samples,
                                           std::size_t num_samples) noexcept {
    (void)samples;
    if (!playing_) return 0;
    // TODO:
    //   size_t written = 0;
    //   i2s_channel_write(handle_, samples, num_samples * sizeof(int16_t),
    //                     &written, portMAX_DELAY);
    //   return written / sizeof(int16_t);
    return num_samples;
}

void Esp32SpeakerDriver::stop_playback() noexcept {
    playing_ = false;
    // TODO: i2s_channel_disable + i2s_del_channel + amp gate low.
}

bool Esp32SpeakerDriver::is_playing() const noexcept { return playing_; }

}  // namespace pre_buddy::esp32
