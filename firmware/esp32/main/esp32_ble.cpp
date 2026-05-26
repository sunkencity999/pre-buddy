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
    // TODO: peek an internal ring buffer fed by the RX write callback.
    return false;
}

std::size_t Esp32BleTransport::pop_incoming(char* out_buf, std::size_t max_len) noexcept {
    (void)out_buf;
    (void)max_len;
    // TODO: drain one line from the RX ring buffer (newline-framed).
    return 0;
}

bool Esp32BleTransport::send_line(std::string_view line) noexcept {
    (void)line;
    // TODO: ble_gatts_notify_custom on the TX characteristic handle.
    return false;
}

}  // namespace pre_buddy::esp32
