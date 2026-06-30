#pragma once

#include <cstdint>

#include "apps/app.hpp"

namespace app::ebook::apps::calendar {

/** @brief 日历：月视图 + 年月 Picker */
class CalendarApp : public App
{
  public:
    static CalendarApp& instance();

    AppId       id()      const override { return AppId::Calendar; }
    const char* title()   const override;
    uint32_t    icon_cp() const override;

    void on_enter() override;
    void on_exit()  override;
    void paint(gfx::Canvas& canvas) override;
    shell::InputResult on_input(const ::app::ebook::input::Event& ev) override;

  private:
    CalendarApp() = default;

    struct DayCell
    {
        uint16_t year{0};
        uint8_t  month{0};
        uint8_t  day{0};
        bool     in_month{false};
        bool     is_today{false};
    };

    static constexpr uint8_t kGridCols = 7;
    static constexpr uint8_t kGridRows = 6;

    void sync_to_clock();
    void shift_month(int delta);
    void rebuild_grid();
    void select_cell(uint8_t index);

    void open_picker();
    void close_picker();
    void apply_picker();
    void paint_picker(gfx::Canvas& c);
    shell::InputResult handle_picker_input(int16_t x, int16_t y);

    static uint8_t  days_in_month(uint16_t year, uint8_t month);
    static uint8_t  weekday_sun0(uint16_t year, uint8_t month, uint8_t day);

    static core::Rect month_bar_rect();
    static core::Rect month_title_rect();
    static core::Rect weekday_row_rect();
    static core::Rect grid_region_rect();
    static core::Rect cell_rect(uint8_t row, uint8_t col);
    static core::Rect picker_panel_rect();

    uint16_t view_year_{2026};
    uint8_t  view_month_{1};
    uint16_t sel_year_{0};
    uint8_t  sel_month_{0};
    uint8_t  sel_day_{0};
    DayCell  cells_[kGridCols * kGridRows]{};

    bool     picker_open_{false};
    uint16_t picker_year_{2026};
    uint8_t  picker_month_{1};
};

} // namespace app::ebook::apps::calendar
