// SPDX-License-Identifier: TBD
// Tests for the platform-independent driver math:
//   - led_palette.h:  LedColor → Rgb888 + brightness scaling
//   - servo_math.h:   angle (0..180°) → LEDC duty
//   - line_framer.h:  byte stream → newline-terminated frames

#include <cstdint>
#include <string>

#include "pre_buddy/led_palette.h"
#include "pre_buddy/line_framer.h"
#include "pre_buddy/servo_math.h"
#include "test_harness.h"

using namespace pre_buddy;

// ── LED palette ──────────────────────────────────────────────────────


PRE_TEST(led_palette_off_is_black) {
    auto rgb = to_rgb888(LedColor::Off);
    PRE_CHECK_EQ(rgb.r, 0u);
    PRE_CHECK_EQ(rgb.g, 0u);
    PRE_CHECK_EQ(rgb.b, 0u);
}

PRE_TEST(led_palette_blue_matches_viewer_hex) {
    auto rgb = to_rgb888(LedColor::Blue);
    PRE_CHECK_EQ(rgb.r, 0x3Bu);
    PRE_CHECK_EQ(rgb.g, 0x82u);
    PRE_CHECK_EQ(rgb.b, 0xF6u);
}

PRE_TEST(led_palette_all_colors_are_distinct) {
    LedColor all[] = {
        LedColor::Off, LedColor::Blue,   LedColor::Green,  LedColor::Amber,
        LedColor::Red, LedColor::White,  LedColor::Cyan,   LedColor::Purple,
        LedColor::Yellow,
    };
    int collisions = 0;
    for (std::size_t i = 0; i < sizeof(all) / sizeof(all[0]); ++i) {
        for (std::size_t j = i + 1; j < sizeof(all) / sizeof(all[0]); ++j) {
            if (to_rgb888(all[i]) == to_rgb888(all[j])) ++collisions;
        }
    }
    PRE_CHECK_EQ(collisions, 0);
}

PRE_TEST(led_brightness_zero_blackens_any_color) {
    auto dim = apply_brightness(to_rgb888(LedColor::Yellow), 0);
    PRE_CHECK_EQ(dim.r, 0u);
    PRE_CHECK_EQ(dim.g, 0u);
    PRE_CHECK_EQ(dim.b, 0u);
}

PRE_TEST(led_brightness_255_preserves_color) {
    auto full = apply_brightness(to_rgb888(LedColor::Cyan), 255);
    auto raw = to_rgb888(LedColor::Cyan);
    PRE_CHECK(full == raw);
}

PRE_TEST(led_brightness_half_dims_proportionally) {
    auto half = apply_brightness(to_rgb888(LedColor::White), 128);
    auto raw = to_rgb888(LedColor::White);
    // (X * 128) / 255 ≈ X/2 within integer truncation.
    PRE_CHECK(half.r <= raw.r / 2 + 1);
    PRE_CHECK(half.r >= raw.r / 2 - 1);
}


// ── servo math ───────────────────────────────────────────────────────


PRE_TEST(servo_zero_deg_maps_to_pulse_500us) {
    // At 50 Hz, 500 µs out of 20 000 µs = 1/40 of the duty range.
    // With 16-bit resolution (65535), that's ~1638.
    auto duty = angle_to_duty(0.0f, 16, {});
    PRE_CHECK(duty >= 1620u);
    PRE_CHECK(duty <= 1660u);
}

PRE_TEST(servo_180_deg_maps_to_pulse_2500us) {
    // 2500 / 20 000 = 1/8 → 65535/8 ≈ 8191.
    auto duty = angle_to_duty(180.0f, 16, {});
    PRE_CHECK(duty >= 8170u);
    PRE_CHECK(duty <= 8210u);
}

PRE_TEST(servo_90_deg_is_midway_between_endpoints) {
    auto low = angle_to_duty(0.0f);
    auto high = angle_to_duty(180.0f);
    auto mid = angle_to_duty(90.0f);
    // Within rounding tolerance of the linear interpolation.
    auto expected = (low + high) / 2;
    auto diff = (mid > expected) ? mid - expected : expected - mid;
    PRE_CHECK(diff <= 2u);
}

PRE_TEST(servo_angle_below_zero_clamps_safely) {
    PRE_CHECK_EQ(angle_to_duty(-50.0f, 16, {}), angle_to_duty(0.0f, 16, {}));
}

PRE_TEST(servo_angle_above_180_clamps_safely) {
    PRE_CHECK_EQ(angle_to_duty(300.0f, 16, {}), angle_to_duty(180.0f, 16, {}));
}

PRE_TEST(servo_custom_calibration_changes_pulse_widths) {
    // A unit with longer pulse range: 1000 µs ↔ 2000 µs.
    ServoCalibration cal{1000, 2000, 20000};
    auto duty_0 = angle_to_duty(0.0f, 16, cal);
    auto duty_180 = angle_to_duty(180.0f, 16, cal);
    // duty_0 should now be ~3277 (1000/20000 * 65535).
    PRE_CHECK(duty_0 >= 3260u && duty_0 <= 3290u);
    // duty_180 should be ~6553 (2000/20000 * 65535).
    PRE_CHECK(duty_180 >= 6540u && duty_180 <= 6570u);
}


// ── line framer ──────────────────────────────────────────────────────


PRE_TEST(framer_emits_complete_line_on_newline) {
    LineFramer<64> f;
    const char* in = "{\"event\":\"pre.system.proximity\"}\n";
    PRE_CHECK(f.feed(reinterpret_cast<const std::uint8_t*>(in), std::strlen(in)));
    PRE_CHECK(f.has_line());
    PRE_CHECK(f.pop_line() == "{\"event\":\"pre.system.proximity\"}");
    PRE_CHECK(!f.has_line());
}

PRE_TEST(framer_holds_partial_line_across_writes) {
    LineFramer<64> f;
    const char* first  = "{\"event\":\"";
    const char* second = "pre.system.error\"}\n";
    PRE_CHECK(!f.feed(reinterpret_cast<const std::uint8_t*>(first), std::strlen(first)));
    PRE_CHECK(!f.has_line());
    PRE_CHECK(f.feed(reinterpret_cast<const std::uint8_t*>(second), std::strlen(second)));
    PRE_CHECK(f.pop_line() == "{\"event\":\"pre.system.error\"}");
}

PRE_TEST(framer_reset_clears_state) {
    LineFramer<64> f;
    const char* line = "abc\n";
    f.feed(reinterpret_cast<const std::uint8_t*>(line), 4);
    PRE_CHECK(f.has_line());
    f.reset();
    PRE_CHECK(!f.has_line());
    PRE_CHECK(!f.overflowed());
}

PRE_TEST(framer_marks_overflow_when_line_exceeds_buffer) {
    LineFramer<8> f;
    const char* in = "0123456789ABCDEF";
    f.feed(reinterpret_cast<const std::uint8_t*>(in), std::strlen(in));
    PRE_CHECK(f.overflowed());
    PRE_CHECK(!f.has_line());
}

PRE_TEST(framer_handles_empty_line) {
    LineFramer<32> f;
    const std::uint8_t nl = '\n';
    f.feed(&nl, 1);
    PRE_CHECK(f.has_line());
    PRE_CHECK(f.pop_line().empty());
}
