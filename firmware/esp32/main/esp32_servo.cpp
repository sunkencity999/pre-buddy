// SPDX-License-Identifier: TBD
// ESP32-S3 IServoDriver — stub implementation.
//
// Compiled inside an ESP-IDF build only; the host-test CMake target does
// NOT include this file. See firmware/esp32/README.md for the bring-up
// plan and the GPIO/LEDC channel assignments TODOs.

#include "esp32_servo.h"

#include "pre_buddy/servo_math.h"

namespace pre_buddy::esp32 {

void Esp32ServoDriver::init() noexcept {
    // TODO: configure ledc_timer_config_t for 50Hz / 16-bit and
    // ledc_channel_config_t for X and Y, then ledc_timer_config /
    // ledc_channel_config.
    initialised_ = true;
}

void Esp32ServoDriver::move(const MotionCommand& cmd) noexcept {
    if (!initialised_) return;
    // angle_to_duty() lives in firmware/core and is host-tested. The X
    // axis spans -90..+90° in cmd.head_x_deg space; shift to 0..180 to
    // match the servo's native travel.
    const auto duty_x = angle_to_duty(cmd.head_x_deg + 90.0f);
    const auto duty_y = angle_to_duty(cmd.head_y_deg);
    (void)duty_x;
    (void)duty_y;
    // TODO: ledc_set_fade_with_time(channel_x, duty_x, cmd.duration_ms);
    //       ledc_set_fade_with_time(channel_y, duty_y, cmd.duration_ms);
    //       ledc_fade_start(...) on both.
}

void Esp32ServoDriver::rest() noexcept {
    if (!initialised_) return;
    // TODO: snap both channels to centered duty.
}

void Esp32ServoDriver::disable() noexcept {
    // TODO: ledc_stop on both channels so the head goes limp safely.
    initialised_ = false;
}

}  // namespace pre_buddy::esp32
