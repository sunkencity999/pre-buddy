// SPDX-License-Identifier: TBD
// PRE Buddy — user-adjustable settings (LED, volume, animations).
//
// Settings arrive from PRE's GUI / agent tool as a `pre.buddy.config` event
// over BLE (see shared/protocol). They are applied live in main.cpp and
// persisted to NVS so they survive a reboot even with no host connected.
//
// This module owns only the settings struct + its JSON decode + NVS load/save.
// Applying values to the hardware (LED color/brightness, speaker gain, the
// animation gates) stays in main.cpp, which holds the driver references.

#pragma once

#include <cstddef>
#include <cstdint>

#include "pre_buddy/character.h"  // LedColor

namespace pre_buddy::esp32 {

struct BuddyConfig {
    bool led_override = false;                  // false = follow the character's idle color
    pre_buddy::LedColor led_color = pre_buddy::LedColor::Blue;
    std::uint8_t led_brightness = 180;
    int volume = 100;                           // 0-100, software gain on playback
    bool idle_anim = true;                      // ambient idle head sway
    bool thinking_anim = true;                  // thinking face/LED/sway while awaiting PRE
    bool boot_chime = true;                     // two-note chime at startup
};

// Load persisted settings from NVS into `cfg` (leaves defaults if none saved).
void buddy_config_load(BuddyConfig& cfg) noexcept;

// Persist `cfg` to NVS.
void buddy_config_save(const BuddyConfig& cfg) noexcept;

// If `buf` is a `pre.buddy.config` event, apply its present fields to `cfg`,
// persist, and return true (so the caller can re-apply to hardware). Returns
// false for any other line, leaving `cfg` untouched.
bool buddy_config_apply_json(BuddyConfig& cfg, const char* buf, std::size_t n) noexcept;

}  // namespace pre_buddy::esp32
