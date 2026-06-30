#include "apps/calendar/calendar_app.hpp"

#include <cstdio>
#include <ctime>

#include "data/clock_provider.hpp"
#include "gfx/icon.hpp"
#include "overlays/toast.hpp"
#include "router/refresh_intent.hpp"
#include "ui/strings.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

namespace app::ebook::apps::calendar {

namespace {

using ui::Theme;

constexpr uint16_t kMonthBarH     = 22;
constexpr uint16_t kWeekdayH      = 14;
constexpr uint16_t kGridTopY      = Theme::kListStartY + kMonthBarH + kWeekdayH;
constexpr uint8_t  kNavIconSize   = 14;
constexpr uint16_t kPickerYearMin = 1970;
constexpr uint16_t kPickerYearMax = 2099;

bool is_leap(uint16_t y)
{
    return (y % 4U == 0U && y % 100U != 0U) || (y % 400U == 0U);
}

void draw_nav_btn(gfx::Canvas& c, const core::Rect& btn, uint32_t icon_cp)
{
    c.rect(btn, 1);
    const int16_t ax = static_cast<int16_t>(btn.x + (btn.w - kNavIconSize) / 2);
    const int16_t ay = static_cast<int16_t>(btn.y + (btn.h - kNavIconSize) / 2);
    c.glyph(ax, ay, icon_cp, kNavIconSize, gfx::FontFace::Icon);
}

void fill_dim_cell(gfx::Canvas& c, const core::Rect& cell)
{
    c.rect(cell, 1);
    for (int16_t py = cell.y; py < cell.bottom(); py += 2)
        for (int16_t px = cell.x; px < cell.right(); px += 2)
            c.pixel(px, py);
}

void draw_day_num(gfx::Canvas& c, const core::Rect& cell, uint8_t day,
                  bool dim, gfx::Ink ink = gfx::Ink::Black)
{
    char buf[4];
    (void)std::snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(day));
    gfx::Canvas::TextStyle ts{};
    ts.size_px = dim ? Theme::kFontSmall : Theme::kFontBody;
    ts.h       = gfx::HAlign::Center;
    ts.v       = gfx::VAlign::Middle;
    ts.ink     = ink;
    c.text_in(cell, buf, ts);
}

core::Rect pk_year_row(const core::Rect& panel)
{
    return core::Rect{panel.x, static_cast<int16_t>(panel.y + 22), panel.w, 28};
}
core::Rect pk_year_prev(const core::Rect& panel)
{
    const core::Rect r = pk_year_row(panel);
    return core::Rect{static_cast<int16_t>(r.x + 8), r.y, 36, r.h};
}
core::Rect pk_year_next(const core::Rect& panel)
{
    const core::Rect r = pk_year_row(panel);
    return core::Rect{static_cast<int16_t>(r.right() - 44), r.y, 36, r.h};
}
core::Rect pk_grid(const core::Rect& panel)
{
    const core::Rect y_row = pk_year_row(panel);
    const int16_t  top  = static_cast<int16_t>(y_row.bottom() + 4);
    const int16_t  foot = static_cast<int16_t>(panel.bottom() - 32);
    return core::Rect{static_cast<int16_t>(panel.x + 8), top,
                      static_cast<uint16_t>(panel.w - 16),
                      static_cast<uint16_t>(foot - top)};
}
core::Rect pk_month_cell(const core::Rect& grid, uint8_t month)
{
    if (month < 1 || month > 12) return {};
    const uint8_t  idx = static_cast<uint8_t>(month - 1);
    const uint8_t  row = static_cast<uint8_t>(idx / 3);
    const uint8_t  col = static_cast<uint8_t>(idx % 3);
    const uint16_t cw  = static_cast<uint16_t>(grid.w / 3);
    const uint16_t ch  = static_cast<uint16_t>(grid.h / 4);
    const int16_t  x   = static_cast<int16_t>(grid.x + col * cw);
    const int16_t  y   = static_cast<int16_t>(grid.y + row * ch);
    const uint16_t w   = (col == 2) ? static_cast<uint16_t>(grid.right()  - x) : cw;
    const uint16_t h   = (row == 3) ? static_cast<uint16_t>(grid.bottom() - y) : ch;
    return core::Rect{x, y, w, h};
}
core::Rect pk_footer(const core::Rect& panel)
{
    return core::Rect{panel.x, static_cast<int16_t>(panel.bottom() - 32), panel.w, 32};
}
core::Rect pk_ok(const core::Rect& panel)
{
    const core::Rect f = pk_footer(panel);
    return core::Rect{static_cast<int16_t>(f.x + f.w / 2), f.y,
                      static_cast<uint16_t>(f.w / 2), f.h};
}
core::Rect pk_cancel(const core::Rect& panel)
{
    const core::Rect f = pk_footer(panel);
    return core::Rect{f.x, f.y, static_cast<uint16_t>(f.w / 2), f.h};
}

} // namespace

CalendarApp& CalendarApp::instance()
{
    static CalendarApp s;
    return s;
}

const char* CalendarApp::title()   const { return ui::strings::kAppCalendar; }
uint32_t    CalendarApp::icon_cp() const { return gfx::icon::kFaCalendarAlt; }

void CalendarApp::on_enter()
{
    picker_open_ = false;
    sync_to_clock();
    rebuild_grid();
    request_repaint(router::intent_partial_full());
}

void CalendarApp::on_exit() { picker_open_ = false; }

void CalendarApp::sync_to_clock()
{
    const auto clk = data::Clock::now();
    view_year_  = clk.year;
    view_month_ = clk.month;
    sel_year_   = clk.year;
    sel_month_  = clk.month;
    sel_day_    = clk.day;
}

uint8_t CalendarApp::days_in_month(uint16_t year, uint8_t month)
{
    static const uint8_t kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) return 30;
    if (month == 2 && is_leap(year)) return 29;
    return kDays[month - 1];
}

uint8_t CalendarApp::weekday_sun0(uint16_t year, uint8_t month, uint8_t day)
{
    struct tm t{};
    t.tm_year  = static_cast<int>(year) - 1900;
    t.tm_mon   = static_cast<int>(month) - 1;
    t.tm_mday  = static_cast<int>(day);
    t.tm_isdst = -1;
    (void)mktime(&t);
    return static_cast<uint8_t>(t.tm_wday);
}

void CalendarApp::shift_month(int delta)
{
    int y = static_cast<int>(view_year_);
    int m = static_cast<int>(view_month_) + delta;
    while (m < 1)  { --y; m += 12; }
    while (m > 12) { ++y; m -= 12; }
    view_year_  = static_cast<uint16_t>(y);
    view_month_ = static_cast<uint8_t>(m);

    if (sel_year_ == view_year_ && sel_month_ == view_month_)
    {
        const uint8_t dim = days_in_month(view_year_, view_month_);
        if (sel_day_ > dim) sel_day_ = dim;
    }

    rebuild_grid();
    request_repaint();
}

void CalendarApp::rebuild_grid()
{
    const auto today = data::Clock::now();
    const uint8_t dim = days_in_month(view_year_, view_month_);
    const uint8_t w0  = weekday_sun0(view_year_, view_month_, 1);

    uint16_t py = view_year_;
    uint8_t  pm = view_month_;
    if (pm == 1) { pm = 12; --py; } else { --pm; }
    const uint8_t prev_dim = days_in_month(py, pm);

    uint16_t ny = view_year_;
    uint8_t  nm = view_month_;
    if (nm == 12) { nm = 1; ++ny; } else { ++nm; }

    for (uint8_t i = 0; i < kGridCols * kGridRows; ++i)
    {
        DayCell& c = cells_[i];
        c = {};

        const int slot = static_cast<int>(i) - static_cast<int>(w0) + 1;
        if (slot < 1)
        {
            c.year     = py;
            c.month    = pm;
            c.day      = static_cast<uint8_t>(slot + static_cast<int>(prev_dim));
            c.in_month = false;
        }
        else if (slot > static_cast<int>(dim))
        {
            c.year     = ny;
            c.month    = nm;
            c.day      = static_cast<uint8_t>(slot - static_cast<int>(dim));
            c.in_month = false;
        }
        else
        {
            c.year     = view_year_;
            c.month    = view_month_;
            c.day      = static_cast<uint8_t>(slot);
            c.in_month = true;
        }

        if (c.day > 0)
            c.is_today = (c.year == today.year && c.month == today.month && c.day == today.day);
    }
}

core::Rect CalendarApp::month_bar_rect()
{
    return core::Rect{0, static_cast<int16_t>(Theme::kListStartY),
                      Theme::kScreenW, kMonthBarH};
}

core::Rect CalendarApp::month_title_rect()
{
    const core::Rect bar   = month_bar_rect();
    const uint16_t   btn_w = static_cast<uint16_t>(Theme::kScreenW / 4);
    return core::Rect{static_cast<int16_t>(btn_w), bar.y,
                      static_cast<uint16_t>(Theme::kScreenW - btn_w * 2), bar.h};
}

core::Rect CalendarApp::weekday_row_rect()
{
    return core::Rect{0, static_cast<int16_t>(Theme::kListStartY + kMonthBarH),
                      Theme::kScreenW, kWeekdayH};
}

core::Rect CalendarApp::grid_region_rect()
{
    return core::Rect{0, static_cast<int16_t>(kGridTopY), Theme::kScreenW,
                      static_cast<uint16_t>(Theme::kScreenH - kGridTopY)};
}

core::Rect CalendarApp::cell_rect(uint8_t row, uint8_t col)
{
    const uint16_t cell_w = static_cast<uint16_t>(Theme::kScreenW / kGridCols);
    const uint16_t grid_h = static_cast<uint16_t>(Theme::kScreenH - kGridTopY);
    const uint16_t cell_h = static_cast<uint16_t>(grid_h / kGridRows);

    const int16_t x = static_cast<int16_t>(col * cell_w);
    const int16_t y = static_cast<int16_t>(kGridTopY + row * cell_h);

    const uint16_t w = (col + 1 == kGridCols)
        ? static_cast<uint16_t>(Theme::kScreenW - col * cell_w) : cell_w;
    const uint16_t hh = (row + 1 == kGridRows)
        ? static_cast<uint16_t>(Theme::kScreenH - y) : cell_h;
    return core::Rect{x, y, w, hh};
}

core::Rect CalendarApp::picker_panel_rect()
{
    const int16_t  y = static_cast<int16_t>(Theme::kListStartY + kMonthBarH + 2);
    const uint16_t h = static_cast<uint16_t>(Theme::kScreenH - y - 4);
    return core::Rect{8, y, static_cast<uint16_t>(Theme::kScreenW - 16), h};
}

void CalendarApp::open_picker()
{
    picker_open_  = true;
    picker_year_  = view_year_;
    picker_month_ = view_month_;
    request_repaint();
}

void CalendarApp::close_picker()
{
    if (!picker_open_) return;
    picker_open_ = false;
    request_repaint();
}

void CalendarApp::apply_picker()
{
    view_year_  = picker_year_;
    view_month_ = picker_month_;

    const uint8_t dim = days_in_month(view_year_, view_month_);
    sel_year_  = view_year_;
    sel_month_ = view_month_;
    if (sel_day_ > dim) sel_day_ = dim;
    if (sel_day_ < 1)   sel_day_ = 1;

    rebuild_grid();
    close_picker();
}

void CalendarApp::paint_picker(gfx::Canvas& c)
{
    const core::Rect panel = picker_panel_rect();
    c.fill(panel, gfx::Ink::White);
    c.rect(panel, 2);

    gfx::Canvas::TextStyle ts{};
    ts.size_px = Theme::kFontTitle;
    ts.h       = gfx::HAlign::Center;
    ts.v       = gfx::VAlign::Middle;
    const core::Rect hdr{panel.x, panel.y, panel.w, 22};
    c.text_in(hdr, "\u9009\u62E9\u5E74\u6708", ts);

    draw_nav_btn(c, pk_year_prev(panel), gfx::icon::kFaChevronLeft);
    draw_nav_btn(c, pk_year_next(panel), gfx::icon::kFaChevronRight);

    char yr_buf[16];
    (void)std::snprintf(yr_buf, sizeof(yr_buf), "%u\u5E74", static_cast<unsigned>(picker_year_));
    ts.size_px = Theme::kFontBody;
    c.text_in(pk_year_row(panel), yr_buf, ts);

    const core::Rect grid = pk_grid(panel);
    for (uint8_t m = 1; m <= 12; ++m)
    {
        const core::Rect cell = pk_month_cell(grid, m);
        const bool picked = (m == picker_month_);

        if (picked) c.fill(cell, gfx::Ink::Black);
        else        c.rect(cell, 1);

        char buf[8];
        (void)std::snprintf(buf, sizeof(buf), "%u\u6708", static_cast<unsigned>(m));
        ts.ink = picked ? gfx::Ink::White : gfx::Ink::Black;
        c.text_in(cell, buf, ts);
    }

    const core::Rect foot = pk_footer(panel);
    c.hline(foot.x, foot.y, foot.w);
    ts.ink = gfx::Ink::Black;
    c.text_in(pk_cancel(panel), ui::strings::kCancel, ts);
    c.vline(static_cast<int16_t>(foot.x + foot.w / 2), foot.y, foot.h);
    c.text_in(pk_ok(panel), ui::strings::kOk, ts);
}

shell::InputResult CalendarApp::handle_picker_input(int16_t x, int16_t y)
{
    const core::Rect panel = picker_panel_rect();
    if (!panel.contains(x, y)) { close_picker(); return {true}; }

    if (pk_cancel(panel).contains(x, y)) { close_picker(); return {true}; }
    if (pk_ok(panel).contains(x, y))     { apply_picker(); return {true}; }

    if (pk_year_prev(panel).contains(x, y))
    {
        if (picker_year_ > kPickerYearMin) --picker_year_;
        request_repaint();
        return {true};
    }
    if (pk_year_next(panel).contains(x, y))
    {
        if (picker_year_ < kPickerYearMax) ++picker_year_;
        request_repaint();
        return {true};
    }

    const core::Rect grid = pk_grid(panel);
    for (uint8_t m = 1; m <= 12; ++m)
    {
        if (pk_month_cell(grid, m).contains(x, y))
        {
            picker_month_ = m;
            request_repaint();
            return {true};
        }
    }
    return {true};
}

void CalendarApp::select_cell(uint8_t index)
{
    if (index >= kGridCols * kGridRows) return;
    const DayCell& cell = cells_[index];
    if (cell.day == 0) return;

    sel_year_  = cell.year;
    sel_month_ = cell.month;
    sel_day_   = cell.day;

    if (!cell.in_month)
    {
        view_year_  = cell.year;
        view_month_ = cell.month;
        rebuild_grid();
    }

    data::Clock clk{};
    clk.year    = cell.year;
    clk.month   = cell.month;
    clk.day     = cell.day;
    clk.weekday = weekday_sun0(cell.year, cell.month, cell.day);

    char date_buf[24];
    clk.format_date_cn(date_buf, sizeof(date_buf));
    char msg[40];
    (void)std::snprintf(msg, sizeof(msg), "%s %s", date_buf, clk.weekday_name_cn());
    overlays::Toast::instance().show(msg, 1800);

    request_repaint();
}

void CalendarApp::paint(gfx::Canvas& c)
{
    ui::widgets::toolbar(c, title());

    const core::Rect bar = month_bar_rect();
    c.hline(bar.x, bar.y, bar.w);
    c.hline(bar.x, static_cast<int16_t>(bar.bottom() - 1), bar.w);

    const uint16_t btn_w = static_cast<uint16_t>(Theme::kScreenW / 4);
    const core::Rect prev_btn{bar.x, bar.y, btn_w, bar.h};
    const core::Rect next_btn{static_cast<int16_t>(bar.right() - btn_w), bar.y, btn_w, bar.h};
    draw_nav_btn(c, prev_btn, gfx::icon::kFaChevronLeft);
    draw_nav_btn(c, next_btn, gfx::icon::kFaChevronRight);

    char title_buf[20];
    (void)std::snprintf(title_buf, sizeof(title_buf), "%u\u5E74%u\u6708",
                        static_cast<unsigned>(view_year_),
                        static_cast<unsigned>(view_month_));
    gfx::Canvas::TextStyle title_ts{};
    title_ts.size_px = Theme::kFontTitle;
    title_ts.h       = gfx::HAlign::Center;
    title_ts.v       = gfx::VAlign::Middle;
    c.text_in(month_title_rect(), title_buf, title_ts);

    if (picker_open_) { paint_picker(c); return; }

    const core::Rect wd_row = weekday_row_rect();
    static const char* const kWd[] = {
        "\u65E5", "\u4E00", "\u4E8C", "\u4E09",
        "\u56DB", "\u4E94", "\u516D"};
    const uint16_t wd_cw = static_cast<uint16_t>(Theme::kScreenW / kGridCols);
    gfx::Canvas::TextStyle wd_ts{};
    wd_ts.size_px = Theme::kFontSmall;
    wd_ts.h       = gfx::HAlign::Center;
    wd_ts.v       = gfx::VAlign::Middle;
    for (uint8_t col = 0; col < kGridCols; ++col)
    {
        const int16_t x = static_cast<int16_t>(col * wd_cw);
        c.text_in(core::Rect{x, wd_row.y, wd_cw, wd_row.h}, kWd[col], wd_ts);
    }
    c.hline(0, static_cast<int16_t>(wd_row.bottom() - 1), Theme::kScreenW);

    for (uint8_t row = 0; row < kGridRows; ++row)
    {
        for (uint8_t col = 0; col < kGridCols; ++col)
        {
            const uint8_t idx = static_cast<uint8_t>(row * kGridCols + col);
            const DayCell& dc = cells_[idx];
            if (dc.day == 0) continue;

            const core::Rect cell = cell_rect(row, col);
            const bool selected =
                (dc.year == sel_year_ && dc.month == sel_month_ && dc.day == sel_day_);

            if (selected)            c.fill(cell, gfx::Ink::Black);
            else if (!dc.in_month)   fill_dim_cell(c, cell);
            else                     c.rect(cell, 1);

            if (dc.is_today && !selected)
            {
                const int16_t cx = static_cast<int16_t>(cell.x + cell.w / 2);
                const int16_t cy = static_cast<int16_t>(cell.y + 2);
                c.vline(cx, cy, 4);
            }

            draw_day_num(c, cell, dc.day, !dc.in_month,
                         selected ? gfx::Ink::White : gfx::Ink::Black);
        }
    }
}

shell::InputResult CalendarApp::on_input(const ::app::ebook::input::Event& ev)
{
    using ::app::ebook::input::EventType;
    if (ev.type != EventType::Tap) return {};

    const int16_t x = static_cast<int16_t>(ev.x);
    const int16_t y = static_cast<int16_t>(ev.y);

    if (ui::widgets::hit_toolbar_back(x, y)) return {};
    if (picker_open_) return handle_picker_input(x, y);

    const core::Rect bar = month_bar_rect();
    const uint16_t btn_w = static_cast<uint16_t>(Theme::kScreenW / 4);
    const core::Rect prev_btn{bar.x, bar.y, btn_w, bar.h};
    const core::Rect next_btn{static_cast<int16_t>(bar.right() - btn_w), bar.y, btn_w, bar.h};

    if (prev_btn.contains(x, y)) { shift_month(-1); return {true}; }
    if (next_btn.contains(x, y)) { shift_month( 1); return {true}; }

    const core::Rect title_hit = month_title_rect();
    if (title_hit.contains(x, y)) { open_picker(); return {true}; }

    for (uint8_t row = 0; row < kGridRows; ++row)
    {
        for (uint8_t col = 0; col < kGridCols; ++col)
        {
            const core::Rect cell = cell_rect(row, col);
            if (!cell.contains(x, y))
                continue;
            select_cell(static_cast<uint8_t>(row * kGridCols + col));
            return {true};
        }
    }

    return {};
}

} // namespace app::ebook::apps::calendar
