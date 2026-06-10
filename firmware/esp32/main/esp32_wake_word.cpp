// SPDX-License-Identifier: TBD
// ESP32-S3 IWakeWordDetector — Espressif esp-sr wakenet (K151-R / CoreS3).
//
// Loads the wakenet model from the `model` flash partition, builds a
// single-mic AFE (no AEC/VAD — wakenet only), and runs a fetch task that
// fires the callback on detection. The mic driver feeds PCM into feed() (the
// detector and mic share one audio stream — no separate pipeline). Bundled
// model is "Hi, ESP" (wn9_hiesp); custom phrases need Espressif's training
// service, so we surface the real active phrase rather than the requested one.

#include "esp32_wake_word.h"

#include <cstring>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_afe_config.h"
#include "esp_afe_sr_models.h"  // ESP_AFE_SR_HANDLE
#include "esp_wn_models.h"      // ESP_WN_PREFIX
#include "model_path.h"         // esp_srmodel_init / _filter

namespace pre_buddy::esp32 {
namespace {

constexpr const char* TAG = "pre-buddy-wake";
constexpr int kMaxChunk = 1024;

const esp_afe_sr_iface_t* s_afe = nullptr;
esp_afe_sr_data_t* s_afe_data = nullptr;
srmodel_list_t* s_models = nullptr;
TaskHandle_t s_task = nullptr;
volatile bool s_running = false;
int s_chunk = 0;
int s_feed_have = 0;
int16_t s_feed_buf[kMaxChunk];
hal::WakeWordFn s_cb = nullptr;
void* s_user = nullptr;
std::string s_phrase;

void fetch_task(void*) {
    while (s_running) {
        afe_fetch_result_t* res = s_afe->fetch(s_afe_data);
        if (res == nullptr || res->ret_value == -1) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (res->wakeup_state == WAKENET_DETECTED) {
            ESP_LOGI(TAG, "WAKE WORD detected (index %d)", res->wake_word_index);
            if (s_cb) s_cb(s_phrase, 1.0f, s_user);
        }
    }
    s_task = nullptr;
    vTaskDelete(nullptr);
}

}  // namespace

void Esp32WakeWordDetector::start(std::string_view phrase,
                                  hal::WakeWordFn callback,
                                  void* user_data) noexcept {
    if (running_) return;
    (void)phrase;  // esp-sr uses the compiled-in model; report the real phrase.
    s_cb = callback;
    s_user = user_data;

    s_models = esp_srmodel_init("model");
    if (s_models == nullptr) {
        ESP_LOGE(TAG, "esp_srmodel_init('model') failed — model partition?");
        return;
    }
    char* wn = esp_srmodel_filter(s_models, ESP_WN_PREFIX, nullptr);
    if (wn == nullptr) {
        ESP_LOGE(TAG, "no wakenet model in partition");
        return;
    }
    ESP_LOGI(TAG, "wakenet model: %s", wn);

    afe_config_t cfg = AFE_CONFIG_DEFAULT();
    cfg.aec_init = false;   // single mic, no echo cancellation
    cfg.se_init = false;    // no speech enhancement
    cfg.vad_init = false;   // wakenet only
    cfg.wakenet_init = true;
    cfg.wakenet_model_name = wn;
    cfg.wakenet_model_name_2 = nullptr;
    cfg.wakenet_mode = DET_MODE_90;  // single channel
    cfg.afe_mode = SR_MODE_LOW_COST;
    cfg.memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
    cfg.pcm_config.total_ch_num = 1;
    cfg.pcm_config.mic_num = 1;
    cfg.pcm_config.ref_num = 0;
    cfg.pcm_config.sample_rate = 16000;

    s_afe = &ESP_AFE_SR_HANDLE;
    s_afe_data = s_afe->create_from_config(&cfg);
    if (s_afe_data == nullptr) {
        ESP_LOGE(TAG, "AFE create_from_config failed");
        return;
    }
    s_chunk = s_afe->get_feed_chunksize(s_afe_data);
    if (s_chunk <= 0 || s_chunk > kMaxChunk) {
        ESP_LOGE(TAG, "unexpected AFE feed chunk %d", s_chunk);
        s_afe->destroy(s_afe_data);
        s_afe_data = nullptr;
        return;
    }
    s_feed_have = 0;
    s_phrase = "hi esp";
    active_phrase_ = s_phrase;
    callback_ = callback;
    user_data_ = user_data;
    running_ = true;
    s_running = true;
    xTaskCreate(fetch_task, "wake", 4096, nullptr, 5, &s_task);
    ESP_LOGI(TAG, "wake-word started; feed chunk = %d samples", s_chunk);
}

void Esp32WakeWordDetector::feed(const std::int16_t* samples,
                                 std::size_t num_samples) noexcept {
    if (!s_running || s_afe_data == nullptr || s_chunk <= 0) return;
    std::size_t off = 0;
    while (off < num_samples) {
        const int need = s_chunk - s_feed_have;
        const std::size_t take =
            (num_samples - off) < static_cast<std::size_t>(need) ? (num_samples - off)
                                                                  : static_cast<std::size_t>(need);
        std::memcpy(s_feed_buf + s_feed_have, samples + off, take * sizeof(std::int16_t));
        s_feed_have += static_cast<int>(take);
        off += take;
        if (s_feed_have >= s_chunk) {
            s_afe->feed(s_afe_data, s_feed_buf);
            s_feed_have = 0;
        }
    }
}

std::size_t Esp32WakeWordDetector::feed_chunksize() const noexcept {
    return s_chunk > 0 ? static_cast<std::size_t>(s_chunk) : 512;
}

void Esp32WakeWordDetector::stop() noexcept {
    if (!running_) return;
    s_running = false;
    running_ = false;
    for (int i = 0; i < 50 && s_task != nullptr; ++i) vTaskDelay(pdMS_TO_TICKS(10));
    if (s_afe != nullptr && s_afe_data != nullptr) {
        s_afe->destroy(s_afe_data);
        s_afe_data = nullptr;
    }
    active_phrase_ = {};
}

bool Esp32WakeWordDetector::is_running() const noexcept { return running_; }

std::string_view Esp32WakeWordDetector::active_phrase() const noexcept {
    return active_phrase_;
}

}  // namespace pre_buddy::esp32
