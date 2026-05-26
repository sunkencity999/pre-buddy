// SPDX-License-Identifier: TBD
// PRE Buddy — first-boot character picker state machine.
//
// On a fresh device, before any pre.character.set has arrived from the
// server, the user picks one of Sage / Sprout / Sentinel directly on
// the IPS screen. The hardware integration (tap / button → input) is
// platform-specific and TODO; this header is the pure state machine
// they all share.
//
// Cycle: Sage → Sprout → Sentinel → Sage → ... (next), reverse for prev.
// Confirm freezes the choice and ignores further input until reset.

#pragma once

#include "pre_buddy/character.h"

namespace pre_buddy {

class CharacterPicker {
   public:
    constexpr CharacterPicker() noexcept = default;

    explicit constexpr CharacterPicker(Character initial) noexcept
        : current_(initial) {}

    constexpr Character current() const noexcept { return current_; }
    constexpr bool is_confirmed() const noexcept { return confirmed_; }

    // Advance one slot in the cycle. No-op once confirmed.
    constexpr void next() noexcept {
        if (confirmed_) return;
        current_ = _step(current_, +1);
    }

    // Reverse one slot in the cycle. No-op once confirmed.
    constexpr void prev() noexcept {
        if (confirmed_) return;
        current_ = _step(current_, -1);
    }

    // Freeze on the currently-highlighted character and return it.
    // Subsequent calls are idempotent — once confirmed, the picker is
    // a constant-value view of the final choice.
    constexpr Character confirm() noexcept {
        confirmed_ = true;
        return current_;
    }

    // Re-arm the picker. The ESP32 reset button uses this to send the
    // user back through onboarding without nuking NVS.
    constexpr void reset(Character initial = Character::Sage) noexcept {
        current_ = initial;
        confirmed_ = false;
    }

   private:
    static constexpr Character _step(Character c, int delta) noexcept {
        // Three-state ring. Cast through int for safety; the values
        // happen to be 0/1/2 today but we don't want to rely on that.
        int idx = 0;
        switch (c) {
            case Character::Sage:     idx = 0; break;
            case Character::Sprout:   idx = 1; break;
            case Character::Sentinel: idx = 2; break;
        }
        idx = (idx + delta + 3) % 3;
        switch (idx) {
            case 0: return Character::Sage;
            case 1: return Character::Sprout;
            case 2: return Character::Sentinel;
        }
        return Character::Sage;
    }

    Character current_ = Character::Sage;
    bool confirmed_ = false;
};

}  // namespace pre_buddy
