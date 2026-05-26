// SPDX-License-Identifier: TBD
// ESP32-S3 ILedDriver implementation — driving the SK6812/NeoPixel ring
// on the CoreS3 board via the RMT peripheral.
//
// Skeleton only — not compiled in host-test CI.

#pragma once

#include "pre_buddy/hal/i_led.h"

namespace pre_buddy::esp32 {

class Esp32LedDriver : public hal::ILedDriver {
   public:
    Esp32LedDriver() noexcept = default;

    // TODO: configure rmt_tx_channel_config_t for the LED data pin.
    void init() noexcept;

    void set_color(LedColor color) noexcept override;
    void set_brightness(unsigned char level) noexcept override;
    void off() noexcept override;

   private:
    LedColor last_color_ = LedColor::Off;
    unsigned char brightness_ = 64;
    bool initialised_ = false;
};

}  // namespace pre_buddy::esp32
