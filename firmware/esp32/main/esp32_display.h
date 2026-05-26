// SPDX-License-Identifier: TBD
// ESP32-S3 IDisplayDriver implementation — drives the CoreS3 ILI9342 panel
// via LovyanGFX (or M5Unified).
//
// Skeleton only — not compiled in host-test CI.

#pragma once

#include "pre_buddy/hal/i_display.h"

namespace pre_buddy::esp32 {

class Esp32DisplayDriver : public hal::IDisplayDriver {
   public:
    Esp32DisplayDriver() noexcept = default;

    // TODO: spi/parallel bus init + panel reset.
    void init() noexcept;

    void show_character(Character ch) noexcept override;
    void show_banner(std::string_view text) noexcept override;
    void show_passkey(unsigned int code) noexcept override;
    void clear() noexcept override;

   private:
    bool initialised_ = false;
};

}  // namespace pre_buddy::esp32
