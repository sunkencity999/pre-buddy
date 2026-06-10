// SPDX-License-Identifier: TBD
// ESP32-S3 ISpeakerDriver — M5Stack CoreS3 (AW88298 amp + I2S).
//
// The built-in speaker is an AW88298 class-D amp (I2C-configured at 0x36) fed
// by I2S. On the CoreS3 the amp + mic share I2S_NUM_1 (BCK=34, WS=33,
// MCLK=0); the speaker's data-out is GPIO13. Amp power is gated by AW9523
// (0x58) port0 bit2. We reach the I2C devices over the shared internal bus
// via lgfx's helpers (M5GFX owns it). Init/registers mirror M5Unified.

#include "esp32_speaker.h"

#include <M5GFX.h>

#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

namespace pre_buddy::esp32 {
namespace {

constexpr const char* TAG = "pre-buddy-spk";
constexpr int kI2cPort = 1;          // CoreS3 internal bus (LED/AXP/AW88298 live here)
constexpr int kAw9523Addr = 0x58;
constexpr int kAw88298Addr = 0x36;
constexpr uint32_t kI2cFreq = 400000;

i2s_chan_handle_t s_tx = nullptr;

void aw9523_bit(uint8_t reg, uint8_t bit, bool on) {
    auto r = lgfx::i2c::readRegister8(kI2cPort, kAw9523Addr, reg, kI2cFreq);
    if (!r.has_value()) return;
    const uint8_t cur = r.value();
    const uint8_t nv = on ? static_cast<uint8_t>(cur | (1 << bit))
                          : static_cast<uint8_t>(cur & ~(1 << bit));
    lgfx::i2c::writeRegister8(kI2cPort, kAw9523Addr, reg, nv, 0, kI2cFreq);
}
// AW88298 registers are 16-bit, big-endian on the wire.
void aw88298_reg(uint8_t reg, uint16_t val) {
    const uint8_t buf[3] = {reg, static_cast<uint8_t>(val >> 8), static_cast<uint8_t>(val & 0xFF)};
    lgfx::i2c::transactionWrite(kI2cPort, kAw88298Addr, buf, sizeof(buf), kI2cFreq);
}
void amp_enable(uint32_t sample_rate) {
    aw9523_bit(0x02, 2, true);  // amp power on
    // reg 0x06 picks the AW88298 sample-rate slot (mirrors M5Unified).
    static const uint8_t rate_tbl[] = {4, 5, 6, 8, 10, 11, 15, 20, 22, 44};
    const uint32_t rate = (sample_rate + 1102) / 2205;
    size_t i = 0;
    while (i < sizeof(rate_tbl) && rate > rate_tbl[i]) ++i;
    if (i >= sizeof(rate_tbl)) i = sizeof(rate_tbl) - 1;
    const uint16_t reg06 = static_cast<uint16_t>(i) | 0x14C0;  // I2S BCK 16*2
    aw88298_reg(0x61, 0x0673);  // boost mode disabled
    aw88298_reg(0x04, 0x4040);  // I2SEN=1 AMPPD=0 PWDN=0
    aw88298_reg(0x05, 0x0008);
    aw88298_reg(0x06, reg06);
    aw88298_reg(0x0C, 0x0064);  // full volume
}
void amp_disable() {
    aw88298_reg(0x04, 0x4000);  // I2SEN=0
    aw9523_bit(0x02, 2, false);
}

}  // namespace

void Esp32SpeakerDriver::start_playback(std::uint32_t sample_rate_hz) noexcept {
    if (playing_) return;
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    if (i2s_new_channel(&chan_cfg, &s_tx, nullptr) != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel failed");
        return;
    }
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate_hz),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = GPIO_NUM_0,
            .bclk = GPIO_NUM_34,
            .ws = GPIO_NUM_33,
            .dout = GPIO_NUM_13,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {},
        },
    };
    if (i2s_channel_init_std_mode(s_tx, &std_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_init_std_mode failed");
        i2s_del_channel(s_tx);
        s_tx = nullptr;
        return;
    }
    i2s_channel_enable(s_tx);
    amp_enable(sample_rate_hz);
    playing_ = true;
    ESP_LOGI(TAG, "speaker started @ %u Hz", static_cast<unsigned>(sample_rate_hz));
}

std::size_t Esp32SpeakerDriver::play_frame(const std::int16_t* samples,
                                           std::size_t num_samples) noexcept {
    if (!playing_ || s_tx == nullptr) return 0;
    // The amp runs 16-bit stereo; duplicate the mono source into L/R.
    constexpr std::size_t kChunk = 128;
    std::int16_t stereo[kChunk * 2];
    std::size_t done = 0;
    while (done < num_samples) {
        const std::size_t m = (num_samples - done) < kChunk ? (num_samples - done) : kChunk;
        for (std::size_t i = 0; i < m; ++i) {
            const std::int16_t s = samples[done + i];
            stereo[2 * i] = s;
            stereo[2 * i + 1] = s;
        }
        std::size_t written = 0;
        if (i2s_channel_write(s_tx, stereo, m * 2 * sizeof(std::int16_t), &written,
                              pdMS_TO_TICKS(500)) != ESP_OK) {
            break;
        }
        done += m;
    }
    return done;
}

void Esp32SpeakerDriver::stop_playback() noexcept {
    if (!playing_) return;
    amp_disable();
    i2s_channel_disable(s_tx);
    i2s_del_channel(s_tx);
    s_tx = nullptr;
    playing_ = false;
}

bool Esp32SpeakerDriver::is_playing() const noexcept { return playing_; }

}  // namespace pre_buddy::esp32
