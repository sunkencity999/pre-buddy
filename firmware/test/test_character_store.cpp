// SPDX-License-Identifier: TBD
// Tests for ICharacterStore (in-memory impl) + the boot flow helper.

#include "pre_buddy/boot_flow.h"
#include "pre_buddy/hal/in_memory_character_store.h"
#include "test_harness.h"

using namespace pre_buddy;
using namespace pre_buddy::hal;

// ── InMemoryCharacterStore ────────────────────────────────────────────


PRE_TEST(store_starts_empty) {
    InMemoryCharacterStore s;
    PRE_CHECK(!s.has_character());
}

PRE_TEST(store_save_then_load_round_trip) {
    InMemoryCharacterStore s;
    s.save(Character::Sprout);
    PRE_CHECK(s.has_character());
    PRE_CHECK(s.load() == Character::Sprout);
    PRE_CHECK_EQ(s.save_calls, 1);
}

PRE_TEST(store_save_overwrites_previous_value) {
    InMemoryCharacterStore s;
    s.save(Character::Sage);
    s.save(Character::Sentinel);
    PRE_CHECK(s.load() == Character::Sentinel);
    PRE_CHECK_EQ(s.save_calls, 2);
}

PRE_TEST(store_clear_makes_has_character_false) {
    InMemoryCharacterStore s;
    s.save(Character::Sprout);
    s.clear();
    PRE_CHECK(!s.has_character());
    PRE_CHECK_EQ(s.clear_calls, 1);
}

PRE_TEST(store_clear_when_already_empty_is_safe) {
    InMemoryCharacterStore s;
    s.clear();
    PRE_CHECK(!s.has_character());
}


// ── determine_initial_character() boot flow ──────────────────────────


PRE_TEST(boot_skips_picker_when_store_already_has_character) {
    InMemoryCharacterStore s;
    s.save(Character::Sprout);

    bool picker_ran = false;
    auto outcome = determine_initial_character(
        s,
        [&picker_ran](CharacterPicker& p) {
            picker_ran = true;
            return p.confirm();  // would default to Sage if invoked
        });

    PRE_CHECK(!picker_ran);
    PRE_CHECK(outcome.character == Character::Sprout);
    PRE_CHECK(outcome.decision == BootDecision::LoadedFromStore);
}

PRE_TEST(boot_runs_picker_then_saves_when_store_empty) {
    InMemoryCharacterStore s;

    auto outcome = determine_initial_character(
        s,
        [](CharacterPicker& p) {
            // Caller-controlled input loop. Move to Sentinel and confirm.
            p.next();
            p.next();
            return p.confirm();
        });

    PRE_CHECK(outcome.character == Character::Sentinel);
    PRE_CHECK(outcome.decision == BootDecision::PickedByUser);
    PRE_CHECK(s.has_character());
    PRE_CHECK(s.load() == Character::Sentinel);
    PRE_CHECK_EQ(s.save_calls, 1);
}

PRE_TEST(boot_does_not_double_save_on_second_boot) {
    InMemoryCharacterStore s;
    // First boot: user picks Sprout.
    determine_initial_character(s, [](CharacterPicker& p) {
        p.next();
        return p.confirm();
    });
    PRE_CHECK_EQ(s.save_calls, 1);

    // Second boot: store has the value, picker is skipped, no extra save.
    auto outcome = determine_initial_character(
        s, [](CharacterPicker&) { return Character::Sage; });
    PRE_CHECK(outcome.decision == BootDecision::LoadedFromStore);
    PRE_CHECK_EQ(s.save_calls, 1);
}
