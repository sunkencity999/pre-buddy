// SPDX-License-Identifier: TBD
// ESP32-S3 IServoDriver — stub implementation.
//
// Compiled inside an ESP-IDF build only; the host-test CMake target does
// NOT include this file. See firmware/esp32/README.md for the bring-up
// plan and the GPIO/LEDC channel assignments TODOs.

#include "esp32_servo.h"

namespace pre_buddy::esp32 {

void Esp32ServoDriver::init() noexcept {
    // TODO: configure ledc_timer_config_t for 50Hz / 16-bit and
    // ledc_channel_config_t for X and Y, then ledc_timer_config /
    // ledc_channel_config.
    initialised_ = true;
}

void Esp32ServoDriver::move(const MotionCommand& cmd) noexcept {
    if (!initialised_) return;
    (void)cmd;
    // TODO: map cmd.head_x_deg + cmd.head_y_deg to LEDC duty, then
    // ramp from current → target over cmd.duration_ms using an esp_timer
    // (or just ledc_set_fade_with_time when good enough).
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
