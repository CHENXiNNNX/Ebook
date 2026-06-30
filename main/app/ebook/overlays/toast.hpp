#pragma once

#include <cstdint>

#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>

#include "gfx/canvas.hpp"

namespace app::ebook::overlays {

/** @brief 底部短提示；xTimer 到期经 UiBus ToastExpire 自隐 */
class Toast
{
  public:
    static Toast& instance();

    void show(const char* text, uint16_t duration_ms = 1500);
    void hide();

    bool visible() const { return visible_; }
    void paint(gfx::Canvas& canvas);

  private:
    Toast() = default;

    static constexpr size_t kMaxLen = 60;

    static void timer_cb(TimerHandle_t t);
    void ensure_timer(uint32_t duration_ms);

    char          text_[kMaxLen + 1]{};
    bool          visible_{false};
    TimerHandle_t timer_{nullptr};
};

} // namespace app::ebook::overlays
