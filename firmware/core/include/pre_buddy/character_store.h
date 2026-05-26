// SPDX-License-Identifier: TBD
// PRE Buddy — on-device character persistence.
//
// The first-boot picker writes the user's choice into an
// ``ICharacterStore`` so subsequent boots skip the picker. The ESP32
// backs this with NVS; host tests use an in-memory implementation.
//
// Interface only — no IDF deps. Concrete impls live in
// ``firmware/core/include/pre_buddy/hal/in_memory_character_store.h``
// (host tests) and ``firmware/esp32/main/esp32_character_store.cpp``
// (the device).

#pragma once

#include "pre_buddy/character.h"

namespace pre_buddy {

class ICharacterStore {
   public:
    virtual ~ICharacterStore() = default;

    // True if a character has been persisted. False on a fresh device,
    // after a factory reset, or if NVS is unavailable. The boot flow
    // uses this to decide whether to run the first-boot picker.
    virtual bool has_character() const noexcept = 0;

    // Return the persisted character. Result is unspecified if
    // ``has_character()`` is false — callers MUST check first. We
    // intentionally don't return optional<Character> to keep the
    // interface ABI-friendly on platforms with no <optional> support.
    virtual Character load() const noexcept = 0;

    // Persist the choice. Subsequent ``has_character()`` returns true,
    // ``load()`` returns ``c``. Idempotent on the same value.
    virtual void save(Character c) noexcept = 0;

    // Forget any persisted choice. Used by a future "factory reset"
    // path in the tray menu / device button combo.
    virtual void clear() noexcept = 0;
};

}  // namespace pre_buddy
