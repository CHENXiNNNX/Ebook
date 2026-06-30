#pragma once

#include <cstddef>
#include <cstdint>

#include "apps/settings/settings_app.hpp"
#include "gfx/canvas.hpp"
#include "input/input_event.hpp"

namespace app::ebook::apps::settings::detail {

constexpr const char* const TAG = "Settings";

constexpr int8_t kTzOffsets[] = {
    -12, -11, -10, -9, -8, -7, -6, -5, -4, -3, -2, -1,
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
};
constexpr uint8_t kTzCount = sizeof(kTzOffsets) / sizeof(kTzOffsets[0]);

const char* page_title(SettingsPage p);
void        copy_str(char* dst, size_t cap, const char* src);
bool        ensure_wifi();

void    draw_slider_row(gfx::Canvas& c, int16_t y, const char* label, uint8_t pct);
bool    slider_hit_pct(int16_t x, uint8_t& out_pct);
bool    scroll_rows(uint8_t& scroll_row, uint8_t total, uint8_t visible,
                    ::app::ebook::input::EventType type);
uint8_t tap_row_index(int16_t y);

} // namespace app::ebook::apps::settings::detail
