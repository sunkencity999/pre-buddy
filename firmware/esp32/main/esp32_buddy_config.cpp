// SPDX-License-Identifier: TBD
// PRE Buddy — user settings: JSON decode + NVS persistence (see header).

#include "esp32_buddy_config.h"

#include <cstring>

#include "cJSON.h"
#include "esp_log.h"
#include "nvs.h"

namespace pre_buddy::esp32 {
namespace {

constexpr const char* TAG = "pre-buddy-cfg";
constexpr const char* kNamespace = "buddycfg";

}  // namespace

void buddy_config_load(BuddyConfig& cfg) noexcept {
    nvs_handle_t h;
    if (nvs_open(kNamespace, NVS_READONLY, &h) != ESP_OK) return;  // none saved yet
    uint8_t u8;
    int32_t i32;
    if (nvs_get_u8(h, "led_ovr", &u8) == ESP_OK) cfg.led_override = (u8 != 0);
    if (nvs_get_u8(h, "led_col", &u8) == ESP_OK) cfg.led_color = static_cast<LedColor>(u8);
    if (nvs_get_u8(h, "led_bri", &u8) == ESP_OK) cfg.led_brightness = u8;
    if (nvs_get_i32(h, "volume", &i32) == ESP_OK) cfg.volume = i32;
    if (nvs_get_u8(h, "idle_an", &u8) == ESP_OK) cfg.idle_anim = (u8 != 0);
    if (nvs_get_u8(h, "think_an", &u8) == ESP_OK) cfg.thinking_anim = (u8 != 0);
    if (nvs_get_u8(h, "chime", &u8) == ESP_OK) cfg.boot_chime = (u8 != 0);
    nvs_close(h);
    ESP_LOGI(TAG, "loaded: ovr=%d col=%d bri=%u vol=%d idle=%d think=%d chime=%d",
             cfg.led_override, static_cast<int>(cfg.led_color), cfg.led_brightness,
             cfg.volume, cfg.idle_anim, cfg.thinking_anim, cfg.boot_chime);
}

void buddy_config_save(const BuddyConfig& cfg) noexcept {
    nvs_handle_t h;
    if (nvs_open(kNamespace, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open(rw) failed — settings not persisted");
        return;
    }
    nvs_set_u8(h, "led_ovr", cfg.led_override ? 1 : 0);
    nvs_set_u8(h, "led_col", static_cast<uint8_t>(cfg.led_color));
    nvs_set_u8(h, "led_bri", cfg.led_brightness);
    nvs_set_i32(h, "volume", cfg.volume);
    nvs_set_u8(h, "idle_an", cfg.idle_anim ? 1 : 0);
    nvs_set_u8(h, "think_an", cfg.thinking_anim ? 1 : 0);
    nvs_set_u8(h, "chime", cfg.boot_chime ? 1 : 0);
    nvs_commit(h);
    nvs_close(h);
}

bool buddy_config_apply_json(BuddyConfig& cfg, const char* buf, std::size_t n) noexcept {
    cJSON* root = cJSON_ParseWithLength(buf, n);
    if (root == nullptr) return false;
    const cJSON* ev = cJSON_GetObjectItemCaseSensitive(root, "event");
    if (!cJSON_IsString(ev) || ev->valuestring == nullptr ||
        std::strcmp(ev->valuestring, "pre.buddy.config") != 0) {
        cJSON_Delete(root);
        return false;
    }
    const cJSON* d = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (cJSON_IsObject(d)) {
        const cJSON* c;
        if ((c = cJSON_GetObjectItemCaseSensitive(d, "led_color")) != nullptr &&
            cJSON_IsString(c) && c->valuestring != nullptr) {
            if (std::strcmp(c->valuestring, "auto") == 0) {
                cfg.led_override = false;  // follow the character default again
            } else {
                LedColor lc;
                if (parse_led_color(c->valuestring, lc)) {
                    cfg.led_override = true;
                    cfg.led_color = lc;
                }
            }
        }
        if ((c = cJSON_GetObjectItemCaseSensitive(d, "led_brightness")) != nullptr &&
            cJSON_IsNumber(c)) {
            int v = static_cast<int>(c->valuedouble);
            cfg.led_brightness = static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
        }
        if ((c = cJSON_GetObjectItemCaseSensitive(d, "volume")) != nullptr &&
            cJSON_IsNumber(c)) {
            int v = static_cast<int>(c->valuedouble);
            cfg.volume = v < 0 ? 0 : (v > 100 ? 100 : v);
        }
        if ((c = cJSON_GetObjectItemCaseSensitive(d, "idle_anim")) != nullptr &&
            cJSON_IsBool(c)) {
            cfg.idle_anim = cJSON_IsTrue(c);
        }
        if ((c = cJSON_GetObjectItemCaseSensitive(d, "thinking_anim")) != nullptr &&
            cJSON_IsBool(c)) {
            cfg.thinking_anim = cJSON_IsTrue(c);
        }
        if ((c = cJSON_GetObjectItemCaseSensitive(d, "boot_chime")) != nullptr &&
            cJSON_IsBool(c)) {
            cfg.boot_chime = cJSON_IsTrue(c);
        }
        buddy_config_save(cfg);
        ESP_LOGI(TAG, "config applied: ovr=%d col=%d bri=%u vol=%d idle=%d think=%d chime=%d",
                 cfg.led_override, static_cast<int>(cfg.led_color), cfg.led_brightness,
                 cfg.volume, cfg.idle_anim, cfg.thinking_anim, cfg.boot_chime);
    }
    cJSON_Delete(root);
    return true;
}

}  // namespace pre_buddy::esp32
