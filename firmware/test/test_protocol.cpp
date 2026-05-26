#include "pre_buddy/protocol.h"
#include "test_harness.h"

using namespace pre_buddy;

PRE_TEST(protocol_event_name_parsing) {
    PRE_CHECK(parse_event_kind("pre.system.wake_word")   == EventKind::WakeWord);
    PRE_CHECK(parse_event_kind("pre.bg_agents.change")   == EventKind::BgAgentChange);
    PRE_CHECK(parse_event_kind("pre.confidence.warning") == EventKind::ConfidenceWarning);
    PRE_CHECK(parse_event_kind("pre.system.error")       == EventKind::Error);
    PRE_CHECK(parse_event_kind("pre.character.set")      == EventKind::CharacterSet);
    PRE_CHECK(parse_event_kind("pre.unknown.thing")      == EventKind::Unknown);
}

PRE_TEST(wake_word_turns_head_toward_mic) {
    Event ev;
    ev.kind = EventKind::WakeWord;
    ev.source_mic = Event::Mic::Left;
    auto cmd = map_event(ev, Character::Sage);
    PRE_CHECK(cmd.has_motion);
    PRE_CHECK(cmd.motion.head_x_deg < 0.0f);

    ev.source_mic = Event::Mic::Right;
    cmd = map_event(ev, Character::Sage);
    PRE_CHECK(cmd.motion.head_x_deg > 0.0f);
}

PRE_TEST(bg_agent_change_is_led_only_no_motion) {
    Event ev;
    ev.kind = EventKind::BgAgentChange;
    ev.tier = Event::Tier::Frontier;
    auto cmd = map_event(ev, Character::Sprout);
    PRE_CHECK(!cmd.has_motion);
    PRE_CHECK(cmd.led == LedColor::Purple);

    ev.tier = Event::Tier::Fast;
    cmd = map_event(ev, Character::Sprout);
    PRE_CHECK(cmd.led == LedColor::Green);

    ev.tier = Event::Tier::Standard;
    cmd = map_event(ev, Character::Sprout);
    PRE_CHECK(cmd.led == LedColor::Blue);
}

PRE_TEST(confidence_warning_tilts_and_amber) {
    Event ev;
    ev.kind = EventKind::ConfidenceWarning;
    ev.confidence = 0.3f;
    ev.threshold  = 0.6f;
    auto cmd = map_event(ev, Character::Sentinel);
    PRE_CHECK(cmd.has_motion);
    PRE_CHECK(cmd.led == LedColor::Amber);
    PRE_CHECK(cmd.motion.head_y_deg < 45.0f);  // tilt down
}

PRE_TEST(error_event_is_red_and_still) {
    Event ev;
    ev.kind = EventKind::Error;
    auto cmd = map_event(ev, Character::Sprout);
    PRE_CHECK(!cmd.has_motion);
    PRE_CHECK(cmd.led == LedColor::Red);
}

PRE_TEST(character_reaction_durations_track_personality) {
    // Sage reacts slowest, Sprout snappiest. Use wake_word to compare.
    Event ev;
    ev.kind = EventKind::WakeWord;
    ev.source_mic = Event::Mic::Right;
    auto sage   = map_event(ev, Character::Sage);
    auto sprout = map_event(ev, Character::Sprout);
    PRE_CHECK(sage.motion.duration_ms > sprout.motion.duration_ms);
}

PRE_TEST(embodiment_command_y_is_within_safe_range_after_sanitize) {
    // Even a misbehaving event should pass through MotionEngine cleanly.
    Event ev;
    ev.kind = EventKind::ConfidenceWarning;
    auto cmd = map_event(ev, Character::Sage);

    MotionEngine eng;
    auto safe = eng.sanitize(cmd.motion);
    PRE_CHECK(safe.head_y_deg >= 10.0f);
    PRE_CHECK(safe.head_y_deg <= 80.0f);
}
