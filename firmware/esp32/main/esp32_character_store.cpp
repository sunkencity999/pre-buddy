// SPDX-License-Identifier: TBD
// ESP32-S3 NVS-backed character store — stub implementation.
//
// Storage layout (NVS namespace "pre_buddy"):
//   key="character" → string of one of {"sage","sprout","sentinel"}.

#include "esp32_character_store.h"

namespace pre_buddy::esp32 {

void Esp32NvsCharacterStore::init() noexcept {
    // TODO: nvs_flash_init() + nvs_open("pre_buddy", NVS_READWRITE, &h_).
    initialised_ = true;
}

bool Esp32NvsCharacterStore::has_character() const noexcept {
    if (!initialised_) return false;
    // TODO: size_t len = 0;
    //       esp_err_t e = nvs_get_str(h_, "character", nullptr, &len);
    //       return e == ESP_OK;
    return false;
}

Character Esp32NvsCharacterStore::load() const noexcept {
    if (!initialised_) return Character::Sage;
    // TODO:
    //   char buf[16] = {};
    //   size_t len = sizeof(buf);
    //   if (nvs_get_str(h_, "character", buf, &len) != ESP_OK) return Sage;
    //   Character out;
    //   return parse_character(buf, out) ? out : Character::Sage;
    return Character::Sage;
}

void Esp32NvsCharacterStore::save(Character c) noexcept {
    if (!initialised_) return;
    (void)c;
    // TODO: nvs_set_str(h_, "character", to_string(c).data()) + nvs_commit(h_).
}

void Esp32NvsCharacterStore::clear() noexcept {
    if (!initialised_) return;
    // TODO: nvs_erase_key(h_, "character"); nvs_commit(h_).
}

}  // namespace pre_buddy::esp32
