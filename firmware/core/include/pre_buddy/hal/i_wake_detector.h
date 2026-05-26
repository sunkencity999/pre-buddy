// SPDX-License-Identifier: TBD
// PRE Buddy — on-device wake-word detector interface.
//
// CoreS3-side implementations wrap ESP-SR (Espressif's first-party
// engine, bundled with IDF) or Porcupine. Both run on a dedicated FreeRTOS
// task that consumes the same PCM stream as the BLE-relay mic capture,
// so the wake detector and mic driver coexist without a separate audio
// pipeline.

#pragma once

#include <cstdint>
#include <string_view>

namespace pre_buddy::hal {

// Called whenever the detector fires. ``phrase`` is the matched
// keyword; ``confidence`` is the engine's reported score (0..1).
using WakeWordFn = void (*)(std::string_view phrase, float confidence,
                            void* user_data);

class IWakeWordDetector {
   public:
    virtual ~IWakeWordDetector() = default;

    // Start listening for ``phrase``. The detector is expected to run
    // continuously until ``stop()`` is called. Some engines (ESP-SR)
    // only support a fixed catalogue of phrases compiled in; if the
    // requested phrase is unsupported, the implementation MUST fall
    // back to a built-in default and surface that via the user-visible
    // status (e.g. the tray menu reflects the active phrase, not the
    // requested one).
    virtual void start(std::string_view phrase,
                       WakeWordFn callback,
                       void* user_data) noexcept = 0;

    virtual void stop() noexcept = 0;
    virtual bool is_running() const noexcept = 0;

    // The phrase actually being detected (may differ from ``start``'s
    // argument if the engine doesn't support it). Empty when stopped.
    virtual std::string_view active_phrase() const noexcept = 0;
};

}  // namespace pre_buddy::hal
