// SPDX-License-Identifier: TBD
// PRE Buddy — facial expressions.
//
// Eight discrete expressions cover every v1 event mapping without exploding
// the sprite budget on the ESP32 (8 expressions × 3 characters = 24 face
// states to draw or composite). Expressions are character-agnostic at the
// model level — each Character renders the same Expression with its own
// visual identity (Sage's calm eyes, Sprout's wide eyes, Sentinel's slit
// eyes), but the mood is shared.

#pragma once

#include <cstdint>
#include <string_view>

namespace pre_buddy {

enum class Expression : std::uint8_t {
    Neutral = 0,     // idle / nothing happening
    Surprised,       // wake_word, near-term scheduler reminder
    Thinking,        // bg_agents.change, kg.delta, training.progress
    Concerned,       // confidence_warning, low confidence_snapshot
    Happy,           // memory_write, character_set acknowledge
    Sleepy,          // power-save / quiet-hours (reserved; v1 doesn't emit)
    Curious,         // proximity, router_decision (escalation)
    Error,           // pre.system.error
};

constexpr std::string_view to_string(Expression e) noexcept {
    switch (e) {
        case Expression::Neutral:   return "neutral";
        case Expression::Surprised: return "surprised";
        case Expression::Thinking:  return "thinking";
        case Expression::Concerned: return "concerned";
        case Expression::Happy:     return "happy";
        case Expression::Sleepy:    return "sleepy";
        case Expression::Curious:   return "curious";
        case Expression::Error:     return "error";
    }
    return "neutral";
}

inline bool parse_expression(std::string_view s, Expression& out) noexcept {
    if (s == "neutral")   { out = Expression::Neutral;   return true; }
    if (s == "surprised") { out = Expression::Surprised; return true; }
    if (s == "thinking")  { out = Expression::Thinking;  return true; }
    if (s == "concerned") { out = Expression::Concerned; return true; }
    if (s == "happy")     { out = Expression::Happy;     return true; }
    if (s == "sleepy")    { out = Expression::Sleepy;    return true; }
    if (s == "curious")   { out = Expression::Curious;   return true; }
    if (s == "error")     { out = Expression::Error;     return true; }
    return false;
}

}  // namespace pre_buddy
