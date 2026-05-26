// SPDX-License-Identifier: TBD
// PRE Buddy — sprite atlas index.
//
// Maps (Character, Expression) → a stable index in [0, 24). The byte arrays
// themselves (RGB565 raster, 96×80 each) live in
// ``firmware/esp32/main/sprites_data.h`` (auto-generated), which is only
// compiled into the ESP32 target. The core header keeps the *contract* —
// the index function — so the rest of the firmware can be exercised in
// host tests without dragging in ~370 KB of sprite data.

#pragma once

#include <cstdint>

#include "pre_buddy/character.h"
#include "pre_buddy/expression.h"

namespace pre_buddy::sprites {

inline constexpr std::uint8_t SPRITE_COUNT = 24;       // 3 characters × 8 expressions
inline constexpr std::uint16_t SPRITE_WIDTH_PX = 96;
inline constexpr std::uint16_t SPRITE_HEIGHT_PX = 80;

// Stable enumeration. Generator (`tools/sprites_to_header.py`) emits its
// data array in this same order, so ``sprite_index(c, e)`` is also the
// flat offset into the data table.
constexpr std::uint8_t sprite_index(Character ch, Expression expr) noexcept {
    std::uint8_t c = 0;
    switch (ch) {
        case Character::Sage:     c = 0; break;
        case Character::Sprout:   c = 1; break;
        case Character::Sentinel: c = 2; break;
    }
    std::uint8_t e = 0;
    switch (expr) {
        case Expression::Neutral:   e = 0; break;
        case Expression::Surprised: e = 1; break;
        case Expression::Thinking:  e = 2; break;
        case Expression::Concerned: e = 3; break;
        case Expression::Happy:     e = 4; break;
        case Expression::Sleepy:    e = 5; break;
        case Expression::Curious:   e = 6; break;
        case Expression::Error:     e = 7; break;
    }
    return static_cast<std::uint8_t>(c * 8 + e);
}

}  // namespace pre_buddy::sprites
