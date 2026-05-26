// SPDX-License-Identifier: TBD
// ESP32-S3 IServoDriver implementation via LEDC PWM.
//
// Wiring (M5Stack CoreS3, S0 build plan):
//   X-axis servo (yaw)  → GPIO ??  via LEDC channel 0
//   Y-axis servo (pitch)→ GPIO ??  via LEDC channel 1
// LEDC config: 50 Hz frequency, 16-bit duty resolution. Servo pulse width
// 500..2500 µs maps to 0..180°.
//
// Skeleton only — not compiled in host-test CI. The contract is locked in
// by IServoDriver in firmware/core/include/pre_buddy/hal/i_servo.h.

#pragma once

#include "pre_buddy/hal/i_servo.h"

namespace pre_buddy::esp32 {

class Esp32ServoDriver : public hal::IServoDriver {
   public:
    Esp32ServoDriver() noexcept = default;

    // Must be called once before any move(). Sets up LEDC timer + channels.
    // TODO: implement via driver/ledc.h.
    void init() noexcept;

    void move(const MotionCommand& cmd) noexcept override;
    void rest() noexcept override;
    void disable() noexcept override;

   private:
    bool initialised_ = false;
};

}  // namespace pre_buddy::esp32
