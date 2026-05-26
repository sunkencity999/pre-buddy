// SPDX-License-Identifier: TBD
// PRE Buddy — character profiles.
//
// Mirrors DESIGN.md §5: Sage / Sprout / Sentinel.
// Pure data + helpers; no hardware deps so it runs in host tests.

#pragma once

#include <cstdint>
#include <string_view>

namespace pre_buddy {

enum class Character : std::uint8_t {
    Sage = 0,
    Sprout = 1,
    Sentinel = 2,
};

// LED palette base color, picked per character for ambient/idle cues.
enum class LedColor : std::uint8_t {
    Off = 0,
    Blue,
    Green,
    Amber,
    Red,
    White,
    Cyan,
    Purple,
    Yellow,
};

struct CharacterProfile {
    Character id;
    std::string_view name;   // canonical lowercase id used on the wire
    // Motion timing (ms) for reaction transitions; lower = snappier.
    std::uint16_t reaction_ms;
    // Blink cadence range (ms); randomized within [min, max].
    std::uint16_t blink_min_ms;
    std::uint16_t blink_max_ms;
    // Default idle LED color (overridden by event-specific colors).
    LedColor idle_color;
    // Whether the character "returns to center" after every reaction.
    bool returns_to_center;
};

constexpr CharacterProfile profile_for(Character c) noexcept {
    switch (c) {
        case Character::Sage:
            return {Character::Sage, "sage", 2000, 6000, 8000, LedColor::Blue, false};
        case Character::Sprout:
            return {Character::Sprout, "sprout", 350, 3000, 5000, LedColor::Green, false};
        case Character::Sentinel:
            return {Character::Sentinel, "sentinel", 500, 5000, 5000, LedColor::White, true};
    }
    // Unreachable; default to Sage to keep firmware safe.
    return {Character::Sage, "sage", 2000, 6000, 8000, LedColor::Blue, false};
}

constexpr std::string_view to_string(Character c) noexcept {
    return profile_for(c).name;
}

// Returns true and writes *out* on success. False on unknown name.
inline bool parse_character(std::string_view s, Character& out) noexcept {
    if (s == "sage")     { out = Character::Sage;     return true; }
    if (s == "sprout")   { out = Character::Sprout;   return true; }
    if (s == "sentinel") { out = Character::Sentinel; return true; }
    return false;
}

}  // namespace pre_buddy
