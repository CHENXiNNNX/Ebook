#pragma once

#include <cstdint>
#include <functional>

#include "core/geometry.hpp"
#include "gfx/canvas.hpp"
#include "input/input_event.hpp"
#include "ui/widgets.hpp"

namespace app::ebook::ui {

/** @brief 虚拟列表：按视口渲染，整行 swipe 滚动 */
class ListView
{
  public:
    using RowProvider = std::function<void(uint8_t index, widgets::RowStyle& out)>;
    using TapHandler  = std::function<void(uint8_t index)>;

    void set_provider(RowProvider p) { provider_ = std::move(p); }
    void set_tap_handler(TapHandler h) { tap_ = std::move(h); }
    void set_total(uint8_t total);

    void set_area(const core::Rect& a) { area_ = a; }
    void set_scroll(uint8_t s);

    uint8_t    total()  const { return total_; }
    uint8_t    scroll() const { return scroll_; }
    core::Rect area()   const { return area_; }

    void paint(gfx::Canvas& c);

    struct InputOutcome
    {
        bool consumed{false};
        bool scroll_changed{false};
        bool tap_consumed{false};
    };
    InputOutcome handle_input(const ::app::ebook::input::Event& ev);

  private:
    RowProvider provider_;
    TapHandler  tap_;
    uint8_t     total_{0};
    uint8_t     scroll_{0};
    core::Rect  area_{};
};

} // namespace app::ebook::ui
