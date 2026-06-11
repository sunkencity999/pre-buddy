// SPDX-License-Identifier: TBD
// ESP32-S3 IBleTransport implementation — Nordic UART Service peripheral
// over NimBLE (preferred for the IDF v5.x stack on ESP32-S3).
//
// Skeleton only — not compiled in host-test CI. UUIDs come from
// firmware/core/include/pre_buddy/hal/uuids.h.

#pragma once

#include "pre_buddy/hal/i_ble_transport.h"

namespace pre_buddy::esp32 {

class Esp32BleTransport : public hal::IBleTransport {
   public:
    Esp32BleTransport() noexcept = default;

    // Inbound lines are reassembled from fragmented GATT writes and queued in a
    // FreeRTOS message buffer by the RX callback (see esp32_ble.cpp); the main
    // loop drains them via has_incoming()/pop_incoming(). The queue lets fast
    // write-without-response bursts land without dropping lines.
    void start(std::string_view device_name) noexcept override;
    void stop() noexcept override;
    bool is_connected() const noexcept override;

    bool has_incoming() const noexcept override;
    std::size_t pop_incoming(char* out_buf, std::size_t max_len) noexcept override;
    bool send_line(std::string_view line) noexcept override;
};

}  // namespace pre_buddy::esp32
