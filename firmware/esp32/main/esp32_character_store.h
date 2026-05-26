// SPDX-License-Identifier: TBD
// ESP32-S3 ICharacterStore backed by NVS.
//
// Skeleton — not compiled in host-test CI. The actual ``nvs_*`` calls
// live in esp32_character_store.cpp.

#pragma once

#include "pre_buddy/character_store.h"

namespace pre_buddy::esp32 {

class Esp32NvsCharacterStore : public ::pre_buddy::ICharacterStore {
   public:
    Esp32NvsCharacterStore() noexcept = default;

    // Must be called once before any has_character()/load()/save().
    // Opens (and creates if needed) the NVS namespace "pre_buddy".
    void init() noexcept;

    bool has_character() const noexcept override;
    Character load() const noexcept override;
    void save(Character c) noexcept override;
    void clear() noexcept override;

   private:
    bool initialised_ = false;
};

}  // namespace pre_buddy::esp32
