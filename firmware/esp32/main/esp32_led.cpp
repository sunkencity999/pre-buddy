// SPDX-License-Identifier: TBD
// ESP32-S3 ILedDriver — stub implementation.

#include "esp32_led.h"

namespace pre_buddy::esp32 {

void Esp32LedDriver::init() noexcept {
    // TODO: configure RMT TX channel for SK6812 timing.
    initialised_ = true;
}

void Esp32LedDriver::set_color(LedColor color) noexcept {
    if (!initialised_) return;
    last_color_ = color;
    // TODO: convert LedColor → RGB triplet (palette in pre_buddy::leds?)
    // then push frame via RMT.
}

void Esp32LedDriver::set_brightness(unsigned char level) noexcept {
    if (!initialised_) return;
    brightness_ = level;
    set_color(last_color_);  // re-emit with the new brightness applied.
}

void Esp32LedDriver::off() noexcept {
    if (!initialised_) return;
    // TODO: push an all-zeros frame; keep last_color_ for restore.
}

}  // namespace pre_buddy::esp32
