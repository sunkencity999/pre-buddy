// SPDX-License-Identifier: TBD
// PRE Buddy — Nordic UART Service UUIDs (BLE NUS framing).
//
// Single source of truth on the firmware side. The Python mirror lives at
// server/pre_buddy/uuids.py and the human-readable spec is
// shared/protocol/uuids.md. Keep all three in lockstep.

#pragma once

namespace pre_buddy::nus {

// 128-bit Nordic UART Service UUIDs. Stored as ASCII strings to keep the
// header platform-independent — the ESP32 BLE stack (NimBLE / Bluedroid)
// can parse these via its own UUID factory.
inline constexpr const char* SERVICE_UUID =
    "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
inline constexpr const char* RX_CHAR_UUID =  // central → peripheral, write
    "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";
inline constexpr const char* TX_CHAR_UUID =  // peripheral → central, notify
    "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";

}  // namespace pre_buddy::nus
