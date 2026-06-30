#pragma once

#include "core/geometry.hpp"
#include "gfx/canvas.hpp"
#include "input/input_event.hpp"
#include "shell/page.hpp"

namespace app::ebook::overlays {

/**
 * @brief 控制中心（状态栏下滑）；局刷 partial_full；锁屏走 refresh_edges Fast
 */
class ControlCenter
{
  public:
    static ControlCenter& instance();

    bool is_open() const;
    void open();
    void close();

    core::Rect bounds() const;
    void paint(gfx::Canvas& canvas);
    shell::InputResult on_input(const ::app::ebook::input::Event& ev);

  private:
    ControlCenter() = default;

    core::Rect tile_rect(uint8_t idx) const;
    core::Rect level_row_rect(uint8_t idx) const;
    core::Rect level_minus_rect(uint8_t idx) const;
    core::Rect level_plus_rect(uint8_t idx) const;

    bool adjust_level(uint8_t idx, int delta);
    void repaint_panel();
};

} // namespace app::ebook::overlays
