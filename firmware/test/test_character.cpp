#include "pre_buddy/character.h"
#include "test_harness.h"

using namespace pre_buddy;

PRE_TEST(character_names_round_trip) {
    PRE_CHECK_EQ(to_string(Character::Sage),     std::string_view{"sage"});
    PRE_CHECK_EQ(to_string(Character::Sprout),   std::string_view{"sprout"});
    PRE_CHECK_EQ(to_string(Character::Sentinel), std::string_view{"sentinel"});

    Character c{};
    PRE_CHECK(parse_character("sage", c));     PRE_CHECK(c == Character::Sage);
    PRE_CHECK(parse_character("sprout", c));   PRE_CHECK(c == Character::Sprout);
    PRE_CHECK(parse_character("sentinel", c)); PRE_CHECK(c == Character::Sentinel);
    PRE_CHECK(!parse_character("dragon", c));
    PRE_CHECK(!parse_character("", c));
}

PRE_TEST(character_profiles_have_distinct_personality) {
    auto sage     = profile_for(Character::Sage);
    auto sprout   = profile_for(Character::Sprout);
    auto sentinel = profile_for(Character::Sentinel);

    // Sage moves slowest, Sprout snappiest.
    PRE_CHECK(sage.reaction_ms > sprout.reaction_ms);
    PRE_CHECK(sprout.reaction_ms < sentinel.reaction_ms);

    // Sentinel returns to center (military-style).
    PRE_CHECK(sentinel.returns_to_center);
    PRE_CHECK(!sage.returns_to_center);
    PRE_CHECK(!sprout.returns_to_center);

    // Sentinel blink is regular (min == max), others are randomized.
    PRE_CHECK_EQ(sentinel.blink_min_ms, sentinel.blink_max_ms);
    PRE_CHECK(sage.blink_min_ms != sage.blink_max_ms);
    PRE_CHECK(sprout.blink_min_ms != sprout.blink_max_ms);
}
