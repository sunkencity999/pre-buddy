// SPDX-License-Identifier: TBD
// ESP32-S3 wake-word detector — stub.

#include "esp32_wake_word.h"

namespace pre_buddy::esp32 {

void Esp32WakeWordDetector::start(std::string_view phrase,
                                  hal::WakeWordFn callback,
                                  void* user_data) noexcept {
    // ESP-SR only supports phrases that were compiled into the model
    // bundle. The Stack-Chan kit ships with "hey buddy"; anything else
    // falls back to it.
    active_phrase_ = (phrase == "hey buddy") ? std::string(phrase) : "hey buddy";
    callback_ = callback;
    user_data_ = user_data;
    running_ = true;
    // TODO when CoreS3 arrives:
    //   - esp_srmodel_init() to load the bundled multinet/wakenet models.
    //   - Spawn a task that:
    //       audio_feed = audio_feed_pipeline_create(...);
    //       afe_handle->feed(...) the same PCM stream the mic driver
    //       captures (use a ring buffer split between the two consumers).
    //       afe_handle->fetch() in a loop; on wakenet_state ==
    //       WAKENET_DETECTED, call callback_(active_phrase_, score,
    //       user_data_).
}

void Esp32WakeWordDetector::stop() noexcept {
    running_ = false;
    active_phrase_.clear();
    // TODO: signal the detect task to exit; afe_handle->destroy();
    //       esp_srmodel_deinit().
}

bool Esp32WakeWordDetector::is_running() const noexcept { return running_; }

std::string_view Esp32WakeWordDetector::active_phrase() const noexcept {
    return active_phrase_;
}

}  // namespace pre_buddy::esp32
