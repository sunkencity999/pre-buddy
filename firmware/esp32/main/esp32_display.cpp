// SPDX-License-Identifier: TBD
// ESP32-S3 IDisplayDriver — stub implementation.

#include "esp32_display.h"

namespace pre_buddy::esp32 {

void Esp32DisplayDriver::init() noexcept {
    // TODO: M5.Display.begin() or LGFX::init().
    initialised_ = true;
}

void Esp32DisplayDriver::show_character(Character ch) noexcept {
    if (!initialised_) return;
    (void)ch;
    // TODO: draw the corresponding character glyph from a sprite atlas.
}

void Esp32DisplayDriver::show_banner(std::string_view text) noexcept {
    if (!initialised_) return;
    (void)text;
    // TODO: clear + drawString at a fixed banner y-position.
}

void Esp32DisplayDriver::show_passkey(unsigned int code) noexcept {
    if (!initialised_) return;
    (void)code;
    // TODO: render large-glyph 6-digit pairing code centered on screen.
}

void Esp32DisplayDriver::clear() noexcept {
    if (!initialised_) return;
    // TODO: fillScreen(BLACK).
}

}  // namespace pre_buddy::esp32
