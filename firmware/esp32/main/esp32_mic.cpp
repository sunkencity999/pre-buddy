// SPDX-License-Identifier: TBD
// ESP32-S3 PDM microphone driver — stub.

#include "esp32_mic.h"

namespace pre_buddy::esp32 {

void Esp32MicDriver::start_capture(std::uint32_t sample_rate_hz,
                                   std::size_t frame_size_samples,
                                   hal::MicFrameFn callback,
                                   void* user_data) noexcept {
    (void)sample_rate_hz;
    (void)frame_size_samples;
    callback_ = callback;
    user_data_ = user_data;
    capturing_ = true;
    // TODO when CoreS3 arrives:
    //   - i2s_pdm_rx_clk_config_t with target sample_rate_hz
    //   - i2s_new_channel + i2s_channel_init_pdm_rx_mode
    //   - Allocate a frame buffer of frame_size_samples * sizeof(int16_t)
    //   - Spawn a task that loops on i2s_channel_read(...) and invokes
    //     callback_(buf, frame_size_samples, user_data_) on each frame.
}

void Esp32MicDriver::stop_capture() noexcept {
    capturing_ = false;
    // TODO: signal the capture task to exit; i2s_channel_disable;
    // i2s_del_channel; free buffers.
}

bool Esp32MicDriver::is_capturing() const noexcept { return capturing_; }

}  // namespace pre_buddy::esp32
