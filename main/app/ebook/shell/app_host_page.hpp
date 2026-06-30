#pragma once

#include "shell/page.hpp"

namespace app::ebook::shell {

/** App 宿主：转发 paint/input 到 AppRegistry::active() */
class AppHostPage : public Page
{
  public:
    static AppHostPage& instance();
    void paint(gfx::Canvas& canvas) override;
    InputResult on_input(const ::app::ebook::input::Event& ev) override;

  private:
    AppHostPage() = default;
};

} // namespace app::ebook::shell
