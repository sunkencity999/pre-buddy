// SPDX-License-Identifier: TBD
// PRE Buddy — servo driver interface.
//
// The robot has two servos: X (yaw / turn) and Y (pitch / nod). Both are
// driven through one IServoDriver instance. Implementations:
//   - MockServoDriver (host tests, records every call)
//   - Esp32ServoDriver (ESP32 LEDC PWM, lands when hardware arrives)
//
// Pure interface header — no concrete code, no ESP-IDF deps. Anything that
// can be ranged on without a board belongs in motion.h, not here.

#pragma once

#include "pre_buddy/motion.h"

namespace pre_buddy::hal {

class IServoDriver {
   public:
    virtual ~IServoDriver() = default;

    // Drive both servos toward the target pose over ``duration_ms``.
    // Implementations MUST honour the duration as a wall-clock budget;
    // motion is expected to be already clamped via MotionEngine, so the
    // driver shouldn't second-guess angles.
    virtual void move(const MotionCommand& cmd) noexcept = 0;

    // Snap both servos to their resting pose (X=0°, Y=45°) without ramp.
    // Called on startup and on graceful shutdown.
    virtual void rest() noexcept = 0;

    // Disable servo torque to save power / let the head go limp safely.
    // Used by the stall-detection escape hatch in DESIGN.md §4.2.
    virtual void disable() noexcept = 0;
};

}  // namespace pre_buddy::hal
