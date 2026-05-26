// SPDX-License-Identifier: TBD
// PRE Buddy — display driver interface.
//
// The 2" IPS screen is used for: character identity, pairing passkey,
// transient status banners. The interface stays deliberately narrow —
// rich panel rendering is firmware-side concern, not HAL.

#pragma once

#include <string_view>

#include "pre_buddy/character.h"

namespace pre_buddy::hal {

class IDisplayDriver {
   public:
    virtual ~IDisplayDriver() = default;

    // Render the chosen character's idle face. Called on boot, on
    // pre.character.set, and after recovery from sleep.
    virtual void show_character(Character ch) noexcept = 0;

    // Render an arbitrary short status banner. The implementation is
    // responsible for truncation / line-wrapping. Persistent until the
    // next show_* call.
    virtual void show_banner(std::string_view text) noexcept = 0;

    // Display the 6-digit pairing passkey during BLE bonding. Separate
    // method (instead of using show_banner) so a future driver can apply
    // larger glyphs / contrast tweaks for accessibility.
    virtual void show_passkey(unsigned int code) noexcept = 0;

    // Blank the screen (sleep / quiet hours).
    virtual void clear() noexcept = 0;
};

}  // namespace pre_buddy::hal
