// SPDX-License-Identifier: TBD
// Tests for the (Character, Expression) → sprite index lookup.

#include <set>

#include "pre_buddy/sprite_atlas.h"
#include "test_harness.h"

using namespace pre_buddy;
using namespace pre_buddy::sprites;

namespace {

constexpr Character ALL_CHARS[] = {
    Character::Sage, Character::Sprout, Character::Sentinel,
};

constexpr Expression ALL_EXPR[] = {
    Expression::Neutral,  Expression::Surprised, Expression::Thinking,
    Expression::Concerned, Expression::Happy,    Expression::Sleepy,
    Expression::Curious,   Expression::Error,
};

}  // namespace

PRE_TEST(sprite_atlas_covers_3x8_combinations) {
    std::set<int> seen;
    for (Character c : ALL_CHARS) {
        for (Expression e : ALL_EXPR) {
            seen.insert(sprite_index(c, e));
        }
    }
    PRE_CHECK_EQ(seen.size(), 24u);
}

PRE_TEST(sprite_atlas_indices_are_bounded) {
    for (Character c : ALL_CHARS) {
        for (Expression e : ALL_EXPR) {
            auto idx = sprite_index(c, e);
            PRE_CHECK(idx < SPRITE_COUNT);
        }
    }
}

PRE_TEST(sprite_atlas_distinct_chars_give_distinct_indices_for_same_expression) {
    // Holding expression constant, every character must land in a
    // different slot. The generator relies on this layout.
    for (Expression e : ALL_EXPR) {
        auto a = sprite_index(Character::Sage, e);
        auto b = sprite_index(Character::Sprout, e);
        auto c = sprite_index(Character::Sentinel, e);
        PRE_CHECK(a != b);
        PRE_CHECK(b != c);
        PRE_CHECK(a != c);
    }
}

PRE_TEST(sprite_atlas_distinct_expressions_give_distinct_indices_for_same_char) {
    // Same shape: holding character constant, every expression is unique.
    std::set<int> seen;
    for (Expression e : ALL_EXPR) {
        seen.insert(sprite_index(Character::Sentinel, e));
    }
    PRE_CHECK_EQ(seen.size(), 8u);
}

PRE_TEST(sprite_atlas_layout_groups_by_character) {
    // Generator emits 8 sprites per character, contiguous: Sage 0..7,
    // Sprout 8..15, Sentinel 16..23. If this invariant breaks, the
    // emitter and the device-side blit logic will disagree silently.
    PRE_CHECK_EQ(sprite_index(Character::Sage, Expression::Neutral), 0u);
    PRE_CHECK_EQ(sprite_index(Character::Sage, Expression::Error), 7u);
    PRE_CHECK_EQ(sprite_index(Character::Sprout, Expression::Neutral), 8u);
    PRE_CHECK_EQ(sprite_index(Character::Sentinel, Expression::Neutral), 16u);
    PRE_CHECK_EQ(sprite_index(Character::Sentinel, Expression::Error), 23u);
}

PRE_TEST(sprite_atlas_dimensions_match_panel_footprint) {
    // The CoreS3 IPS panel is 320×240. The face occupies the upper
    // portion below the LED slit; 96×80 leaves room for the slit + the
    // status footer.
    PRE_CHECK_EQ(SPRITE_WIDTH_PX, 96u);
    PRE_CHECK_EQ(SPRITE_HEIGHT_PX, 80u);
}
