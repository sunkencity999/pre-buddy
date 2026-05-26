// SPDX-License-Identifier: TBD
// ESP32-S3 IMicDriver implementation — CoreS3 onboard PDM microphone
// over I2S (peripheral I2S_NUM_0, RX-only).
//
// Skeleton — not compiled in host-test CI.

#pragma once

#include "pre_buddy/hal/i_mic.h"

namespace pre_buddy::esp32 {

class Esp32MicDriver : public hal::IMicDriver {
   public:
    Esp32MicDriver() noexcept = default;

    void start_capture(std::uint32_t sample_rate_hz,
                       std::size_t frame_size_samples,
                       hal::MicFrameFn callback,
                       void* user_data) noexcept override;

    void stop_capture() noexcept override;
    bool is_capturing() const noexcept override;

   private:
    bool capturing_ = false;
    hal::MicFrameFn callback_ = nullptr;
    void* user_data_ = nullptr;
};

}  // namespace pre_buddy::esp32
