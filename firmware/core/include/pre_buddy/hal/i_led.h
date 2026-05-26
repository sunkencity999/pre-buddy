// SPDX-License-Identifier: TBD
// PRE Buddy — LED ring driver interface.
//
// Used for the ambient LED (color is the dominant signal — see
// docs/embodiment.md). Brightness is a separate knob from color so the
// caller can dim the ring during quiet hours without losing the color.

#pragma once

#include "pre_buddy/character.h"

namespace pre_buddy::hal {

class ILedDriver {
   public:
    virtual ~ILedDriver() = default;

    // Set the ring to a solid color (envelopes/pulses TBD post-S0).
    virtual void set_color(LedColor color) noexcept = 0;

    // 0..255 brightness. Out-of-range values MUST be clamped, not rejected.
    virtual void set_brightness(unsigned char level) noexcept = 0;

    // Turn the ring off without changing the stored color, so the next
    // set_brightness() restores it.
    virtual void off() noexcept = 0;
};

}  // namespace pre_buddy::hal
