// SPDX-License-Identifier: TBD
// ESP32-S3 ILedDriver — M5Stack CoreS3 stack-chan (K151-R).
//
// The 12 WS2812 LEDs are NOT driven directly by the ESP32: they hang off a
// PY32 IO-expander (I2C addr 0x6F) that also gates the servo power rail
// (VM_EN, expander pin 0). We reach the expander over the CoreS3 internal
// I2C bus that M5GFX already initialised, using lgfx's shared-bus i2c
// helpers (creating a second bus on the same pins would conflict). Register
// protocol mirrors M5's StackChan-BSP PY32IOExpander driver.

#include "esp32_led.h"

#include <M5GFX.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pre_buddy/led_palette.h"

namespace pre_buddy::esp32 {
namespace {

constexpr const char* TAG = "pre-buddy-led";
constexpr int kExpanderAddr = 0x6F;
constexpr uint32_t kI2cFreq = 100000;  // PY32 expander runs at 100 kHz (per BSP)

// PY32 IO-expander registers (StackChan-BSP PY32IOExpander.cpp).
constexpr uint8_t REG_VERSION    = 0x02;
constexpr uint8_t REG_GPIO_M_L   = 0x03;  // direction, 1 = output
constexpr uint8_t REG_GPIO_M_H   = 0x04;
constexpr uint8_t REG_GPIO_O_L   = 0x05;  // output level
constexpr uint8_t REG_GPIO_PU_L  = 0x09;  // pull-up
constexpr uint8_t REG_GPIO_PU_H  = 0x0A;
constexpr uint8_t REG_GPIO_PD_L  = 0x0B;  // pull-down
constexpr uint8_t REG_GPIO_PD_H  = 0x0C;
constexpr uint8_t REG_GPIO_DRV_H = 0x14;  // drive, 0 = push-pull
constexpr uint8_t REG_LED_CFG       = 0x24;  // [5:0] = count, bit6 = refresh
constexpr uint8_t REG_LED_RAM_START = 0x30;  // 2 bytes/LED, RGB565

constexpr uint8_t kNumLeds = 12;
constexpr uint8_t PIN_VM_EN = 0;   // servo power enable
constexpr uint8_t PIN_RGB   = 13;  // WS2812 data line (driven by the expander)

int s_port = -1;  // i2c port hosting the expander (auto-detected in init)

int rd8(uint8_t reg) {
    auto r = lgfx::i2c::readRegister8(s_port, kExpanderAddr, reg, kI2cFreq);
    return r.has_value() ? r.value() : -1;
}
bool wr8(uint8_t reg, uint8_t val) {
    return lgfx::i2c::writeRegister8(s_port, kExpanderAddr, reg, val, 0, kI2cFreq).has_value();
}
// Read-modify-write a single bit of an 8-bit register.
void set_bit(uint8_t reg, uint8_t bit, bool v) {
    int cur = rd8(reg);
    if (cur < 0) return;
    const uint8_t nv = v ? static_cast<uint8_t>(cur | (1 << bit))
                         : static_cast<uint8_t>(cur & ~(1 << bit));
    wr8(reg, nv);
}
void led_color(uint8_t index, Rgb888 c) {
    const uint16_t v = ((c.r & 0xF8) << 8) | ((c.g & 0xFC) << 3) | (c.b >> 3);  // RGB565
    const uint8_t buf[3] = {static_cast<uint8_t>(REG_LED_RAM_START + index * 2),
                            static_cast<uint8_t>(v & 0xFF),
                            static_cast<uint8_t>((v >> 8) & 0xFF)};
    lgfx::i2c::transactionWrite(s_port, kExpanderAddr, buf, sizeof(buf), kI2cFreq);
}
void led_refresh() {
    int cfg = rd8(REG_LED_CFG);
    if (cfg < 0) cfg = kNumLeds;
    wr8(REG_LED_CFG, static_cast<uint8_t>(cfg | (1 << 6)));
}
// AW9523 GPIO expander (0x58) — gates the CoreS3 5V boost + bus that power
// the body (LED strips + servos). Read-modify-write a single bit so we don't
// disturb the LCD-reset bit M5GFX drives on the same chip.
constexpr int kAw9523Addr = 0x58;
void aw_set_bit(uint8_t reg, uint8_t bit, bool v) {
    auto r = lgfx::i2c::readRegister8(s_port, kAw9523Addr, reg, kI2cFreq);
    if (!r.has_value()) return;
    const uint8_t cur = r.value();
    const uint8_t nv = v ? static_cast<uint8_t>(cur | (1 << bit))
                         : static_cast<uint8_t>(cur & ~(1 << bit));
    lgfx::i2c::writeRegister8(s_port, kAw9523Addr, reg, nv, 0, kI2cFreq);
}
// Enable the CoreS3 5V boost (SY7088) + bus output so the body gets power.
// M5GFX (display-only) skips this; M5Unified's M5.begin() does it.
void enable_body_power() {
    aw_set_bit(0x05, 7, false);  // P1_7 -> output (AW9523 config: 0 = output)
    aw_set_bit(0x04, 1, false);  // P0_1 -> output
    aw_set_bit(0x03, 7, true);   // P1_7 high = SY7088 BOOST_EN (5V boost on)
    aw_set_bit(0x02, 1, true);   // P0_1 high = BUS_EN (5V to the body)
    ESP_LOGI(TAG, "CoreS3 5V boost + bus enabled (AW9523 0x58)");
}

// The expander sits on the CoreS3 internal I2C bus M5GFX configured. It boots
// slowly (the BSP polls up to ~1.2s), so retry both candidate ports until one
// answers with a valid version before giving up.
bool detect_expander() {
    for (int attempt = 0; attempt < 12; ++attempt) {  // ~1.8s budget
        for (int port : {1, 0}) {
            s_port = port;
            const int v = rd8(REG_VERSION);
            if (v > 0 && v != 0xFF) {
                ESP_LOGI(TAG, "PY32 expander on i2c port %d (version %d, attempt %d)",
                         port, v, attempt);
                return true;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(150));
    }
    s_port = -1;
    return false;
}

}  // namespace

void Esp32LedDriver::init() noexcept {
    if (!detect_expander()) {
        ESP_LOGE(TAG, "PY32 IO expander not found — LEDs + servo power unavailable");
        return;
    }
    // Power the body first: enable the CoreS3 5V boost + bus (the WS2812 strips
    // and servos run on 5V), then let the rail settle before driving LEDs.
    enable_body_power();
    vTaskDelay(pdMS_TO_TICKS(100));
    // VM_EN (pin 0): output, pull-up, high → enable the servo power rail.
    set_bit(REG_GPIO_M_L, PIN_VM_EN, true);
    set_bit(REG_GPIO_PD_L, PIN_VM_EN, false);
    set_bit(REG_GPIO_PU_L, PIN_VM_EN, true);
    set_bit(REG_GPIO_O_L, PIN_VM_EN, true);
    // RGB (pin 13): output, pull-up, push-pull; then set the LED count.
    set_bit(REG_GPIO_M_H, PIN_RGB - 8, true);
    set_bit(REG_GPIO_PD_H, PIN_RGB - 8, false);
    set_bit(REG_GPIO_PU_H, PIN_RGB - 8, true);
    set_bit(REG_GPIO_DRV_H, PIN_RGB - 8, false);
    wr8(REG_LED_CFG, kNumLeds);
    vTaskDelay(pdMS_TO_TICKS(200));  // let the expander's WS2812 engine come up (matches BSP)

    initialised_ = true;
    // BSP primes with two black frames before the first real colour; mirror
    // that, then a full-white self-test flash (brightness-independent) to
    // confirm the data path on the bench.
    off();
    vTaskDelay(pdMS_TO_TICKS(50));
    off();
    vTaskDelay(pdMS_TO_TICKS(50));
    for (uint8_t i = 0; i < kNumLeds; ++i) led_color(i, Rgb888{255, 255, 255});
    led_refresh();
    vTaskDelay(pdMS_TO_TICKS(800));
    ESP_LOGI(TAG, "LED + servo power initialised (self-test flash done)");
}

void Esp32LedDriver::set_color(LedColor color) noexcept {
    last_color_ = color;
    if (!initialised_) return;
    const Rgb888 rgb = apply_brightness(to_rgb888(color), brightness_);
    for (uint8_t i = 0; i < kNumLeds; ++i) led_color(i, rgb);
    led_refresh();
}

void Esp32LedDriver::set_brightness(unsigned char level) noexcept {
    brightness_ = level;
    set_color(last_color_);
}

void Esp32LedDriver::off() noexcept {
    if (!initialised_) return;
    for (uint8_t i = 0; i < kNumLeds; ++i) led_color(i, Rgb888{0, 0, 0});
    led_refresh();
}

}  // namespace pre_buddy::esp32
