// SPDX-License-Identifier: TBD
// ESP32-S3 IServoDriver — M5Stack stack-chan K151-R.
//
// The head is driven by two Feetech SCS serial-bus servos (SCSCL protocol)
// on UART1 @ 1 Mbps (TX=GPIO6, RX=GPIO7), NOT PWM. Servo ID 1 = yaw/pan,
// ID 2 = pitch/tilt. Power comes from the 5V boost + VM_EN that the LED
// driver's expander init raises, so led.init() must run before servo.init().
//
// We only send WRITE packets (position / torque) — fire-and-forget, no
// response parsing needed for motion. Packet + register layout mirror M5's
// StackChan-BSP FTServo_Arduino (SCSCL/SCS).

#include "esp32_servo.h"

#include <cstdint>

#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace pre_buddy::esp32 {
namespace {

constexpr const char* TAG = "pre-buddy-servo";
constexpr uart_port_t kUart = UART_NUM_1;
constexpr int kTxPin = 6;
constexpr int kRxPin = 7;
constexpr int kBaud = 1000000;

// SCSCL register addresses + instruction.
constexpr uint8_t REG_TORQUE_ENABLE = 40;
constexpr uint8_t REG_GOAL_POSITION = 42;
constexpr uint8_t INST_WRITE = 0x03;

// Servo IDs + per-unit zero positions (M5 BSP defaults; per-unit — may need
// a calibration pass for the head to centre exactly).
constexpr uint8_t ID_YAW = 1;
constexpr uint8_t ID_PITCH = 2;
constexpr int ZERO_YAW = 460;
constexpr int ZERO_PITCH = 620;
// Body-safe raw-position window per axis (BSP angle limits → pos), inside the
// servo's electrical [0,1000]. Bounds travel so a bad zero can't drive past
// the safe range.
constexpr int YAW_POS_MIN = 50,   YAW_POS_MAX = 870;
constexpr int PITCH_POS_MIN = 620, PITCH_POS_MAX = 908;
constexpr float STEPS_PER_DEG = 3.2f;  // 0.3125°/step
constexpr uint16_t kMoveSpeed = 400;   // steps/s — gentle, controlled motion

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Send one SCS WRITE packet: FF FF ID Len(=len+3) INST_WRITE Addr [data] ~chk.
void scs_write(uint8_t id, uint8_t addr, const uint8_t* data, uint8_t len) {
    uint8_t buf[16];
    const uint8_t msglen = static_cast<uint8_t>(len + 3);
    uint8_t n = 0;
    buf[n++] = 0xFF;
    buf[n++] = 0xFF;
    buf[n++] = id;
    buf[n++] = msglen;
    buf[n++] = INST_WRITE;
    buf[n++] = addr;
    uint8_t cks = id + msglen + INST_WRITE + addr;
    for (uint8_t i = 0; i < len; ++i) {
        buf[n++] = data[i];
        cks += data[i];
    }
    buf[n++] = static_cast<uint8_t>(~cks);
    uart_write_bytes(kUart, reinterpret_cast<const char*>(buf), n);
}

// SCSCL is big-endian (End=1): u16 → {high, low}.
void write_pos(uint8_t id, uint16_t pos, uint16_t time, uint16_t speed) {
    const uint8_t d[6] = {
        static_cast<uint8_t>(pos >> 8),   static_cast<uint8_t>(pos & 0xFF),
        static_cast<uint8_t>(time >> 8),  static_cast<uint8_t>(time & 0xFF),
        static_cast<uint8_t>(speed >> 8), static_cast<uint8_t>(speed & 0xFF),
    };
    scs_write(id, REG_GOAL_POSITION, d, sizeof(d));
}

void enable_torque(uint8_t id, bool en) {
    const uint8_t v = en ? 1 : 0;
    scs_write(id, REG_TORQUE_ENABLE, &v, 1);
}

void drive(float head_x_deg, float head_y_deg) {
    const int yaw = clampi(static_cast<int>(ZERO_YAW + head_x_deg * STEPS_PER_DEG),
                           YAW_POS_MIN, YAW_POS_MAX);
    const int pitch = clampi(static_cast<int>(ZERO_PITCH + head_y_deg * STEPS_PER_DEG),
                             PITCH_POS_MIN, PITCH_POS_MAX);
    // Time=0 → speed-governed move (kMoveSpeed) for smooth, bounded motion.
    write_pos(ID_YAW, static_cast<uint16_t>(yaw), 0, kMoveSpeed);
    write_pos(ID_PITCH, static_cast<uint16_t>(pitch), 0, kMoveSpeed);
}

}  // namespace

void Esp32ServoDriver::init() noexcept {
    uart_config_t cfg = {};
    cfg.baud_rate = kBaud;
    cfg.data_bits = UART_DATA_8_BITS;
    cfg.parity = UART_PARITY_DISABLE;
    cfg.stop_bits = UART_STOP_BITS_1;
    cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    cfg.source_clk = UART_SCLK_DEFAULT;
    if (uart_driver_install(kUart, 256, 256, 0, nullptr, 0) != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed");
        return;
    }
    uart_param_config(kUart, &cfg);
    uart_set_pin(kUart, kTxPin, kRxPin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    initialised_ = true;

    // Servos are powered via VM_EN / 5V boost raised by led.init() — which
    // runs before this. Enable torque, then a gentle bring-up sweep so we can
    // confirm motion on the bench (small angles, low speed).
    enable_torque(ID_YAW, true);
    enable_torque(ID_PITCH, true);
    ESP_LOGI(TAG, "SCSCL servos init (UART1 1M, GPIO6/7); torque enabled");

    drive(0.0f, 45.0f);   vTaskDelay(pdMS_TO_TICKS(800));  // centre
    drive(-15.0f, 45.0f); vTaskDelay(pdMS_TO_TICKS(800));  // look left
    drive(15.0f, 45.0f);  vTaskDelay(pdMS_TO_TICKS(800));  // look right
    drive(0.0f, 55.0f);   vTaskDelay(pdMS_TO_TICKS(800));  // look up a touch
    drive(0.0f, 45.0f);                                    // back to centre
    ESP_LOGI(TAG, "servo bring-up sweep done");
}

void Esp32ServoDriver::move(const MotionCommand& cmd) noexcept {
    if (!initialised_) return;
    // cmd is already MotionEngine-clamped (Y in [10,80], X rate-limited).
    // TODO: honour cmd.duration_ms (currently a fixed gentle speed).
    drive(cmd.head_x_deg, cmd.head_y_deg);
}

void Esp32ServoDriver::rest() noexcept {
    if (!initialised_) return;
    drive(0.0f, 45.0f);  // resting pose per IServoDriver contract
}

void Esp32ServoDriver::disable() noexcept {
    if (!initialised_) return;
    enable_torque(ID_YAW, false);
    enable_torque(ID_PITCH, false);
}

}  // namespace pre_buddy::esp32
