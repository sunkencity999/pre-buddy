// SPDX-License-Identifier: TBD
// ESP32-S3 IMicDriver — M5Stack CoreS3 built-in mic over I2S.
//
// Despite the legacy "PDM" wording, the CoreS3 mic is standard I2S input,
// sharing I2S_NUM_1 with the speaker (BCK=34, WS=33, MCLK=0; mic DIN=14).
// A capture task reads stereo frames, downmixes to mono PCM16, and hands
// each frame to the callback. It also logs a peak level periodically so the
// mic can be verified on the bench (clap/talk → the number jumps).
//
// Note: I2S_NUM_1 is shared with the speaker; only one of capture/playback
// can hold the port at a time until a full-duplex refactor (Stage 2.5+).

#include "esp32_mic.h"

#include <M5GFX.h>  // lgfx::i2c — shared internal bus to the ES7210 ADC

#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace pre_buddy::esp32 {
namespace {

constexpr const char* TAG = "pre-buddy-mic";
constexpr std::size_t kMaxFrame = 512;

// The CoreS3 mic feeds an ES7210 audio ADC (I2C 0x40) that must be configured
// before it streams I2S data — otherwise the bus reads all zeros. Register
// sequence mirrors M5Unified's _microphone_enabled_cb_cores3.
constexpr int kI2cPort = 1;
constexpr int kEs7210Addr = 0x40;
void es7210_write(uint8_t reg, uint8_t val) {
    lgfx::i2c::writeRegister8(kI2cPort, kEs7210Addr, reg, val, 0, 400000);
}
void es7210_init() {
    es7210_write(0x00, 0xFF);  // RESET_CTL
    static const uint8_t seq[][2] = {
        {0x00, 0x41}, {0x01, 0x1f}, {0x06, 0x00}, {0x07, 0x20}, {0x08, 0x10},
        {0x09, 0x30}, {0x0A, 0x30}, {0x20, 0x0a}, {0x21, 0x2a}, {0x22, 0x0a},
        {0x23, 0x2a}, {0x02, 0xC1}, {0x04, 0x01}, {0x05, 0x00}, {0x11, 0x60},
        {0x40, 0x42}, {0x41, 0x70}, {0x42, 0x70}, {0x43, 0x1B}, {0x44, 0x1B},
        {0x45, 0x00}, {0x46, 0x00}, {0x47, 0x00}, {0x48, 0x00}, {0x49, 0x00},
        {0x4A, 0x00}, {0x4B, 0x00}, {0x4C, 0xFF}, {0x01, 0x14},
    };
    for (const auto& d : seq) es7210_write(d[0], d[1]);
}

i2s_chan_handle_t s_rx = nullptr;
TaskHandle_t s_task = nullptr;
volatile bool s_running = false;
hal::MicFrameFn s_cb = nullptr;
void* s_user = nullptr;
std::size_t s_frame = 320;

std::int16_t s_stereo[kMaxFrame * 2];
std::int16_t s_mono[kMaxFrame];

void capture_task(void*) {
    std::uint32_t frames = 0;
    std::int16_t peak = 0;
    while (s_running) {
        std::size_t got = 0;
        const esp_err_t e = i2s_channel_read(s_rx, s_stereo,
                                              s_frame * 2 * sizeof(std::int16_t),
                                              &got, pdMS_TO_TICKS(200));
        if (e != ESP_OK || got == 0) continue;
        const std::size_t stereo_samples = got / sizeof(std::int16_t);
        const std::size_t n = stereo_samples / 2;  // mono frames
        for (std::size_t i = 0; i < n; ++i) {
            const std::int16_t l = s_stereo[2 * i];
            s_mono[i] = l;
            const std::int16_t a = l < 0 ? static_cast<std::int16_t>(-l) : l;
            if (a > peak) peak = a;
        }
        if (s_cb) s_cb(s_mono, n, s_user);
        if (++frames % 50 == 0) {  // ~1 s at 20 ms frames
            ESP_LOGI(TAG, "mic peak (last ~1s) = %d", peak);
            peak = 0;
        }
    }
    s_task = nullptr;
    vTaskDelete(nullptr);
}

}  // namespace

void Esp32MicDriver::start_capture(std::uint32_t sample_rate_hz,
                                   std::size_t frame_size_samples,
                                   hal::MicFrameFn callback,
                                   void* user_data) noexcept {
    if (capturing_) return;
    callback_ = callback;
    user_data_ = user_data;
    s_cb = callback;
    s_user = user_data;
    s_frame = (frame_size_samples == 0 || frame_size_samples > kMaxFrame)
                  ? 320
                  : frame_size_samples;
    const std::uint32_t sr = sample_rate_hz ? sample_rate_hz : 16000;

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    if (i2s_new_channel(&chan_cfg, nullptr, &s_rx) != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel (rx) failed — is I2S_NUM_1 free?");
        return;
    }
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sr),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = GPIO_NUM_0,
            .bclk = GPIO_NUM_34,
            .ws = GPIO_NUM_33,
            .dout = I2S_GPIO_UNUSED,
            .din = GPIO_NUM_14,
            .invert_flags = {},
        },
    };
    if (i2s_channel_init_std_mode(s_rx, &std_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_init_std_mode (rx) failed");
        i2s_del_channel(s_rx);
        s_rx = nullptr;
        return;
    }
    i2s_channel_enable(s_rx);  // starts MCLK/BCK so the ES7210 has a clock
    es7210_init();             // configure the ADC so it actually streams audio
    capturing_ = true;
    s_running = true;
    xTaskCreate(capture_task, "mic", 4096, nullptr, 5, &s_task);
    ESP_LOGI(TAG, "mic capture started @ %u Hz, frame %u",
             static_cast<unsigned>(sr), static_cast<unsigned>(s_frame));
}

void Esp32MicDriver::stop_capture() noexcept {
    if (!capturing_) return;
    s_running = false;
    for (int i = 0; i < 30 && s_task != nullptr; ++i) vTaskDelay(pdMS_TO_TICKS(10));
    if (s_rx != nullptr) {
        i2s_channel_disable(s_rx);
        i2s_del_channel(s_rx);
        s_rx = nullptr;
    }
    capturing_ = false;
}

bool Esp32MicDriver::is_capturing() const noexcept { return capturing_; }

}  // namespace pre_buddy::esp32
