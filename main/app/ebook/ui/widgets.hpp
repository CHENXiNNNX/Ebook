#pragma once

#include <cstdint>

#include "core/geometry.hpp"
#include "gfx/canvas.hpp"
#include "ui/theme.hpp"

namespace app::ebook::ui::widgets {

void label(gfx::Canvas& c, int16_t x, int16_t y_top, const char* text,
           uint8_t size_px = Theme::kFontBody);

void label_in(gfx::Canvas& c, const core::Rect& box, const char* text,
              gfx::HAlign h, gfx::VAlign v, uint8_t size_px = Theme::kFontBody);

/** 空状态：居中标题 + 可选多行说明 */
void empty_state(gfx::Canvas& c, const core::Rect& box, const char* title,
                 const char* const* hint_lines = nullptr, uint8_t hint_line_count = 0);

void toolbar(gfx::Canvas& c, const char* title);
bool hit_toolbar_back(int16_t x, int16_t y);

struct RowStyle
{
    const char* label{nullptr};
    const char* value{nullptr};
    uint32_t    icon_cp{0};
    uint32_t    value_icon{0};
    uint8_t     value_icon_size{16};
    uint32_t    action_icon{0};
    uint8_t     action_icon_size{18};
    bool        show_chevron{true};
    bool        inverted{false};
};

inline constexpr uint16_t kRowActionWidth = 36;

bool hit_row_action(const core::Rect& row_rect, int16_t x, int16_t y);
void list_row(gfx::Canvas& c, const core::Rect& row_rect, const RowStyle& s);

void scrollbar(gfx::Canvas& c, uint16_t track_y, uint16_t track_h,
               uint16_t offset, uint16_t total, uint16_t visible);

void progress_bar(gfx::Canvas& c, const core::Rect& bar, uint8_t pct);

uint16_t battery_indicator(gfx::Canvas& c, int16_t right_edge, int16_t y_top,
                           uint8_t pct, uint16_t bar_h = Theme::kStatusBarH);

void card_border(gfx::Canvas& c, const core::Rect& r);
void cover_placeholder(gfx::Canvas& c, const core::Rect& r, const char* label);

core::Rect grid_cell(const core::Rect& area, uint8_t cols, uint8_t rows,
                     uint8_t index, uint16_t gap = Theme::kPad);

int8_t grid_hit_test(const core::Rect& area, uint8_t cols, uint8_t rows,
                     int16_t x, int16_t y, uint16_t gap = Theme::kPad);

} // namespace app::ebook::ui::widgets
