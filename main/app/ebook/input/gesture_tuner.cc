#include "input/gesture_tuner.hpp"

#include "core/geometry.hpp"

namespace app::ebook::input {

GestureConfig make_config(Profile p)
{
    GestureConfig cfg{};
    cfg.panel_width  = core::kScreenW;
    cfg.panel_height = core::kScreenH;

    if (p == Profile::Keyboard)
        cfg.gesture_cooldown_ms = 30;

    if (p == Profile::Drawing)
    {
        cfg.emit_press            = true;
        cfg.emit_move             = true;
        cfg.emit_release          = true;
        cfg.swipe_min_distance_px = kSwipeDisabledPx;
        cfg.long_press_ms         = kLongPressDisabledMs;
        cfg.gesture_cooldown_ms   = 0;
    }

    return cfg;
}

} // namespace app::ebook::input
