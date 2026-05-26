// SPDX-License-Identifier: TBD
// ESP32-S3 ILedDriver — stub implementation.

#include "esp32_led.h"

#include "pre_buddy/led_palette.h"

namespace pre_buddy::esp32 {

void Esp32LedDriver::init() noexcept {
    // TODO: configure RMT TX channel for SK6812 timing.
    initialised_ = true;
}

void Esp32LedDriver::set_color(LedColor color) noexcept {
    if (!initialised_) return;
    last_color_ = color;
    // Palette lives in firmware/core/include/pre_buddy/led_palette.h and
    // is host-tested for all 9 colours; here we just project it through
    // the current brightness setting.
    const auto rgb = apply_brightness(to_rgb888(color), brightness_);
    (void)rgb;
    // TODO: pack rgb into the SK6812 GRB byte order and rmt_transmit
    // the frame via the configured tx channel.
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
