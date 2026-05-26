// SPDX-License-Identifier: TBD
// PRE Buddy — protocol event types + event→embodiment mapping.
//
// Matches shared/protocol/events.md. Lean parser: we accept the canonical
// event name and a small typed payload. Full JSON parsing lives outside
// the host-testable core (firmware will use ArduinoJson; the host tests
// build payloads directly).

#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "pre_buddy/character.h"
#include "pre_buddy/motion.h"

namespace pre_buddy {

enum class EventKind : std::uint8_t {
    WakeWord = 0,
    BgAgentChange,
    ConfidenceWarning,
    Error,
    CharacterSet,
    Unknown,
};

inline EventKind parse_event_kind(std::string_view name) noexcept {
    if (name == "pre.system.wake_word")       return EventKind::WakeWord;
    if (name == "pre.bg_agents.change")       return EventKind::BgAgentChange;
    if (name == "pre.confidence.warning")     return EventKind::ConfidenceWarning;
    if (name == "pre.system.error")           return EventKind::Error;
    if (name == "pre.character.set")          return EventKind::CharacterSet;
    return EventKind::Unknown;
}

struct EmbodimentCommand {
    MotionCommand motion;
    LedColor      led;
    bool          has_motion;  // false → no head movement (LED-only event)
};

// Minimal typed event payload understood by the initial firmware/server pair.
struct Event {
    EventKind kind = EventKind::Unknown;

    // wake_word
    enum class Mic : std::uint8_t { Unknown = 0, Left, Right } source_mic = Mic::Unknown;

    // bg_agents.change
    enum class Tier : std::uint8_t { Fast = 0, Standard, Frontier } tier = Tier::Fast;

    // confidence.warning
    float confidence = 1.0f;
    float threshold  = 0.6f;

    // character.set
    Character character = Character::Sage;
};

// Map an incoming event to an embodiment command for the given character.
// The mapping is intentionally small but real — see DESIGN.md §4.1.
inline EmbodimentCommand map_event(const Event& ev, Character ch) noexcept {
    const CharacterProfile prof = profile_for(ch);
    EmbodimentCommand out{};
    out.has_motion = false;
    out.led        = prof.idle_color;
    out.motion.duration_ms = prof.reaction_ms;
    out.motion.head_y_deg  = 45.0f;
    out.motion.head_x_deg  = 0.0f;

    switch (ev.kind) {
        case EventKind::WakeWord:
            // Head turns toward dominant mic; ±35° from center.
            out.has_motion = true;
            if (ev.source_mic == Event::Mic::Left)  out.motion.head_x_deg = -35.0f;
            if (ev.source_mic == Event::Mic::Right) out.motion.head_x_deg =  35.0f;
            out.led = (ch == Character::Sprout) ? LedColor::Yellow : prof.idle_color;
            break;

        case EventKind::BgAgentChange:
            // LED ring pulse on tier color, no head motion (motion budget).
            out.has_motion = false;
            switch (ev.tier) {
                case Event::Tier::Fast:     out.led = LedColor::Green;  break;
                case Event::Tier::Standard: out.led = LedColor::Blue;   break;
                case Event::Tier::Frontier: out.led = LedColor::Purple; break;
            }
            break;

        case EventKind::ConfidenceWarning:
            // Head tilt + amber LED.
            out.has_motion = true;
            out.motion.head_x_deg = 0.0f;
            out.motion.head_y_deg = 30.0f;  // tilt down a bit
            out.led = LedColor::Amber;
            break;

        case EventKind::Error:
            // LED red, NO motion (DESIGN.md: "we don't shake").
            out.has_motion = false;
            out.led = LedColor::Red;
            break;

        case EventKind::CharacterSet:
            // Acknowledge with a slow nod in the new character's idle color.
            out.has_motion = true;
            out.motion.head_y_deg = 35.0f;
            out.led = profile_for(ev.character).idle_color;
            break;

        case EventKind::Unknown:
            // Leave defaults; firmware will treat as no-op.
            break;
    }
    return out;
}

}  // namespace pre_buddy
