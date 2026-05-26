// SPDX-License-Identifier: TBD
// Tests for the first-boot character picker.

#include "pre_buddy/character_picker.h"
#include "test_harness.h"

using namespace pre_buddy;

PRE_TEST(picker_defaults_to_sage_when_constructed_without_init) {
    CharacterPicker p;
    PRE_CHECK(p.current() == Character::Sage);
    PRE_CHECK(!p.is_confirmed());
}

PRE_TEST(picker_honours_explicit_initial_character) {
    CharacterPicker p(Character::Sprout);
    PRE_CHECK(p.current() == Character::Sprout);
}

PRE_TEST(picker_next_cycles_through_all_three_characters_in_order) {
    CharacterPicker p;
    PRE_CHECK(p.current() == Character::Sage);
    p.next();
    PRE_CHECK(p.current() == Character::Sprout);
    p.next();
    PRE_CHECK(p.current() == Character::Sentinel);
    p.next();
    PRE_CHECK(p.current() == Character::Sage);  // wrap
}

PRE_TEST(picker_prev_cycles_the_other_direction) {
    CharacterPicker p;
    p.prev();
    PRE_CHECK(p.current() == Character::Sentinel);  // wrap backwards
    p.prev();
    PRE_CHECK(p.current() == Character::Sprout);
    p.prev();
    PRE_CHECK(p.current() == Character::Sage);
}

PRE_TEST(picker_confirm_returns_current_and_freezes_state) {
    CharacterPicker p;
    p.next();  // Sprout
    Character chosen = p.confirm();
    PRE_CHECK(chosen == Character::Sprout);
    PRE_CHECK(p.is_confirmed());

    // Further next/prev calls are ignored.
    p.next();
    p.prev();
    PRE_CHECK(p.current() == Character::Sprout);
}

PRE_TEST(picker_double_confirm_is_idempotent) {
    CharacterPicker p;
    Character a = p.confirm();
    Character b = p.confirm();
    PRE_CHECK(a == b);
    PRE_CHECK(a == Character::Sage);
}

PRE_TEST(picker_reset_clears_confirmation_and_returns_to_initial) {
    CharacterPicker p;
    p.next();
    p.next();
    p.confirm();
    PRE_CHECK(p.is_confirmed());

    p.reset();
    PRE_CHECK(!p.is_confirmed());
    PRE_CHECK(p.current() == Character::Sage);
}

PRE_TEST(picker_reset_with_argument_changes_starting_point) {
    CharacterPicker p(Character::Sprout);
    p.confirm();
    p.reset(Character::Sentinel);
    PRE_CHECK(p.current() == Character::Sentinel);
    PRE_CHECK(!p.is_confirmed());
}
