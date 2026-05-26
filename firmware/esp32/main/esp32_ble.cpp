// SPDX-License-Identifier: TBD
// ESP32-S3 IBleTransport — stub implementation.

#include "esp32_ble.h"

#include "pre_buddy/hal/uuids.h"

namespace pre_buddy::esp32 {

void Esp32BleTransport::start(std::string_view device_name) noexcept {
    (void)device_name;
    // TODO: nimble_port_init(), register NUS service with
    // nus::SERVICE_UUID / RX_CHAR_UUID / TX_CHAR_UUID, start advertising.
}

void Esp32BleTransport::stop() noexcept {
    // TODO: nimble_port_stop().
}

bool Esp32BleTransport::is_connected() const noexcept {
    // TODO: track in a gap event callback.
    return false;
}

bool Esp32BleTransport::has_incoming() const noexcept {
    // framer_ is fed inside the RX callback (see TODO in start()).
    return framer_.has_line();
}

std::size_t Esp32BleTransport::pop_incoming(char* out_buf, std::size_t max_len) noexcept {
    if (!framer_.has_line()) return 0;
    const auto line = framer_.pop_line();
    const auto n = (line.size() < max_len) ? line.size() : max_len;
    std::memcpy(out_buf, line.data(), n);
    return n;
}

bool Esp32BleTransport::send_line(std::string_view line) noexcept {
    (void)line;
    // TODO: ble_gatts_notify_custom on the TX characteristic handle.
    return false;
}

}  // namespace pre_buddy::esp32
