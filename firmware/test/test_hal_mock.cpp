// SPDX-License-Identifier: TBD
// Tests for the mock HAL drivers themselves — make sure the recording
// behavior is solid before tests use it to assert robot behavior.

#include "pre_buddy/hal/mock.h"
#include "test_harness.h"

using namespace pre_buddy;
using namespace pre_buddy::hal;

PRE_TEST(mock_servo_records_each_move_in_order) {
    MockServoDriver s;
    MotionCommand a{-35.0f, 45.0f, 350};
    MotionCommand b{0.0f, 60.0f, 700};
    s.move(a);
    s.move(b);

    PRE_CHECK_EQ(s.moves.size(), 2u);
    PRE_CHECK_NEAR(s.moves[0].head_x_deg, -35.0f, 0.01);
    PRE_CHECK_NEAR(s.moves[1].head_y_deg, 60.0f, 0.01);
    PRE_CHECK_EQ(s.moves[1].duration_ms, 700u);
}

PRE_TEST(mock_servo_counts_rest_and_disable) {
    MockServoDriver s;
    s.rest();
    s.rest();
    s.disable();

    PRE_CHECK_EQ(s.rest_calls, 2);
    PRE_CHECK_EQ(s.disable_calls, 1);
}

PRE_TEST(mock_led_tracks_color_brightness_and_off) {
    MockLedDriver l;
    l.set_color(LedColor::Red);
    l.set_brightness(64);
    l.off();
    l.set_color(LedColor::Cyan);

    PRE_CHECK_EQ(l.color_calls.size(), 2u);
    PRE_CHECK(l.color_calls[0] == LedColor::Red);
    PRE_CHECK(l.color_calls[1] == LedColor::Cyan);
    PRE_CHECK_EQ(l.brightness_calls.size(), 1u);
    PRE_CHECK_EQ(l.brightness_calls[0], static_cast<unsigned char>(64));
    PRE_CHECK_EQ(l.off_calls, 1);
}

PRE_TEST(mock_display_tracks_character_banner_passkey_clear) {
    MockDisplayDriver d;
    d.show_character(Character::Sage);
    d.show_banner("hi");
    d.show_passkey(123456);
    d.clear();

    PRE_CHECK_EQ(d.character_calls.size(), 1u);
    PRE_CHECK(d.character_calls[0] == Character::Sage);
    PRE_CHECK_EQ(d.banner_calls.size(), 1u);
    PRE_CHECK(d.banner_calls[0] == "hi");
    PRE_CHECK_EQ(d.passkey_calls.size(), 1u);
    PRE_CHECK_EQ(d.passkey_calls[0], 123456u);
    PRE_CHECK_EQ(d.clear_calls, 1);
}

PRE_TEST(mock_ble_transport_start_marks_connected_and_records_name) {
    MockBleTransport b;
    PRE_CHECK(!b.is_connected());
    b.start("pre-buddy-dev");
    PRE_CHECK(b.is_connected());
    PRE_CHECK(b.started);
    PRE_CHECK(b.device_name == "pre-buddy-dev");
}

PRE_TEST(mock_ble_transport_round_trips_lines_in_fifo_order) {
    MockBleTransport b;
    b.start("x");
    b.inject_incoming("first");
    b.inject_incoming("second");

    PRE_CHECK(b.has_incoming());
    char buf[64] = {};
    std::size_t n = b.pop_incoming(buf, sizeof(buf));
    PRE_CHECK_EQ(n, 5u);
    PRE_CHECK(std::string(buf, n) == "first");
    n = b.pop_incoming(buf, sizeof(buf));
    PRE_CHECK_EQ(n, 6u);
    PRE_CHECK(std::string(buf, n) == "second");
    PRE_CHECK(!b.has_incoming());
}

PRE_TEST(mock_ble_transport_truncates_oversized_lines_safely) {
    // pop_incoming honours the caller's buffer size; the remainder is
    // dropped (caller decides whether that's an error). Mirrors the
    // worst-case behavior of a fixed-size MTU on hardware.
    MockBleTransport b;
    b.start("x");
    b.inject_incoming("this string is longer than the buffer");

    char buf[8] = {};
    std::size_t n = b.pop_incoming(buf, sizeof(buf));
    PRE_CHECK_EQ(n, 8u);
    PRE_CHECK(std::string(buf, n) == "this str");
}

PRE_TEST(mock_ble_transport_send_requires_connected) {
    MockBleTransport b;
    PRE_CHECK(!b.send_line("nope"));
    PRE_CHECK_EQ(b.sent.size(), 0u);
    b.start("x");
    PRE_CHECK(b.send_line("ok"));
    PRE_CHECK_EQ(b.sent.size(), 1u);
    PRE_CHECK(b.sent[0] == "ok");
}
