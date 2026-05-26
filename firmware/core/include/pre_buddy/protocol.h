// SPDX-License-Identifier: TBD
// PRE Buddy — protocol event types + event→embodiment mapping.
//
// Matches shared/protocol/events.md.

#pragma once

#include <cstdint>
#include <string_view>

#include "pre_buddy/character.h"
#include "pre_buddy/motion.h"

namespace pre_buddy {

enum class EventKind : std::uint8_t {
    WakeWord = 0,
    BgAgentChange,
    RouterDecision,
    ConfidenceWarning,
    ConfidenceSnapshot,
    KgDelta,
    TrainingProgress,
    SchedulerUpcoming,
    ToolsRollup,
    MemoryWrite,
    Proximity,
    Error,
    CharacterSet,
    Unknown,
};

inline EventKind parse_event_kind(std::string_view name) noexcept {
    if (name == "pre.system.wake_word")       return EventKind::WakeWord;
    if (name == "pre.bg_agents.change")       return EventKind::BgAgentChange;
    if (name == "pre.router.decision")        return EventKind::RouterDecision;
    if (name == "pre.confidence.warning")     return EventKind::ConfidenceWarning;
    if (name == "pre.confidence.snapshot")    return EventKind::ConfidenceSnapshot;
    if (name == "pre.kg.delta")              return EventKind::KgDelta;
    if (name == "pre.training.progress")      return EventKind::TrainingProgress;
    if (name == "pre.scheduler.upcoming")     return EventKind::SchedulerUpcoming;
    if (name == "pre.tools.rollup")           return EventKind::ToolsRollup;
    if (name == "pre.system.memory_write")    return EventKind::MemoryWrite;
    if (name == "pre.system.proximity")       return EventKind::Proximity;
    if (name == "pre.system.error")           return EventKind::Error;
    if (name == "pre.character.set")          return EventKind::CharacterSet;
    return EventKind::Unknown;
}

struct EmbodimentCommand {
    MotionCommand motion;
    LedColor led;
    bool has_motion;  // false → no head movement (LED-only event)
};

struct Event {
    EventKind kind = EventKind::Unknown;

    enum class Mic : std::uint8_t { Unknown = 0, Left, Right } source_mic = Mic::Unknown;
    enum class Tier : std::uint8_t { Fast = 0, Standard, Frontier } tier = Tier::Fast;

    // Router events use both from/to.
    Tier from_tier = Tier::Fast;
    Tier to_tier = Tier::Fast;

    // Confidence events.
    float confidence = 1.0f;
    float threshold = 0.6f;

    // Scheduler.
    std::int32_t minutes_until = 999;

    // Proximity.
    float distance_cm = 100.0f;

    // Character.
    Character character = Character::Sage;
};

constexpr int tier_rank(Event::Tier t) noexcept {
    switch (t) {
        case Event::Tier::Fast: return 0;
        case Event::Tier::Standard: return 1;
        case Event::Tier::Frontier: return 2;
    }
    return 0;
}

inline LedColor tier_color(Event::Tier t) noexcept {
    switch (t) {
        case Event::Tier::Fast: return LedColor::Green;
        case Event::Tier::Standard: return LedColor::Blue;
        case Event::Tier::Frontier: return LedColor::Purple;
    }
    return LedColor::Green;
}

// Map an incoming event to an embodiment command for the given character.
inline EmbodimentCommand map_event(const Event& ev, Character ch) noexcept {
    const CharacterProfile prof = profile_for(ch);
    EmbodimentCommand out{};
    out.has_motion = false;
    out.led = prof.idle_color;
    out.motion.duration_ms = prof.reaction_ms;
    out.motion.head_y_deg = 45.0f;
    out.motion.head_x_deg = 0.0f;

    switch (ev.kind) {
        case EventKind::WakeWord:
            out.has_motion = true;
            if (ev.source_mic == Event::Mic::Left) out.motion.head_x_deg = -35.0f;
            if (ev.source_mic == Event::Mic::Right) out.motion.head_x_deg = 35.0f;
            out.led = (ch == Character::Sprout) ? LedColor::Yellow : prof.idle_color;
            break;

        case EventKind::BgAgentChange:
            out.has_motion = false;
            out.led = tier_color(ev.tier);
            break;

        case EventKind::RouterDecision: {
            out.led = tier_color(ev.to_tier);
            const bool escalating = tier_rank(ev.to_tier) > tier_rank(ev.from_tier);
            // Escalations get a subtle nod; lateral/downshift stays LED-only.
            if (escalating) {
                out.has_motion = true;
                out.motion.head_y_deg = 38.0f;
                out.motion.duration_ms = prof.reaction_ms + 200;
            }
            break;
        }

        case EventKind::ConfidenceWarning:
            out.has_motion = true;
            out.motion.head_x_deg = 0.0f;
            out.motion.head_y_deg = 30.0f;  // tilt down a bit
            out.led = LedColor::Amber;
            break;

        case EventKind::ConfidenceSnapshot:
            out.has_motion = false;
            // If confidence is low-ish, tint amber; otherwise idle color.
            out.led = (ev.confidence < 0.7f) ? LedColor::Amber : prof.idle_color;
            break;

        case EventKind::KgDelta:
            out.has_motion = false;
            out.led = LedColor::Cyan;
            break;

        case EventKind::TrainingProgress:
            out.has_motion = false;
            out.led = LedColor::White;
            break;

        case EventKind::SchedulerUpcoming:
            out.has_motion = false;
            // Near-term event: amber reminder pulse.
            out.led = (ev.minutes_until <= 120) ? LedColor::Amber : LedColor::Blue;
            break;

        case EventKind::ToolsRollup:
            out.has_motion = false;
            out.led = LedColor::White;
            break;

        case EventKind::MemoryWrite:
            // Slow nod + soft LED flash.
            out.has_motion = true;
            out.motion.head_y_deg = 35.0f;
            out.motion.duration_ms = prof.reaction_ms + 300;
            out.led = LedColor::White;
            break;

        case EventKind::Proximity:
            // User approaches: look up.
            out.has_motion = true;
            out.motion.head_y_deg = 60.0f;
            out.motion.duration_ms = 700;
            out.led = prof.idle_color;
            break;

        case EventKind::Error:
            out.has_motion = false;
            out.led = LedColor::Red;
            break;

        case EventKind::CharacterSet:
            out.has_motion = true;
            out.motion.head_y_deg = 35.0f;
            out.led = profile_for(ev.character).idle_color;
            break;

        case EventKind::Unknown:
            break;
    }

    return out;
}

}  // namespace pre_buddy
