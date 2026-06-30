#pragma once

#include "shell/page.hpp"

namespace app::ebook::shell {

/** 锁屏：时钟、金句、上滑解锁 / PIN */
class LockPage : public Page
{
  public:
    static LockPage& instance();

    bool wants_status_bar() const override { return false; }
    void paint(gfx::Canvas& canvas) override;
    InputResult on_input(const ::app::ebook::input::Event& ev) override;
    void on_ui_event(const ui::UiEvent& ev) override;

  private:
    LockPage() = default;
    uint8_t hour_{0};
    uint8_t minute_{0};
    uint8_t battery_pct_{0};
};

} // namespace app::ebook::shell
