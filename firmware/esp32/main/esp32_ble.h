// SPDX-License-Identifier: TBD
// ESP32-S3 IBleTransport implementation — Nordic UART Service peripheral
// over NimBLE (preferred for the IDF v5.x stack on ESP32-S3).
//
// Skeleton only — not compiled in host-test CI. UUIDs come from
// firmware/core/include/pre_buddy/hal/uuids.h.

#pragma once

#include "pre_buddy/hal/i_ble_transport.h"
#include "pre_buddy/line_framer.h"

namespace pre_buddy::esp32 {

class Esp32BleTransport : public hal::IBleTransport {
   public:
    Esp32BleTransport() noexcept = default;

    // The framer accumulates partial GATT writes from the central into
    // complete \n-terminated lines. Exposed so the RX callback can feed it
    // directly. Sized for the largest line we receive — base64 PCM16 audio
    // output frames (~1.4 KB), well past the ~300 B control events.
    LineFramer<2048>& framer() noexcept { return framer_; }

    void start(std::string_view device_name) noexcept override;
    void stop() noexcept override;
    bool is_connected() const noexcept override;

    bool has_incoming() const noexcept override;
    std::size_t pop_incoming(char* out_buf, std::size_t max_len) noexcept override;
    bool send_line(std::string_view line) noexcept override;

   private:
    LineFramer<2048> framer_;
};

}  // namespace pre_buddy::esp32
