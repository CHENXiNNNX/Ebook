#pragma once

#include <cstdint>

#include "core/geometry.hpp"
#include "gfx/canvas.hpp"

namespace app::ebook::overlays {

/** @brief 全局状态栏（时间 + 电量） */
class StatusBar
{
  public:
    static StatusBar& instance();

    core::Rect bounds() const;
    bool visible() const;
    void paint(gfx::Canvas& canvas);

    void on_tick_clock(uint8_t h, uint8_t m);
    void on_tick_battery(uint8_t pct);
    void on_ntp_sync_done(uint8_t status);

  private:
    StatusBar() = default;

    uint8_t hour_{0};
    uint8_t minute_{0};
    uint8_t battery_pct_{0};
};

} // namespace app::ebook::overlays
