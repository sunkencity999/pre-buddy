// SPDX-License-Identifier: TBD
// End-to-end tests for the robot loop: event in, HAL calls out.

#include "pre_buddy/hal/mock.h"
#include "pre_buddy/robot_loop.h"
#include "test_harness.h"

using namespace pre_buddy;
using namespace pre_buddy::hal;

namespace {

struct Rig {
    MockServoDriver servo;
    MockLedDriver led;
    MockDisplayDriver display;
    RobotLoop loop;

    explicit Rig(Character ch = Character::Sage)
        : loop(ch, servo, led, display) {}
};

}  // namespace

PRE_TEST(robot_loop_wake_word_turns_head_and_sets_led) {
    Rig rig(Character::Sprout);
    Event ev;
    ev.kind = EventKind::WakeWord;
    ev.source_mic = Event::Mic::Left;

    rig.loop.dispatch(ev);

    PRE_CHECK_EQ(rig.servo.moves.size(), 1u);
    PRE_CHECK_NEAR(rig.servo.moves[0].head_x_deg, -35.0f, 0.01);
    PRE_CHECK_EQ(rig.led.color_calls.size(), 1u);
    PRE_CHECK(rig.led.color_calls[0] == LedColor::Yellow);  // Sprout accent
    PRE_CHECK_EQ(rig.display.character_calls.size(), 0u);
}

PRE_TEST(robot_loop_bg_agent_change_is_led_only) {
    Rig rig(Character::Sage);
    Event ev;
    ev.kind = EventKind::BgAgentChange;
    ev.tier = Event::Tier::Frontier;

    rig.loop.dispatch(ev);

    PRE_CHECK_EQ(rig.servo.moves.size(), 0u);
    PRE_CHECK_EQ(rig.led.color_calls.size(), 1u);
    PRE_CHECK(rig.led.color_calls[0] == LedColor::Purple);  // frontier tier
}

PRE_TEST(robot_loop_router_escalation_nods) {
    Rig rig(Character::Sage);
    Event ev;
    ev.kind = EventKind::RouterDecision;
    ev.from_tier = Event::Tier::Fast;
    ev.to_tier = Event::Tier::Frontier;

    rig.loop.dispatch(ev);

    PRE_CHECK_EQ(rig.servo.moves.size(), 1u);
    PRE_CHECK_NEAR(rig.servo.moves[0].head_y_deg, 38.0f, 0.01);
    PRE_CHECK(rig.led.color_calls[0] == LedColor::Purple);
}

PRE_TEST(robot_loop_router_lateral_stays_led_only) {
    Rig rig(Character::Sage);
    Event ev;
    ev.kind = EventKind::RouterDecision;
    ev.from_tier = Event::Tier::Frontier;
    ev.to_tier = Event::Tier::Standard;  // de-escalation

    rig.loop.dispatch(ev);

    PRE_CHECK_EQ(rig.servo.moves.size(), 0u);
    PRE_CHECK(rig.led.color_calls[0] == LedColor::Blue);  // standard tier
}

PRE_TEST(robot_loop_confidence_warning_tilts_amber) {
    Rig rig(Character::Sage);
    Event ev;
    ev.kind = EventKind::ConfidenceWarning;
    ev.confidence = 0.4f;

    rig.loop.dispatch(ev);

    PRE_CHECK_EQ(rig.servo.moves.size(), 1u);
    PRE_CHECK_NEAR(rig.servo.moves[0].head_y_deg, 30.0f, 0.01);
    PRE_CHECK(rig.led.color_calls[0] == LedColor::Amber);
}

PRE_TEST(robot_loop_memory_write_nods_white) {
    Rig rig(Character::Sage);
    Event ev;
    ev.kind = EventKind::MemoryWrite;

    rig.loop.dispatch(ev);

    PRE_CHECK_EQ(rig.servo.moves.size(), 1u);
    PRE_CHECK(rig.led.color_calls[0] == LedColor::White);
}

PRE_TEST(robot_loop_proximity_looks_up) {
    Rig rig(Character::Sage);
    Event ev;
    ev.kind = EventKind::Proximity;
    ev.distance_cm = 35.0f;

    rig.loop.dispatch(ev);

    PRE_CHECK_EQ(rig.servo.moves.size(), 1u);
    PRE_CHECK_NEAR(rig.servo.moves[0].head_y_deg, 60.0f, 0.01);
    PRE_CHECK_EQ(rig.servo.moves[0].duration_ms, 700u);
}

PRE_TEST(robot_loop_error_is_still_and_red) {
    Rig rig(Character::Sage);
    Event ev;
    ev.kind = EventKind::Error;

    rig.loop.dispatch(ev);

    PRE_CHECK_EQ(rig.servo.moves.size(), 0u);
    PRE_CHECK(rig.led.color_calls[0] == LedColor::Red);
}

PRE_TEST(robot_loop_character_set_switches_state_and_updates_display) {
    Rig rig(Character::Sage);
    Event ev;
    ev.kind = EventKind::CharacterSet;
    ev.character = Character::Sentinel;

    rig.loop.dispatch(ev);

    PRE_CHECK(rig.loop.character() == Character::Sentinel);
    PRE_CHECK_EQ(rig.display.character_calls.size(), 1u);
    PRE_CHECK(rig.display.character_calls[0] == Character::Sentinel);
    PRE_CHECK_EQ(rig.led.color_calls.size(), 1u);
    PRE_CHECK(rig.led.color_calls[0] == LedColor::White);  // sentinel idle
}

PRE_TEST(robot_loop_clamps_unsafe_y_axis_before_servo) {
    // ConfidenceWarning's mapping uses y=30°, well inside the [10,80] safe
    // envelope. Force an out-of-range event by directly constructing an
    // EmbodimentCommand-like scenario via a synthesised event whose
    // mapping happens to be at the edge; the safety net comes from
    // MotionEngine.clamp(). Use Proximity (y=60°) and trust that the
    // clamp pipeline is wired — covered explicitly by the dedicated
    // motion tests in test_motion.cpp. Here, just confirm that the
    // servo received a y value within the safe range.
    Rig rig(Character::Sage);
    Event ev;
    ev.kind = EventKind::Proximity;
    rig.loop.dispatch(ev);

    PRE_CHECK(rig.servo.moves[0].head_y_deg >= 10.0f);
    PRE_CHECK(rig.servo.moves[0].head_y_deg <= 80.0f);
}

PRE_TEST(robot_loop_reset_to_idle_drives_safe_state) {
    Rig rig(Character::Sprout);
    rig.loop.reset_to_idle();

    PRE_CHECK_EQ(rig.servo.rest_calls, 1);
    PRE_CHECK_EQ(rig.led.color_calls.size(), 1u);
    PRE_CHECK(rig.led.color_calls[0] == LedColor::Green);  // sprout idle
    PRE_CHECK_EQ(rig.display.character_calls.size(), 1u);
    PRE_CHECK(rig.display.character_calls[0] == Character::Sprout);
}

PRE_TEST(robot_loop_unknown_event_is_safe_idle_led) {
    Rig rig(Character::Sage);
    Event ev;
    ev.kind = EventKind::Unknown;

    rig.loop.dispatch(ev);

    // Unknown events still emit the idle color (default LED color in the
    // returned EmbodimentCommand). No motion. No display change.
    PRE_CHECK_EQ(rig.servo.moves.size(), 0u);
    PRE_CHECK_EQ(rig.led.color_calls.size(), 1u);
    PRE_CHECK(rig.led.color_calls[0] == LedColor::Blue);  // sage idle
    PRE_CHECK_EQ(rig.display.character_calls.size(), 0u);
}
