// SPDX-License-Identifier: TBD
// ESP32-S3 IDisplayDriver — M5Stack CoreS3 (ILI9342) via M5GFX.
//
// M5GFX auto-detects the CoreS3 and brings up the AXP2101 PMU + AW9523
// IO-expander rails that power the panel and backlight, so a bare init()
// is enough to light the screen. Faces are 96x80 RGB565 sprites pushed
// from the generated atlas (sprites_data.h via sprite_atlas.h).

#include "esp32_display.h"

#include <M5GFX.h>

#include "pre_buddy/sprite_atlas.h"
#include "sprites_data.h"

namespace pre_buddy::esp32 {
namespace {

M5GFX g_gfx;

constexpr int kSpriteW = pre_buddy::sprites::SPRITE_WIDTH_PX;   // 96
constexpr int kSpriteH = pre_buddy::sprites::SPRITE_HEIGHT_PX;  // 80

constexpr std::uint16_t kBlack = 0x0000;
constexpr std::uint16_t kWhite = 0xFFFF;

}  // namespace

void Esp32DisplayDriver::init() noexcept {
    g_gfx.init();
    g_gfx.setRotation(1);                 // CoreS3 landscape, 320x240
    g_gfx.fillScreen(kBlack);

    // Immediate sign-of-life banner before the first face is drawn — proves
    // the panel + PMU came up even if a later sprite push has issues.
    g_gfx.setTextColor(kWhite);
    g_gfx.setTextSize(2);
    g_gfx.drawString("PRE Buddy", 95, 110);

    initialised_ = true;
}

void Esp32DisplayDriver::show_character(Character ch) noexcept {
    // The idle face is the Neutral expression for the character.
    show_expression(ch, Expression::Neutral);
}

void Esp32DisplayDriver::show_expression(Character ch, Expression expr) noexcept {
    if (!initialised_) return;
    if (drawn_ && ch == last_ch_ && expr == last_expr_) return;  // no-op redraw

    const auto idx = pre_buddy::sprites::sprite_index(ch, expr);
    const std::uint16_t* img = pre_buddy::sprites::SPRITE_TABLE[idx];

    // Clear first (wipes the boot banner / previous face), then draw the
    // 96x80 sprite scaled up and centered so the face fills the panel.
    // Sprites are authored for native RGB565 byte order; if colors ever
    // look swapped, add g_gfx.setSwapBytes(true) in init().
    g_gfx.fillScreen(kBlack);
    constexpr float kZoom = 2.5f;   // 96x80 -> 240x200 on the 320x240 panel
    g_gfx.pushImageRotateZoom(
        g_gfx.width() / 2.0f, g_gfx.height() / 2.0f,  // destination center
        kSpriteW / 2.0f, kSpriteH / 2.0f,             // source anchor (center)
        0.0f, kZoom, kZoom,
        kSpriteW, kSpriteH, img);

    last_ch_ = ch;
    last_expr_ = expr;
    drawn_ = true;
}

void Esp32DisplayDriver::show_banner(std::string_view text) noexcept {
    if (!initialised_) return;
    // Clear a strip along the bottom and draw the (truncated) banner there,
    // leaving the face above untouched.
    const int strip_h = 28;
    const int y0 = g_gfx.height() - strip_h;
    g_gfx.fillRect(0, y0, g_gfx.width(), strip_h, kBlack);
    g_gfx.setTextColor(kWhite);
    g_gfx.setTextSize(2);
    char buf[33];
    const std::size_t n = text.size() < sizeof(buf) - 1 ? text.size() : sizeof(buf) - 1;
    for (std::size_t i = 0; i < n; ++i) buf[i] = text[i];
    buf[n] = '\0';
    g_gfx.drawString(buf, 6, y0 + 4);
}

void Esp32DisplayDriver::show_passkey(unsigned int code) noexcept {
    if (!initialised_) return;
    g_gfx.fillScreen(kBlack);
    g_gfx.setTextColor(kWhite);
    g_gfx.setTextSize(4);
    char buf[16];
    // 6-digit BLE passkey, zero-padded.
    for (int i = 5; i >= 0; --i) { buf[i] = static_cast<char>('0' + (code % 10)); code /= 10; }
    buf[6] = '\0';
    g_gfx.drawString(buf, 70, 100);
    // Mark the face state dirty so the idle face redraws after pairing.
    drawn_ = false;
}

void Esp32DisplayDriver::clear() noexcept {
    if (!initialised_) return;
    g_gfx.fillScreen(kBlack);
    drawn_ = false;
}

}  // namespace pre_buddy::esp32
