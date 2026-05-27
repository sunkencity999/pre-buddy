# Hardware bring-up

> **Status:** Late pre-hardware. Every host-buildable half is done and
> tested; this doc is the playbook for wiring real peripherals on the
> M5Stack CoreS3 once a board is in hand. We work through it together
> the first time, then `tools/provision_device.sh` packages the result
> for assembly-line builds of subsequent units.

See [README §Hardware bring-up checklist](../README.md#hardware-bring-up-checklist)
for the high-level table; this doc is the long-form, with the actual
ESP-IDF API calls + verification steps for each stage.

## Hardware

- [M5Stack StackChan Remote Kit (K151-R)](https://shop.m5stack.com/products/m5stackchan-ai-desktop-robot-kit-with-remote-control-esp32-s3)
  — CoreS3 (ESP32-S3) + animatronic body (servos × 2, SK6812 LED ring,
  ILI9342 2" IPS panel, AW88298 amp + speaker, PDM mic).
- Or a bare M5Stack CoreS3 if you want to bring up firmware before
  assembling the chassis.

## Tooling on the dev machine

| Tool | Why | Install |
|---|---|---|
| ESP-IDF v5.x | The Espressif build system, toolchain, NimBLE, ESP-SR | `tools/install_idf.sh` (wraps the official Espressif installer) |
| `idf.py` | Build/flash/monitor CLI; comes with IDF | n/a — surfaced via `. $IDF_PATH/export.sh` |
| `bleak` (Python) | Host-side BLE central for end-to-end testing | `pip install 'pre-buddy[transport]'` |
| nRF Connect (iOS/Android) | Sanity-check advertising + GATT services | App Store / Play Store |
| USB-serial driver | macOS 12+ ships CDC support; CoreS3 typically Just Works | macOS Big Sur+ has built-in CDC support |

After running `tools/install_idf.sh`, future shells need this in
`~/.zshrc` (one-time):

```bash
alias get_idf='. $HOME/esp/esp-idf/export.sh'
```

Then in any shell where you want to build firmware: `get_idf`.

---

## Stage 0 — Physical assembly

A few hours, no code. Order:

1. Assemble the K151-R kit per M5Stack's [instructions](https://docs.m5stack.com/en/atom/m5stackchan).
2. Mount the CoreS3 to the body; route the servo cables to the correct GPIOs (kit docs cover this).
3. Plug the CoreS3 into your laptop via USB-C.

Quick verification:

```bash
ls /dev/cu.usbmodem*    # should show one device (the CoreS3)
```

If nothing shows up, your USB-C cable is probably power-only (very
common). Swap for a USB 2.0 data cable.

---

## Stage 1 — Bring-up smoke test (BLE only)

**Goal:** prove the build + flash + advertise path works, before any
peripheral wiring.

```bash
cd firmware/esp32
. ~/esp/esp-idf/export.sh
idf.py set-target esp32s3
idf.py menuconfig
#   - Component config → Bluetooth → Bluetooth controller → enabled
#   - Component config → Bluetooth → Bluedroid Options → disabled
#   - Component config → Bluetooth → NimBLE Options → enabled
#   - Component config → ESP Speech Recognition → bundled wake-net model
idf.py build flash monitor
```

### What lands in this stage

Fill in `firmware/esp32/main/esp32_ble.cpp`:

```cpp
// Pseudocode — actual NimBLE API names will differ slightly
nimble_port_init();
nimble_port_freertos_init(ble_host_task);

// Register the NUS service with the UUIDs from hal/uuids.h:
ble_gatts_count_cfg(...);
ble_gatts_add_svcs(...);

// In the GATT access callback for RX, push received bytes into framer_:
framer().feed(rx_buf, rx_len);

// To send a TX notification, look up the TX attr handle and:
ble_gatts_notify_custom(conn_handle, tx_handle, om);
```

### Success criterion

- [ ] `idf.py monitor` shows `app_main()` running with no panics.
- [ ] nRF Connect on a phone scans and sees the device advertising "pre-buddy" with the NUS service UUID `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`.
- [ ] From the host laptop:

      pre-buddy serve --transport ble --device-name pre-buddy --demo

      → demo events arrive at the device serial monitor as raw RX bytes.

If this stage works, you've validated: USB-C → flash → boot → NimBLE
stack → advertising → host-side BLE central pipeline → wire-format
events flowing.

---

## Stage 2 — Peripherals, one at a time

Each peripheral has its own stub with TODOs naming the exact IDF calls.
**Order matters** — each verification depends on the previous one
working.

### 2.1 Display (`esp32_display.cpp`)

Component requires: `lvgl` (or `LovyanGFX` if you prefer the
M5Stack-canonical path).

Wires:

```cpp
void Esp32DisplayDriver::init() {
    M5.Display.begin();    // M5Unified handles ILI9342 + backlight
    initialised_ = true;
}

void Esp32DisplayDriver::show_expression(Character ch, Expression expr) {
    auto idx = pre_buddy::sprites::sprite_index(ch, expr);
    auto* rgb565 = pre_buddy::sprites::SPRITE_TABLE[idx];
    M5.Display.pushImage(
        (M5.Display.width() - 96) / 2,
        20,                       // 20px below the top, leaves room for status
        96, 80, rgb565);
}
```

**Verify:** boot picker shows the current character's neutral face.

### 2.2 LED ring (`esp32_led.cpp`)

Component requires: `driver` (already declared). Use RMT TX channel.

```cpp
void Esp32LedDriver::init() {
    rmt_tx_channel_config_t cfg = { ... };  // SK6812 timing
    rmt_new_tx_channel(&cfg, &handle_);
    initialised_ = true;
}

void Esp32LedDriver::set_color(LedColor color) {
    last_color_ = color;
    auto rgb = apply_brightness(to_rgb888(color), brightness_);
    // SK6812 ring on the M5 body is 1 LED; W-byte order if RGBW variant.
    uint8_t bytes[3] = {rgb.g, rgb.r, rgb.b};
    rmt_transmit(handle_, encoder_, bytes, sizeof(bytes), &tx_cfg);
}
```

**Verify:** `pre-buddy serve --transport ble --demo` cycles the LED
through green / blue / purple as bg_agent / router events fire.

### 2.3 Servos (`esp32_servo.cpp`)

Component requires: `driver`. LEDC peripheral.

```cpp
void Esp32ServoDriver::init() {
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_16_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 50,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer);

    // Two channels — one per servo. GPIOs depend on the kit wiring.
    for (auto [gpio, channel] : {{GPIO_X, LEDC_CHANNEL_0},
                                  {GPIO_Y, LEDC_CHANNEL_1}}) {
        ledc_channel_config_t cfg = { ... };
        ledc_channel_config(&cfg);
    }
    initialised_ = true;
}

void Esp32ServoDriver::move(const MotionCommand& cmd) {
    if (!initialised_) return;
    auto duty_x = angle_to_duty(cmd.head_x_deg + 90.0f);   // [-90,+90] → [0,180]
    auto duty_y = angle_to_duty(cmd.head_y_deg);
    ledc_set_fade_with_time(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty_x, cmd.duration_ms);
    ledc_set_fade_with_time(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, duty_y, cmd.duration_ms);
    ledc_fade_start(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, LEDC_FADE_NO_WAIT);
    ledc_fade_start(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, LEDC_FADE_NO_WAIT);
}
```

**Verify:** wake-word event triggers a head turn toward the dominant mic.

### 2.4 NVS character store (`esp32_character_store.cpp`)

Component requires: `nvs_flash` (already declared).

```cpp
void Esp32NvsCharacterStore::init() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    nvs_open("pre_buddy", NVS_READWRITE, &handle_);
    initialised_ = true;
}

bool Esp32NvsCharacterStore::has_character() const {
    size_t len = 0;
    return nvs_get_str(handle_, "character", nullptr, &len) == ESP_OK;
}

Character Esp32NvsCharacterStore::load() const {
    char buf[16] = {};
    size_t len = sizeof(buf);
    if (nvs_get_str(handle_, "character", buf, &len) != ESP_OK) return Character::Sage;
    Character out;
    return parse_character(buf, out) ? out : Character::Sage;
}

void Esp32NvsCharacterStore::save(Character c) {
    nvs_set_str(handle_, "character", std::string(to_string(c)).c_str());
    nvs_commit(handle_);
}
```

**Verify:** pick a character via the picker, power-cycle the device,
and the picker is skipped on the second boot.

### 2.5 Microphone (`esp32_mic.cpp`)

Component requires: `driver`. CoreS3 uses the SPM1423 PDM mic on I2S0.

```cpp
void Esp32MicDriver::start_capture(uint32_t sr, size_t frame, MicFrameFn cb, void* user) {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    i2s_new_channel(&chan_cfg, nullptr, &rx_handle_);
    i2s_pdm_rx_config_t pdm = { /* sample_rate_hz, gpio_pins, ... */ };
    i2s_channel_init_pdm_rx_mode(rx_handle_, &pdm);
    i2s_channel_enable(rx_handle_);

    capturing_ = true;
    xTaskCreate(_capture_task, "mic", 4096, this, 5, &task_);
    callback_ = cb; user_data_ = user;
}

// _capture_task loops:
//   i2s_channel_read(rx_handle_, frame_buf, frame_size * 2, &got, portMAX_DELAY);
//   callback_(frame_buf, frame_size, user_data_);
```

**Verify:** `pre-buddy serve --transport ble` receives
`pre.audio.input_frame` events when the device opens an input session.

### 2.6 Speaker (`esp32_speaker.cpp`)

Component requires: `driver`. CoreS3 routes I2S TX to the AW88298 amp
(I2C-configured), then the speaker.

```cpp
void Esp32SpeakerDriver::start_playback(uint32_t sr) {
    // I2C: enable the AW88298 amp first.
    M5.Speaker.begin();    // M5Unified does the amp + I2S setup for you.
    playing_ = true;
}

size_t Esp32SpeakerDriver::play_frame(const int16_t* samples, size_t n) {
    if (!playing_) return 0;
    M5.Speaker.playRaw(samples, n, /*sample_rate=*/16000, /*stereo=*/false);
    return n;
}
```

**Verify:** play a known PCM file via the tray's "Test speaker" affordance (TBD).

### 2.7 Wake-word detector (`esp32_wake_word.cpp`)

Component requires: `esp-sr` (already declared).

```cpp
void Esp32WakeWordDetector::start(std::string_view phrase, WakeWordFn cb, void* user) {
    srmodel_list_t* models = esp_srmodel_init("model");
    char* wn_name = esp_srmodel_filter(models, ESP_WN_PREFIX, nullptr);
    afe_handle_ = esp_afe_sr_v1.create_from_config(...);

    active_phrase_ = supports(phrase) ? std::string(phrase) : "hey buddy";
    callback_ = cb; user_data_ = user;
    running_ = true;
    xTaskCreate(_detect_task, "wn", 8192, this, 5, &task_);
}

// _detect_task:
//   while (running_) {
//     // Feed the same PCM ring buffer the mic_driver writes into.
//     afe->feed(afe_handle_, pcm_block);
//     auto fetched = afe->fetch(afe_handle_);
//     if (fetched && fetched->wakeup_state == WAKENET_DETECTED) {
//         callback_(active_phrase_, 1.0f, user_data_);
//     }
//   }
```

**Verify:** say "hey buddy" → `pre.audio.wake_word_detected` arrives on
the host. The robot turns its head toward the dominant mic (the existing
`pre.system.wake_word` mapping handles this).

---

## Stage 3 — First-boot input binding

Replace the placeholder body in `firmware/esp32/main/main.cpp`:

```cpp
auto outcome = pb::determine_initial_character(
    store,
    [&display, &servo, &led](pb::CharacterPicker& picker) {
        while (!picker.is_confirmed()) {
            display.show_character(picker.current());
            led.set_color(pb::profile_for(picker.current()).idle_color);
            M5.update();
            if (M5.BtnB.wasReleased())      picker.next();
            else if (M5.BtnA.wasReleased()) picker.prev();
            else if (M5.BtnC.wasReleased() || M5.BtnPWR.wasReleased())
                return picker.confirm();
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        return picker.confirm();
    });
```

**Verify:** factory-fresh device boots into the picker, lets you cycle
characters with BtnA/BtnB, confirm with BtnC.

---

## Stage 4 — End-to-end verification

The full loop:

- [ ] Fresh device boots into picker → pick character → confirm → idle face
- [ ] Power-cycle → picker skipped, idle face from NVS
- [ ] `pre-buddy tray` → click Connect → tray says `● Connected to pre-buddy`
- [ ] Demo events drive the LED + face + head movement
- [ ] Tray ▸ Character ▸ Sentinel → device face switches live
- [ ] Say "hey buddy what's on my calendar" → Whisper STT → PRE replies → device speaks

---

## Stage 5 — Polish (open-ended, post-MVP)

| Item | Notes |
|---|---|
| Per-unit servo calibration | NVS-stored `ServoCalibration`; expose in tray's Settings panel (build new) |
| Quiet hours schedule | Switch to severity=quiet on a cron, reduces motion + dims LED |
| Stall detection | If servo doesn't move after N commands → emit `pre.system.error`, disable servos |
| Battery state | New `pre.system.battery` event → tray badge |
| Custom wake word | Espressif's online keyword service (free ≤ 2 phrases) or Porcupine paid tier |
| Vision | Not in protocol yet. Needs `pre.vision.frame` (base64 JPEG) + a server-side adapter (Claude Vision / local CLIP) |

---

## After the first unit works: assembly-line provisioning

Once Stages 1–4 work end-to-end on the prototype unit, run
`tools/provision_device.sh` against subsequent boards. The script:

1. Detects the device on USB (`/dev/cu.usbmodem*`)
2. Flashes the latest firmware build
3. Writes default NVS config (no character — picker runs on first boot)
4. Runs a smoke test sequence over BLE (LED cycle, servo sweep, mic loop-back)
5. Prints a label with device MAC + build hash for tracking

That script is a skeleton today (lots of TODOs); it gets fleshed out
with the same iteration that proves out the first board.
