// SPDX-License-Identifier: TBD
// PRE Buddy — host-testable in-memory ICharacterStore.

#pragma once

#include "pre_buddy/character_store.h"

namespace pre_buddy::hal {

class InMemoryCharacterStore : public ::pre_buddy::ICharacterStore {
   public:
    InMemoryCharacterStore() noexcept = default;

    bool has_character() const noexcept override { return has_; }

    Character load() const noexcept override { return value_; }

    void save(Character c) noexcept override {
        value_ = c;
        has_ = true;
        ++save_calls;
    }

    void clear() noexcept override {
        has_ = false;
        ++clear_calls;
    }

    // Test helpers — number of times each mutator has been called.
    int save_calls = 0;
    int clear_calls = 0;

   private:
    Character value_ = Character::Sage;
    bool has_ = false;
};

}  // namespace pre_buddy::hal
