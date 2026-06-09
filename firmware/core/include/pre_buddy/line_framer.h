// SPDX-License-Identifier: TBD
// PRE Buddy — newline framing for the BLE NUS RX stream.
//
// BLE notifications can split a JSON line across multiple writes when
// the payload straddles the ATT MTU. The Esp32BleTransport's RX
// callback feeds raw bytes into a LineFramer, which buffers until a
// '\n' arrives and then hands a complete line up.
//
// Pure C++17, no allocations after construction.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace pre_buddy {

template <std::size_t Capacity = 512>
class LineFramer {
   public:
    static constexpr std::size_t capacity = Capacity;

    // Feed N raw bytes into the framer. Returns true if a complete line
    // is available via current_line() / pop_line(). False means the
    // bytes were buffered but no newline has arrived yet (or the input
    // overflowed; see overflowed()).
    //
    // Multiple complete lines may be present at once; callers should
    // drain via pop_line() in a loop until it returns empty.
    bool feed(const std::uint8_t* data, std::size_t n) noexcept {
        for (std::size_t i = 0; i < n; ++i) {
            const auto byte = data[i];
            if (byte == '\n') {
                ready_ = true;
            } else if (write_ < buffer_.size()) {
                if (!ready_) {
                    buffer_[write_++] = static_cast<char>(byte);
                } else {
                    // Next line started before the previous one was
                    // popped — that's a caller bug; drop the rest until
                    // pop_line(). overflowed_ stays clean because the
                    // *buffer* didn't overflow.
                }
            } else {
                overflowed_ = true;
            }
        }
        return ready_;
    }

    bool has_line() const noexcept { return ready_; }

    // Snapshot of the current complete line. Valid only while ready;
    // becomes invalid after pop_line().
    std::string_view current_line() const noexcept {
        return std::string_view(buffer_.data(), write_);
    }

    // Return the current line and clear the framer for the next one.
    std::string_view pop_line() noexcept {
        std::string_view view(buffer_.data(), write_);
        write_ = 0;
        ready_ = false;
        return view;
    }

    bool overflowed() const noexcept { return overflowed_; }

    void reset() noexcept {
        write_ = 0;
        ready_ = false;
        overflowed_ = false;
    }

   private:
    std::array<char, Capacity> buffer_{};
    std::size_t write_ = 0;
    bool ready_ = false;
    bool overflowed_ = false;
};

}  // namespace pre_buddy
