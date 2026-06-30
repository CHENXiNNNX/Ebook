#pragma once

#include <cstdint>

#include "apps/app.hpp"
#include "apps/clock/clock_store.hpp"
#include "ui/list_view.hpp"
#include "ui/ui_event.hpp"

namespace app::ebook::apps::clock {

/**
 * @brief 时钟：Clock / Alarm 标签；分钟 tick 与 ClockAlarm 经 on_ui_event
 */
class ClockApp : public App
{
  public:
    static ClockApp& instance();

    AppId       id()      const override { return AppId::Clock; }
    const char* title()   const override;
    uint32_t    icon_cp() const override;

    void on_enter() override;
    void on_exit()  override;

    void paint(gfx::Canvas& canvas) override;
    shell::InputResult on_input(const ::app::ebook::input::Event& ev) override;
    void on_ui_event(const ui::UiEvent& ev) override;

  private:
    ClockApp() = default;

    enum class Tab     : uint8_t { Clock = 0, Alarm };
    enum class SubPage : uint8_t { Main  = 0, AlarmEdit };

    void set_tab(Tab t);
    void cancel_alarm_edit();
    void invalidate_body();

    void open_hour_keyboard();
    void open_minute_keyboard();
    static void on_hour_kb_done(const char* text, void* user);
    static void on_minute_kb_done(const char* text, void* user);

    void paint_tabs(gfx::Canvas& c);
    void paint_clock_face(gfx::Canvas& c);
    void paint_alarm_list(gfx::Canvas& c);
    void paint_alarm_edit(gfx::Canvas& c);

    shell::InputResult handle_tabs(int16_t x, int16_t y);
    shell::InputResult handle_alarm_list(int16_t x, int16_t y);
    shell::InputResult handle_alarm_edit(int16_t x, int16_t y);

    static core::Rect tabs_rect();
    static core::Rect content_rect();
    static core::Rect alarm_add_btn_rect();

    Tab     tab_{Tab::Clock};
    SubPage sub_{SubPage::Main};
    uint8_t edit_alarm_idx_{0};
    bool    edit_is_new_{false};
    AlarmEntry edit_snapshot_{};

    ui::ListView alarm_list_{};
};

} // namespace app::ebook::apps::clock
