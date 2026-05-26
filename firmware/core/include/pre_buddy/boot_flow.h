// SPDX-License-Identifier: TBD
// PRE Buddy — host-testable first-boot flow.
//
// On the ESP32, ``app_main()`` constructs an ICharacterStore + a
// CharacterPicker and asks ``determine_initial_character()`` what to
// boot with. If the store already has a persisted choice, it's
// returned directly (the picker is skipped). Otherwise the picker is
// run via the platform-specific input loop, the confirmed choice is
// saved back, and returned.
//
// The input loop is platform-specific; the *decision* of whether to
// run it lives here, host-testable.

#pragma once

#include "pre_buddy/character.h"
#include "pre_buddy/character_picker.h"
#include "pre_buddy/character_store.h"

namespace pre_buddy {

// Returned to the caller so the ESP32 main loop knows what it ran.
enum class BootDecision : std::uint8_t {
    LoadedFromStore = 0,  // Skipped picker, returned persisted character.
    PickedByUser = 1,     // Ran picker, saved the choice.
};

struct BootOutcome {
    Character character;
    BootDecision decision;
};

// Run the picker via ``run_picker`` (caller supplies the input loop)
// only if ``store`` has no saved character. Otherwise return the stored
// value untouched. The picker is invoked with a default-constructed
// CharacterPicker; the caller may want to call ``picker.reset(seed)``
// inside ``run_picker`` to start from a different position.
template <typename RunPicker>
inline BootOutcome determine_initial_character(
    ICharacterStore& store, RunPicker run_picker) noexcept {
    if (store.has_character()) {
        return {store.load(), BootDecision::LoadedFromStore};
    }
    CharacterPicker picker;
    Character chosen = run_picker(picker);
    store.save(chosen);
    return {chosen, BootDecision::PickedByUser};
}

}  // namespace pre_buddy
