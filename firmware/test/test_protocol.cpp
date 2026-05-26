#include "pre_buddy/protocol.h"
#include "test_harness.h"

using namespace pre_buddy;

PRE_TEST(protocol_event_name_parsing) {
    PRE_CHECK(parse_event_kind("pre.system.wake_word") == EventKind::WakeWord);
    PRE_CHECK(parse_event_kind("pre.bg_agents.change") == EventKind::BgAgentChange);
    PRE_CHECK(parse_event_kind("pre.router.decision") == EventKind::RouterDecision);
    PRE_CHECK(parse_event_kind("pre.confidence.warning") == EventKind::ConfidenceWarning);
    PRE_CHECK(parse_event_kind("pre.confidence.snapshot") == EventKind::ConfidenceSnapshot);
    PRE_CHECK(parse_event_kind("pre.kg.delta") == EventKind::KgDelta);
    PRE_CHECK(parse_event_kind("pre.training.progress") == EventKind::TrainingProgress);
    PRE_CHECK(parse_event_kind("pre.scheduler.upcoming") == EventKind::SchedulerUpcoming);
    PRE_CHECK(parse_event_kind("pre.tools.rollup") == EventKind::ToolsRollup);
    PRE_CHECK(parse_event_kind("pre.system.memory_write") == EventKind::MemoryWrite);
    PRE_CHECK(parse_event_kind("pre.system.proximity") == EventKind::Proximity);
    PRE_CHECK(parse_event_kind("pre.system.error") == EventKind::Error);
    PRE_CHECK(parse_event_kind("pre.character.set") == EventKind::CharacterSet);
    PRE_CHECK(parse_event_kind("pre.unknown.thing") == EventKind::Unknown);
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

PRE_TEST(router_decision_only_moves_on_escalation) {
    Event ev;
    ev.kind = EventKind::RouterDecision;
    ev.from_tier = Event::Tier::Fast;
    ev.to_tier = Event::Tier::Frontier;

    auto cmd = map_event(ev, Character::Sentinel);
    PRE_CHECK(cmd.has_motion);
    PRE_CHECK(cmd.led == LedColor::Purple);

    ev.from_tier = Event::Tier::Frontier;
    ev.to_tier = Event::Tier::Standard;
    cmd = map_event(ev, Character::Sentinel);
    PRE_CHECK(!cmd.has_motion);
    PRE_CHECK(cmd.led == LedColor::Blue);
}

PRE_TEST(confidence_warning_tilts_and_amber) {
    Event ev;
    ev.kind = EventKind::ConfidenceWarning;
    ev.confidence = 0.3f;
    ev.threshold = 0.6f;
    auto cmd = map_event(ev, Character::Sentinel);
    PRE_CHECK(cmd.has_motion);
    PRE_CHECK(cmd.led == LedColor::Amber);
    PRE_CHECK(cmd.motion.head_y_deg < 45.0f);  // tilt down
}

PRE_TEST(memory_write_nods_with_white_led) {
    Event ev;
    ev.kind = EventKind::MemoryWrite;
    auto cmd = map_event(ev, Character::Sage);
    PRE_CHECK(cmd.has_motion);
    PRE_CHECK(cmd.motion.head_y_deg < 45.0f);
    PRE_CHECK(cmd.led == LedColor::White);
}

PRE_TEST(proximity_looks_up) {
    Event ev;
    ev.kind = EventKind::Proximity;
    ev.distance_cm = 45.0f;
    auto cmd = map_event(ev, Character::Sprout);
    PRE_CHECK(cmd.has_motion);
    PRE_CHECK(cmd.motion.head_y_deg > 45.0f);
}

PRE_TEST(scheduler_upcoming_is_amber_when_soon) {
    Event ev;
    ev.kind = EventKind::SchedulerUpcoming;
    ev.minutes_until = 30;
    auto cmd = map_event(ev, Character::Sage);
    PRE_CHECK(!cmd.has_motion);
    PRE_CHECK(cmd.led == LedColor::Amber);

    ev.minutes_until = 300;
    cmd = map_event(ev, Character::Sage);
    PRE_CHECK(cmd.led == LedColor::Blue);
}

PRE_TEST(error_event_is_red_and_still) {
    Event ev;
    ev.kind = EventKind::Error;
    auto cmd = map_event(ev, Character::Sprout);
    PRE_CHECK(!cmd.has_motion);
    PRE_CHECK(cmd.led == LedColor::Red);
}

PRE_TEST(character_reaction_durations_track_personality) {
    Event ev;
    ev.kind = EventKind::WakeWord;
    ev.source_mic = Event::Mic::Right;
    auto sage = map_event(ev, Character::Sage);
    auto sprout = map_event(ev, Character::Sprout);
    PRE_CHECK(sage.motion.duration_ms > sprout.motion.duration_ms);
}

PRE_TEST(embodiment_command_y_is_within_safe_range_after_sanitize) {
    Event ev;
    ev.kind = EventKind::ConfidenceWarning;
    auto cmd = map_event(ev, Character::Sage);

    MotionEngine eng;
    auto safe = eng.sanitize(cmd.motion);
    PRE_CHECK(safe.head_y_deg >= 10.0f);
    PRE_CHECK(safe.head_y_deg <= 80.0f);
}
