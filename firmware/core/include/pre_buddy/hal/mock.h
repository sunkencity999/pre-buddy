// SPDX-License-Identifier: TBD
// PRE Buddy — host-testable mock HAL drivers.
//
// Each mock implements the matching interface from i_servo.h / i_led.h /
// i_display.h and records every call into a vector. Tests then assert
// against those vectors. No threading, no real hardware.
//
// Header-only so it lives alongside the rest of the core and works in
// any CMake config without an extra .cpp build target.

#pragma once

#include <cstddef>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "pre_buddy/hal/i_ble_transport.h"
#include "pre_buddy/hal/i_display.h"
#include "pre_buddy/hal/i_led.h"
#include "pre_buddy/hal/i_servo.h"
#include "pre_buddy/motion.h"

namespace pre_buddy::hal {

class MockServoDriver : public IServoDriver {
   public:
    std::vector<MotionCommand> moves;
    int rest_calls = 0;
    int disable_calls = 0;

    void move(const MotionCommand& cmd) noexcept override {
        moves.push_back(cmd);
    }
    void rest() noexcept override { ++rest_calls; }
    void disable() noexcept override { ++disable_calls; }
};

class MockLedDriver : public ILedDriver {
   public:
    std::vector<LedColor> color_calls;
    std::vector<unsigned char> brightness_calls;
    int off_calls = 0;

    void set_color(LedColor color) noexcept override {
        color_calls.push_back(color);
    }
    void set_brightness(unsigned char level) noexcept override {
        brightness_calls.push_back(level);
    }
    void off() noexcept override { ++off_calls; }
};

class MockDisplayDriver : public IDisplayDriver {
   public:
    std::vector<Character> character_calls;
    std::vector<std::pair<Character, Expression>> expression_calls;
    std::vector<std::string> banner_calls;
    std::vector<unsigned int> passkey_calls;
    int clear_calls = 0;

    void show_character(Character ch) noexcept override {
        character_calls.push_back(ch);
    }
    void show_expression(Character ch, Expression expr) noexcept override {
        expression_calls.emplace_back(ch, expr);
    }
    void show_banner(std::string_view text) noexcept override {
        banner_calls.emplace_back(text);
    }
    void show_passkey(unsigned int code) noexcept override {
        passkey_calls.push_back(code);
    }
    void clear() noexcept override { ++clear_calls; }
};

// Simple FIFO-backed BLE transport mock. Tests push incoming lines via
// inject_incoming() and read outgoing lines via sent. Connection state
// is toggled with set_connected().
class MockBleTransport : public IBleTransport {
   public:
    std::vector<std::string> sent;
    bool started = false;
    std::string device_name;

    void start(std::string_view name) noexcept override {
        started = true;
        device_name.assign(name);
        connected_ = true;
    }
    void stop() noexcept override {
        started = false;
        connected_ = false;
    }
    bool is_connected() const noexcept override { return connected_; }

    bool has_incoming() const noexcept override { return !incoming_.empty(); }

    std::size_t pop_incoming(char* out_buf, std::size_t max_len) noexcept override {
        if (incoming_.empty() || max_len == 0) return 0;
        std::string& head = incoming_.front();
        std::size_t n = head.size();
        if (n > max_len) n = max_len;
        std::memcpy(out_buf, head.data(), n);
        incoming_.erase(incoming_.begin());
        return n;
    }

    bool send_line(std::string_view line) noexcept override {
        if (!connected_) return false;
        sent.emplace_back(line);
        return true;
    }

    // Test helpers.
    void inject_incoming(std::string line) { incoming_.push_back(std::move(line)); }
    void set_connected(bool c) noexcept { connected_ = c; }

   private:
    std::vector<std::string> incoming_;
    bool connected_ = false;
};

}  // namespace pre_buddy::hal
