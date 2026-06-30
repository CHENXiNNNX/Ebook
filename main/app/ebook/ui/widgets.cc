#include "ui/widgets.hpp"

#include <cstdio>

#include "gfx/icon.hpp"
#include "gfx/text_layout.hpp"

namespace app::ebook::ui::widgets {

void label(gfx::Canvas& c, int16_t x, int16_t y_top, const char* text, uint8_t size_px)
{
    if (text == nullptr) return;
    c.text(x, y_top, text, size_px);
}

void label_in(gfx::Canvas& c, const core::Rect& box, const char* text,
              gfx::HAlign h, gfx::VAlign v, uint8_t size_px)
{
    if (text == nullptr || box.empty()) return;
    gfx::Canvas::TextStyle s{};
    s.size_px = size_px;
    s.h       = h;
    s.v       = v;
    c.text_in(box, text, s);
}

void empty_state(gfx::Canvas& c, const core::Rect& box, const char* title,
                 const char* const* hint_lines, uint8_t hint_line_count)
{
    if (box.empty() || title == nullptr) return;

    constexpr uint8_t kTitleSize = Theme::kFontBody;
    constexpr uint8_t kHintSize  = Theme::kFontSmall;
    constexpr uint8_t kGap       = 6;
    constexpr uint8_t kMaxHints  = 4;

    if (hint_line_count > kMaxHints) hint_line_count = kMaxHints;

    const uint8_t title_lh = static_cast<uint8_t>(kTitleSize + 4);
    const uint8_t hint_lh  = static_cast<uint8_t>(kHintSize + 2);
    const uint16_t block_h = static_cast<uint16_t>(
        title_lh + (hint_line_count > 0
                        ? kGap + static_cast<uint16_t>(hint_line_count) * hint_lh
                        : 0));
    int16_t y = static_cast<int16_t>(box.y + (box.h > block_h ? (box.h - block_h) / 2 : 0));

    gfx::Canvas::TextStyle ts{};
    ts.h = gfx::HAlign::Center;
    ts.v = gfx::VAlign::Middle;

    char line_buf[40];

    ts.size_px = kTitleSize;
    const core::Rect title_row{box.x, y, box.w, title_lh};
    (void)gfx::truncate_text(title, kTitleSize, box.w, line_buf, sizeof(line_buf));
    c.text_in(title_row, line_buf, ts);
    y = static_cast<int16_t>(y + title_lh);

    if (hint_line_count == 0 || hint_lines == nullptr) return;

    y = static_cast<int16_t>(y + kGap);
    ts.size_px = kHintSize;
    for (uint8_t i = 0; i < hint_line_count; ++i)
    {
        const char* hint = hint_lines[i];
        if (hint == nullptr || hint[0] == '\0') continue;
        const core::Rect row{box.x, y, box.w, hint_lh};
        (void)gfx::truncate_text(hint, kHintSize, box.w, line_buf, sizeof(line_buf));
        c.text_in(row, line_buf, ts);
        y = static_cast<int16_t>(y + hint_lh);
    }
}

void toolbar(gfx::Canvas& c, const char* title)
{
    constexpr uint8_t kBackIconSize = 18;

    const core::Rect bar = Theme::toolbar_rect();

    // 左：返回图标（命中区 kBackTapW）
    const int16_t back_x = static_cast<int16_t>(Theme::kPadLg);
    const int16_t back_y = static_cast<int16_t>(bar.y + (bar.h - kBackIconSize) / 2);
    c.glyph(back_x, back_y, ::app::ebook::gfx::icon::kFaReply,
            kBackIconSize, gfx::FontFace::Icon);

    // 中：标题
    if (title != nullptr && title[0] != '\0')
    {
        gfx::Canvas::TextStyle ts{};
        ts.size_px = Theme::kFontTitle;
        ts.h       = gfx::HAlign::Center;
        ts.v       = gfx::VAlign::Middle;
        c.text_in(bar, title, ts);
    }

    // 底部分割线
    c.hline(0, static_cast<int16_t>(bar.y + bar.h - 1), Theme::kScreenW);
}

bool hit_toolbar_back(int16_t x, int16_t y)
{
    if (y < Theme::kToolbarY || y >= Theme::kListStartY) return false;
    return x >= 0 && x < Theme::kBackTapW;
}

bool hit_row_action(const core::Rect& row_rect, int16_t x, int16_t y)
{
    if (row_rect.empty()) return false;
    const core::Rect area{
        static_cast<int16_t>(row_rect.x + row_rect.w - kRowActionWidth),
        row_rect.y, kRowActionWidth, row_rect.h};
    return area.contains(x, y);
}

void list_row(gfx::Canvas& c, const core::Rect& r, const RowStyle& s)
{
    const gfx::Ink ink_fg = s.inverted ? gfx::Ink::White : gfx::Ink::Black;

    if (s.inverted)
        c.fill(r, gfx::Ink::Black);

    int16_t text_x = static_cast<int16_t>(r.x + Theme::kPadLg);

    // 左侧主图标
    if (s.icon_cp != 0)
    {
        const int16_t iy = static_cast<int16_t>(r.y + (r.h - Theme::kFontIconSm) / 2);
        c.glyph(text_x, iy, s.icon_cp, Theme::kFontIconSm,
                gfx::FontFace::Icon, ink_fg);
        text_x = static_cast<int16_t>(text_x + Theme::kFontIconSm + 6);
    }

    // 标签文本
    if (s.label != nullptr)
    {
        const int16_t ty = static_cast<int16_t>(r.y + (r.h - Theme::kFontBody) / 2);
        c.text(text_x, ty, s.label, Theme::kFontBody, gfx::FontFace::Text, ink_fg);
    }

    // 右端布局：[ value/value_icon ] [ chevron 或 action_icon ]
    const bool has_action = (s.action_icon != 0);
    int16_t right_edge    = static_cast<int16_t>(r.x + r.w - Theme::kPadLg);

    if (has_action)
    {
        const int16_t area_x = static_cast<int16_t>(r.x + r.w - kRowActionWidth);
        const uint8_t isz    = s.action_icon_size;
        const int16_t ax     = static_cast<int16_t>(area_x + (kRowActionWidth - isz) / 2);
        const int16_t ay     = static_cast<int16_t>(r.y + (r.h - isz) / 2);
        c.glyph(ax, ay, s.action_icon, isz, gfx::FontFace::Icon, ink_fg);
        right_edge = static_cast<int16_t>(area_x - 4);
    }
    else if (s.show_chevron)
    {
        const int16_t cx = static_cast<int16_t>(r.x + r.w - Theme::kPadLg - 10);
        const int16_t cy = static_cast<int16_t>(r.y + (r.h - Theme::kFontIconSm) / 2);
        c.glyph(cx, cy, ::app::ebook::gfx::icon::kFaChevronRight, Theme::kFontIconSm,
                gfx::FontFace::Icon, ink_fg);
        right_edge = static_cast<int16_t>(cx - 4);
    }

    // 右侧值：图标优先（开关 / 状态），否则文本
    if (s.value_icon != 0)
    {
        const uint8_t isz = s.value_icon_size;
        const int16_t vx  = static_cast<int16_t>(right_edge - isz);
        const int16_t vy  = static_cast<int16_t>(r.y + (r.h - isz) / 2);
        c.glyph(vx, vy, s.value_icon, isz, gfx::FontFace::Icon, ink_fg);
    }
    else if (s.value != nullptr && s.value[0] != '\0')
    {
        const uint16_t vw = gfx::Font::get_instance().measure(s.value, Theme::kFontBody);
        const int16_t  vx = static_cast<int16_t>(right_edge - vw);
        const int16_t  vy = static_cast<int16_t>(r.y + (r.h - Theme::kFontBody) / 2);
        c.text(vx, vy, s.value, Theme::kFontBody, gfx::FontFace::Text, ink_fg);
    }

    // 底部分割线（反色行不画）
    if (!s.inverted)
    {
        c.hline(static_cast<int16_t>(r.x + Theme::kPadLg),
                static_cast<int16_t>(r.y + r.h - 1),
                static_cast<uint16_t>(r.w - Theme::kPadLg * 2));
    }
}

void scrollbar(gfx::Canvas& c, uint16_t track_y, uint16_t track_h,
               uint16_t offset, uint16_t total, uint16_t visible)
{
    constexpr uint16_t kMinThumb = 14;

    if (total <= visible || track_h == 0) return;

    const int16_t  x     = static_cast<int16_t>(Theme::kScreenW - 4);
    const uint16_t bar_w = Theme::kScrollbarW;

    c.vline(static_cast<int16_t>(x + 1), static_cast<int16_t>(track_y), track_h);

    uint16_t thumb = static_cast<uint16_t>(
        (static_cast<uint32_t>(visible) * track_h) / total);
    if (thumb < kMinThumb)   thumb = kMinThumb;
    if (thumb > track_h)     thumb = track_h;

    const uint16_t travel  = static_cast<uint16_t>(track_h - thumb);
    const uint16_t denom   = static_cast<uint16_t>(total - visible);
    const uint16_t thumb_y = static_cast<uint16_t>(
        track_y + (static_cast<uint32_t>(offset) * travel) / (denom == 0 ? 1 : denom));

    c.fill(core::Rect{x, static_cast<int16_t>(thumb_y), bar_w, thumb}, gfx::Ink::Black);
}

void progress_bar(gfx::Canvas& c, const core::Rect& bar, uint8_t pct)
{
    c.rect(bar);
    if (pct == 0 || bar.w <= 4 || bar.h <= 4) return;
    const uint16_t inner_w = static_cast<uint16_t>(bar.w - 4);
    const uint16_t fill_w  = static_cast<uint16_t>(
        (static_cast<uint32_t>(inner_w) * pct) / 100U);
    if (fill_w > 0)
        c.fill(core::Rect{static_cast<int16_t>(bar.x + 2),
                          static_cast<int16_t>(bar.y + 2),
                          fill_w, static_cast<uint16_t>(bar.h - 4)},
               gfx::Ink::Black);
}

uint16_t battery_indicator(gfx::Canvas& c, int16_t right_edge, int16_t y_top,
                           uint8_t pct, uint16_t bar_h)
{
    constexpr uint16_t icon_w = 16, icon_h = 9, knob_w = 2, gap = 3;

    char buf[8];
    (void)std::snprintf(buf, sizeof(buf), "%u%%", static_cast<unsigned>(pct));
    const uint16_t tw      = gfx::Font::get_instance().measure(buf, Theme::kFontSmall);
    const uint16_t total_w = icon_w + knob_w + gap + tw;

    const int16_t bx = static_cast<int16_t>(right_edge - total_w);
    const int16_t by = static_cast<int16_t>(y_top + (bar_h - icon_h) / 2);

    // 外框
    c.rect(core::Rect{bx, by, icon_w, icon_h});
    // 凸起
    c.fill(core::Rect{static_cast<int16_t>(bx + icon_w),
                      static_cast<int16_t>(by + 2),
                      knob_w, static_cast<uint16_t>(icon_h - 4)});
    // 内部填充
    if (pct > 0)
    {
        const uint16_t inner = static_cast<uint16_t>(icon_w - 4);
        const uint16_t fw    = static_cast<uint16_t>(
            (static_cast<uint32_t>(inner) * pct) / 100U);
        if (fw > 0)
            c.fill(core::Rect{static_cast<int16_t>(bx + 2),
                              static_cast<int16_t>(by + 2),
                              fw, static_cast<uint16_t>(icon_h - 4)});
    }

    // 百分比文本
    const int16_t tx = static_cast<int16_t>(bx + icon_w + knob_w + gap);
    const int16_t ty = static_cast<int16_t>(y_top + (bar_h - Theme::kFontSmall) / 2);
    c.text(tx, ty, buf, Theme::kFontSmall);

    return total_w;
}

void card_border(gfx::Canvas& c, const core::Rect& r)
{
    c.rect(r);
}

void cover_placeholder(gfx::Canvas& c, const core::Rect& r, const char* label)
{
    c.rect(r);
    if (label != nullptr)
    {
        gfx::Canvas::TextStyle s{};
        s.size_px = Theme::kFontSmall;
        s.h       = gfx::HAlign::Center;
        s.v       = gfx::VAlign::Middle;
        s.padding = 2;
        c.text_in(r, label, s);
    }
}

core::Rect grid_cell(const core::Rect& area, uint8_t cols, uint8_t rows,
                     uint8_t index, uint16_t gap)
{
    if (cols == 0 || rows == 0 || index >= cols * rows) return {};

    const uint16_t total_gap_w = static_cast<uint16_t>(gap * (cols + 1));
    const uint16_t total_gap_h = static_cast<uint16_t>(gap * (rows + 1));
    const uint16_t cw = (area.w > total_gap_w)
        ? static_cast<uint16_t>((area.w - total_gap_w) / cols) : 0;
    const uint16_t ch = (area.h > total_gap_h)
        ? static_cast<uint16_t>((area.h - total_gap_h) / rows) : 0;

    const uint8_t col = index % cols;
    const uint8_t row = index / cols;

    return core::Rect{
        static_cast<int16_t>(area.x + gap + col * (cw + gap)),
        static_cast<int16_t>(area.y + gap + row * (ch + gap)),
        cw, ch};
}

int8_t grid_hit_test(const core::Rect& area, uint8_t cols, uint8_t rows,
                     int16_t x, int16_t y, uint16_t gap)
{
    const uint8_t total = static_cast<uint8_t>(cols * rows);
    for (uint8_t i = 0; i < total; ++i)
    {
        if (grid_cell(area, cols, rows, i, gap).contains(x, y))
            return static_cast<int8_t>(i);
    }
    return -1;
}

} // namespace app::ebook::ui::widgets
