// SPDX-License-Identifier: TBD
// Tests for the Expression enum + event→expression mapping.

#include "pre_buddy/expression.h"
#include "pre_buddy/protocol.h"
#include "test_harness.h"

using namespace pre_buddy;

// ── Expression enum round-trip ────────────────────────────────────────

PRE_TEST(expression_to_string_and_parse_round_trip) {
    const Expression all[] = {
        Expression::Neutral,  Expression::Surprised, Expression::Thinking,
        Expression::Concerned, Expression::Happy,    Expression::Sleepy,
        Expression::Curious,   Expression::Error,
    };
    for (Expression e : all) {
        Expression decoded = Expression::Neutral;
        PRE_CHECK(parse_expression(to_string(e), decoded));
        PRE_CHECK(decoded == e);
    }
}

PRE_TEST(expression_parse_rejects_unknown) {
    Expression out = Expression::Neutral;
    PRE_CHECK(!parse_expression("dreamy", out));
    PRE_CHECK(!parse_expression("", out));
}

// ── map_event sets the expression for each event kind ────────────────

namespace {

Expression expression_for(EventKind k) {
    Event ev;
    ev.kind = k;
    // Defaults that exercise the most common branches.
    ev.source_mic = Event::Mic::Left;
    ev.from_tier = Event::Tier::Fast;
    ev.to_tier = Event::Tier::Frontier;  // escalation
    ev.confidence = 0.4f;                // low → concerned snapshot
    ev.minutes_until = 30;               // near-term scheduler
    return map_event(ev, Character::Sage).expression;
}

}  // namespace

PRE_TEST(wake_word_is_surprised) {
    PRE_CHECK(expression_for(EventKind::WakeWord) == Expression::Surprised);
}

PRE_TEST(bg_agent_change_is_thinking) {
    PRE_CHECK(expression_for(EventKind::BgAgentChange) == Expression::Thinking);
}

PRE_TEST(router_escalation_is_curious) {
    PRE_CHECK(expression_for(EventKind::RouterDecision) == Expression::Curious);
}

PRE_TEST(router_lateral_is_thinking) {
    Event ev;
    ev.kind = EventKind::RouterDecision;
    ev.from_tier = Event::Tier::Frontier;
    ev.to_tier = Event::Tier::Standard;  // de-escalation
    PRE_CHECK(map_event(ev, Character::Sage).expression == Expression::Thinking);
}

PRE_TEST(confidence_warning_is_concerned) {
    PRE_CHECK(expression_for(EventKind::ConfidenceWarning) == Expression::Concerned);
}

PRE_TEST(confidence_snapshot_low_is_concerned_high_is_neutral) {
    Event low;
    low.kind = EventKind::ConfidenceSnapshot;
    low.confidence = 0.5f;
    PRE_CHECK(map_event(low, Character::Sage).expression == Expression::Concerned);

    Event high;
    high.kind = EventKind::ConfidenceSnapshot;
    high.confidence = 0.9f;
    PRE_CHECK(map_event(high, Character::Sage).expression == Expression::Neutral);
}

PRE_TEST(kg_delta_is_thinking) {
    PRE_CHECK(expression_for(EventKind::KgDelta) == Expression::Thinking);
}

PRE_TEST(training_progress_is_thinking) {
    PRE_CHECK(expression_for(EventKind::TrainingProgress) == Expression::Thinking);
}

PRE_TEST(scheduler_near_term_is_surprised_far_term_is_neutral) {
    Event near;
    near.kind = EventKind::SchedulerUpcoming;
    near.minutes_until = 60;
    PRE_CHECK(map_event(near, Character::Sage).expression == Expression::Surprised);

    Event far;
    far.kind = EventKind::SchedulerUpcoming;
    far.minutes_until = 600;
    PRE_CHECK(map_event(far, Character::Sage).expression == Expression::Neutral);
}

PRE_TEST(tools_rollup_is_neutral) {
    PRE_CHECK(expression_for(EventKind::ToolsRollup) == Expression::Neutral);
}

PRE_TEST(memory_write_is_happy) {
    PRE_CHECK(expression_for(EventKind::MemoryWrite) == Expression::Happy);
}

PRE_TEST(proximity_is_curious) {
    PRE_CHECK(expression_for(EventKind::Proximity) == Expression::Curious);
}

PRE_TEST(error_is_error) {
    PRE_CHECK(expression_for(EventKind::Error) == Expression::Error);
}

PRE_TEST(character_set_is_happy) {
    PRE_CHECK(expression_for(EventKind::CharacterSet) == Expression::Happy);
}

PRE_TEST(unknown_is_neutral) {
    PRE_CHECK(expression_for(EventKind::Unknown) == Expression::Neutral);
}
