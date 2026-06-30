#pragma once

#include "shell/page.hpp"

namespace app::ebook::shell {

/** 主页：阅读卡片 + 快捷宫格 */
class HomePage : public Page
{
  public:
    static HomePage& instance();
    void paint(gfx::Canvas& canvas) override;
    InputResult on_input(const ::app::ebook::input::Event& ev) override;

  private:
    HomePage() = default;
};

} // namespace app::ebook::shell
