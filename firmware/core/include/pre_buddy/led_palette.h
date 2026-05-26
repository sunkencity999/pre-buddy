// SPDX-License-Identifier: TBD
// PRE Buddy — LedColor → 24-bit RGB palette.
//
// The ESP32 SK6812 driver pushes 8-bit-per-channel GRB triples to the
// ring; the palette is the only platform-independent piece of that
// path, so it lives here for host tests. Numbers chosen to match the
// viewer's hex codes (viewer.js LED_COLORS), so the GUI and the device
// agree on what "blue" looks like.

#pragma once

#include <cstdint>

#include "pre_buddy/character.h"  // for LedColor

namespace pre_buddy {

struct Rgb888 {
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;

    constexpr bool operator==(const Rgb888& other) const noexcept {
        return r == other.r && g == other.g && b == other.b;
    }
};

constexpr Rgb888 to_rgb888(LedColor c) noexcept {
    switch (c) {
        case LedColor::Off:    return {0x00, 0x00, 0x00};
        case LedColor::Blue:   return {0x3B, 0x82, 0xF6};  // #3B82F6
        case LedColor::Green:  return {0x10, 0xB9, 0x81};  // #10B981
        case LedColor::Amber:  return {0xF5, 0x9E, 0x0B};  // #F59E0B
        case LedColor::Red:    return {0xEF, 0x44, 0x44};  // #EF4444
        case LedColor::White:  return {0xF8, 0xFA, 0xFC};  // #F8FAFC
        case LedColor::Cyan:   return {0x06, 0xB6, 0xD4};  // #06B6D4
        case LedColor::Purple: return {0x8B, 0x5C, 0xF6};  // #8B5CF6
        case LedColor::Yellow: return {0xEA, 0xB3, 0x08};  // #EAB308
    }
    return {0x00, 0x00, 0x00};
}

// Apply 0..255 brightness without losing the colour identity. We use
// integer math to stay deterministic across platforms (no FP rounding
// surprises between host gcc and ESP32 xtensa).
constexpr Rgb888 apply_brightness(Rgb888 c, std::uint8_t level) noexcept {
    return {
        static_cast<std::uint8_t>((c.r * level) / 255),
        static_cast<std::uint8_t>((c.g * level) / 255),
        static_cast<std::uint8_t>((c.b * level) / 255),
    };
}

}  // namespace pre_buddy
