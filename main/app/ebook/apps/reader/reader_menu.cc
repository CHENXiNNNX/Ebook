#include "apps/reader/reader_menu.hpp"

#include <cstdio>
#include <cstring>

#include "gfx/text_layout.hpp"
#include "ui/theme.hpp"

namespace app::ebook::apps::reader {

namespace {

using ui::Theme;

void draw_cell(gfx::Canvas& c, const core::Rect& r, const char* label)
{
    if (r.empty())
        return;
    c.fill(r, gfx::Ink::White);
    if (r.w >= 2 && r.h >= 2)
    {
        c.hline(r.x, r.y, r.w);
        c.hline(r.x, static_cast<int16_t>(r.y + r.h - 1), r.w);
        c.vline(r.x, r.y, r.h);
        c.vline(static_cast<int16_t>(r.x + r.w - 1), r.y, r.h);
    }
    if (label == nullptr || label[0] == '\0')
        return;

    char show[32];
    const uint16_t max_w = (r.w > 4) ? static_cast<uint16_t>(r.w - 4) : r.w;
    (void)gfx::truncate_text(label, Theme::kFontSmall, max_w, show, sizeof(show));

    gfx::Canvas::TextStyle ts{};
    ts.size_px = Theme::kFontSmall;
    ts.h       = gfx::HAlign::Center;
    ts.v       = gfx::VAlign::Middle;
    c.text_in(r, show, ts);
}

core::Rect row_rect(const ReaderLayout& layout, uint8_t row, uint8_t col, uint8_t col_count)
{
    const int16_t base_y = layout.menu_panel_y();
    const int16_t y =
        static_cast<int16_t>(base_y + row * (ReaderLayout::kMenuRowH + ReaderLayout::kMenuRowGap));
    const uint16_t cell_w = static_cast<uint16_t>(Theme::kScreenW / col_count);
    const int16_t  x      = static_cast<int16_t>(col * cell_w);
    const uint16_t w      = (col + 1 == col_count)
        ? static_cast<uint16_t>(Theme::kScreenW - col * cell_w) : cell_w;
    return core::Rect{x, y, w, ReaderLayout::kMenuRowH};
}

} // namespace

void paint_menu_panel(gfx::Canvas& c, const ReaderLayout& layout, const MenuPaintInfo& info)
{
    const core::Rect panel = layout.menu_panel_rect();
    c.fill(panel, gfx::Ink::White);
    c.hline(panel.x, panel.y, panel.w);

    draw_cell(c, row_rect(layout, 0, 0, 3), "<");
    draw_cell(c, row_rect(layout, 0, 1, 3), info.font_label);
    draw_cell(c, row_rect(layout, 0, 2, 3), ">");

    draw_cell(c, row_rect(layout, 1, 0, 3), "<");
    draw_cell(c, row_rect(layout, 1, 1, 3), info.refresh_label);
    draw_cell(c, row_rect(layout, 1, 2, 3), ">");

    draw_cell(c, row_rect(layout, 2, 0, 5), "<<");
    draw_cell(c, row_rect(layout, 2, 1, 5), "<");

    char pct_str[8];
    (void)std::snprintf(pct_str, sizeof(pct_str), "%u%%", static_cast<unsigned>(info.read_percent));
    draw_cell(c, row_rect(layout, 2, 2, 5), pct_str);

    draw_cell(c, row_rect(layout, 2, 3, 5), ">");
    draw_cell(c, row_rect(layout, 2, 4, 5), ">>");

    draw_cell(c, row_rect(layout, 3, 0, 3), "\u4E66\u5E93");
    draw_cell(c, row_rect(layout, 3, 1, 3), "\u8DF3\u8F6C");
    draw_cell(c, row_rect(layout, 3, 2, 3), "\u9501\u5C4F");
}

MenuHit hit_menu(int16_t x, int16_t y, const ReaderLayout& layout)
{
    if (y < layout.menu_panel_y())
        return MenuHit::None;

    const auto hit = [&](uint8_t row, uint8_t col, uint8_t cols, MenuHit h) {
        return row_rect(layout, row, col, cols).contains(x, y) ? h : MenuHit::None;
    };

    if (auto h = hit(0, 0, 3, MenuHit::FontPrev); h != MenuHit::None) return h;
    if (auto h = hit(0, 2, 3, MenuHit::FontNext); h != MenuHit::None) return h;
    if (auto h = hit(1, 0, 3, MenuHit::RefreshPrev); h != MenuHit::None) return h;
    if (auto h = hit(1, 2, 3, MenuHit::RefreshNext); h != MenuHit::None) return h;
    if (auto h = hit(2, 0, 5, MenuHit::JumpPrev10); h != MenuHit::None) return h;
    if (auto h = hit(2, 1, 5, MenuHit::PagePrev); h != MenuHit::None) return h;
    if (auto h = hit(2, 3, 5, MenuHit::PageNext); h != MenuHit::None) return h;
    if (auto h = hit(2, 4, 5, MenuHit::JumpNext10); h != MenuHit::None) return h;
    if (auto h = hit(3, 0, 3, MenuHit::Shelf); h != MenuHit::None) return h;
    if (auto h = hit(3, 1, 3, MenuHit::JumpPage); h != MenuHit::None) return h;
    if (auto h = hit(3, 2, 3, MenuHit::Lock); h != MenuHit::None) return h;
    return MenuHit::None;
}

} // namespace app::ebook::apps::reader
