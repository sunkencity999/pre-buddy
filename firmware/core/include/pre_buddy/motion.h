// SPDX-License-Identifier: TBD
// PRE Buddy — motion safety + rate limiter.
//
// All servo commands flow through MotionEngine. It clamps the Y axis to the
// safety range (DESIGN.md §4.2) and rate-limits X-axis travel so we never
// issue a "sudden 180° spin." Pure C++17, no hardware deps.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace pre_buddy {

struct MotionLimits {
    float y_min_deg = 10.0f;
    float y_max_deg = 80.0f;
    // Maximum X-axis angular velocity in degrees per second.
    float max_x_deg_per_sec = 180.0f;
};

struct MotionCommand {
    float head_x_deg = 0.0f;
    float head_y_deg = 45.0f;
    std::uint32_t duration_ms = 500;
};

class MotionEngine {
   public:
    explicit MotionEngine(MotionLimits limits = {}) noexcept
        : limits_(limits), last_x_deg_(0.0f), have_last_(false) {}

    // Clamp an arbitrary command into the safety envelope.
    // Y is hard-clamped. X is left untouched (rate-limit handles X).
    MotionCommand clamp(MotionCommand cmd) const noexcept {
        cmd.head_y_deg =
            std::clamp(cmd.head_y_deg, limits_.y_min_deg, limits_.y_max_deg);
        return cmd;
    }

    // Apply the rate limit by lengthening duration_ms if necessary so the X
    // travel stays within max_x_deg_per_sec. Returns the adjusted command.
    // On the first call (no prior X position) duration is left alone.
    MotionCommand rate_limit(MotionCommand cmd) noexcept {
        if (!have_last_) {
            last_x_deg_ = cmd.head_x_deg;
            have_last_ = true;
            return cmd;
        }
        const float dx = std::fabs(cmd.head_x_deg - last_x_deg_);
        if (dx > 0.0f && limits_.max_x_deg_per_sec > 0.0f) {
            const float min_ms = (dx / limits_.max_x_deg_per_sec) * 1000.0f;
            if (static_cast<float>(cmd.duration_ms) < min_ms) {
                cmd.duration_ms = static_cast<std::uint32_t>(std::ceil(min_ms));
            }
        }
        last_x_deg_ = cmd.head_x_deg;
        return cmd;
    }

    // Convenience: clamp + rate limit in one call.
    MotionCommand sanitize(MotionCommand cmd) noexcept {
        return rate_limit(clamp(cmd));
    }

    const MotionLimits& limits() const noexcept { return limits_; }

    // Test/reset hook.
    void reset() noexcept {
        last_x_deg_ = 0.0f;
        have_last_ = false;
    }

   private:
    MotionLimits limits_;
    float last_x_deg_;
    bool have_last_;
};

}  // namespace pre_buddy
