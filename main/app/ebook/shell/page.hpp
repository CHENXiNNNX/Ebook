#pragma once

#include "gfx/canvas.hpp"
#include "input/input_event.hpp"
#include "ui/ui_event.hpp"

namespace app::ebook::shell {

struct InputResult
{
    bool consumed{false};
};

/** Shell / App 页面基类 */
class Page
{
  public:
    virtual ~Page() = default;

    virtual void on_enter() {}
    virtual void on_exit() {}

    virtual bool wants_status_bar() const { return true; }
    virtual void paint(gfx::Canvas& canvas) = 0;
    virtual void paint_overlay(gfx::Canvas& canvas) { (void)canvas; }

    virtual InputResult on_input(const ::app::ebook::input::Event& ev)
    {
        (void)ev;
        return {};
    }

    virtual void on_ui_event(const ui::UiEvent& ev)
    {
        (void)ev;
    }
};

} // namespace app::ebook::shell
