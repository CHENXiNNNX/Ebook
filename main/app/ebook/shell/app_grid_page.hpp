#pragma once

#include <cstdint>

#include "shell/page.hpp"

namespace app::ebook::shell {

/** 应用宫格：注册表列表 + 行滚动 */
class AppGridPage : public Page
{
  public:
    static AppGridPage& instance();
    void on_enter() override;
    void paint(gfx::Canvas& canvas) override;
    InputResult on_input(const ::app::ebook::input::Event& ev) override;

  private:
    AppGridPage() = default;

    uint8_t scroll_row_{0};
};

} // namespace app::ebook::shell
