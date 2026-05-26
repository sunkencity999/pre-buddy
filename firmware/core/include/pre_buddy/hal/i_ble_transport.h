// SPDX-License-Identifier: TBD
// PRE Buddy — BLE transport interface (device side).
//
// On the ESP32 the device is the BLE peripheral: it advertises the NUS
// service, accepts writes on RX, and pushes JSON lines to the central
// via TX notifications. This interface hides the BLE stack from the
// robot loop so the latter is host-testable.

#pragma once

#include <cstddef>
#include <string_view>

namespace pre_buddy::hal {

class IBleTransport {
   public:
    virtual ~IBleTransport() = default;

    // Start the BLE stack and begin advertising the NUS service. Idempotent.
    // ``device_name`` is the advertising name (≤ 28 chars typically).
    virtual void start(std::string_view device_name) noexcept = 0;
    virtual void stop() noexcept = 0;
    virtual bool is_connected() const noexcept = 0;

    // True when at least one byte has been received from the central. The
    // robot loop polls this between motion ticks.
    virtual bool has_incoming() const noexcept = 0;

    // Pop one complete JSON line from RX. Returns the number of bytes
    // written into ``out_buf``. Zero means "no complete line yet."
    // Implementations MUST not include the terminating newline.
    virtual std::size_t pop_incoming(char* out_buf, std::size_t max_len) noexcept = 0;

    // Send one JSON line to the central via TX notify. The implementation
    // is responsible for any MTU-aware chunking. ``line`` MUST NOT contain
    // newlines.
    virtual bool send_line(std::string_view line) noexcept = 0;
};

}  // namespace pre_buddy::hal
