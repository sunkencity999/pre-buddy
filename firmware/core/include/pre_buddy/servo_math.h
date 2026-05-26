// SPDX-License-Identifier: TBD
// PRE Buddy — servo angle ↔ PWM duty math.
//
// Standard hobby-servo timing: a 50 Hz signal (20 ms period) with a
// pulse width sweeping from 500 µs (= 0°) to 2500 µs (= 180°). The
// ESP-IDF LEDC peripheral represents duty as an integer fraction of
// ``2^resolution``; this header converts angles into those integer
// duties so the device-side driver is a one-liner.

#pragma once

#include <algorithm>
#include <cstdint>

namespace pre_buddy {

struct ServoCalibration {
    // Pulse widths in microseconds at the safety-clamped endpoints of
    // travel. Defaults match standard 9g hobby servos; expose them so
    // the calibration UI we'll add later can write per-unit values.
    std::uint16_t pulse_us_at_0_deg = 500;
    std::uint16_t pulse_us_at_180_deg = 2500;
    // PWM period in microseconds. 50 Hz = 20 ms = 20 000 µs.
    std::uint32_t period_us = 20000;
};

// Compute the integer LEDC duty value for a given angle.
//
// ``resolution_bits`` is the LEDC timer's duty resolution. ESP32-S3's
// LEDC supports up to 20 bits; 16 is a comfortable default that keeps
// jitter below the servo's own dead band.
//
// The angle is clamped to [0, 180]. Output is guaranteed to be ≤
// (2^resolution_bits) - 1.
constexpr std::uint32_t angle_to_duty(
    float angle_deg,
    std::uint8_t resolution_bits = 16,
    ServoCalibration cal = {}) noexcept {
    // Clamp angle.
    if (angle_deg < 0.0f) angle_deg = 0.0f;
    if (angle_deg > 180.0f) angle_deg = 180.0f;

    const float t = angle_deg / 180.0f;
    const float pulse_us =
        cal.pulse_us_at_0_deg +
        t * (cal.pulse_us_at_180_deg - cal.pulse_us_at_0_deg);

    // Convert pulse width fraction → integer duty in the LEDC range.
    const auto max_duty = (1u << resolution_bits) - 1u;
    auto duty =
        static_cast<std::uint32_t>((pulse_us / cal.period_us) * max_duty + 0.5f);

    if (duty > max_duty) duty = max_duty;
    return duty;
}

}  // namespace pre_buddy
