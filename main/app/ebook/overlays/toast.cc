#include "overlays/toast.hpp"

#include <cstring>

#include "ui/theme.hpp"
#include "ui/ui_bus.hpp"

namespace app::ebook::overlays {

Toast& Toast::instance()
{
    static Toast s;
    return s;
}

void Toast::timer_cb(TimerHandle_t t)
{
    auto* self = static_cast<Toast*>(pvTimerGetTimerID(t));
    (void)self;
    (void)ui::UiBus::get_instance().post_system_hint(ui::SystemHintKind::ToastExpire, 0);
}

void Toast::ensure_timer(uint32_t duration_ms)
{
    if (duration_ms == 0)
        duration_ms = 1;
    if (timer_ == nullptr)
    {
        timer_ = xTimerCreate("toast", pdMS_TO_TICKS(duration_ms), pdFALSE, this, &Toast::timer_cb);
        return;
    }
    (void)xTimerChangePeriod(timer_, pdMS_TO_TICKS(duration_ms), 0);
}

void Toast::show(const char* text, uint16_t duration_ms)
{
    if (text == nullptr)
        return;
    if (duration_ms == 0)
        duration_ms = 1500;

    (void)std::strncpy(text_, text, kMaxLen);
    text_[kMaxLen] = '\0';
    visible_       = true;

    ensure_timer(duration_ms);
    (void)xTimerReset(timer_, 0);
}

void Toast::hide()
{
    if (!visible_)
        return;
    visible_ = false;
    text_[0] = '\0';
    if (timer_ != nullptr)
        (void)xTimerStop(timer_, 0);
}

void Toast::paint(gfx::Canvas& c)
{
    if (!visible_ || text_[0] == '\0')
        return;

    const core::Rect box{
        ui::Theme::kPad,
        static_cast<int16_t>(ui::Theme::kScreenH - 36),
        static_cast<uint16_t>(ui::Theme::kScreenW - ui::Theme::kPad * 2),
        28};
    c.fill(box, gfx::Ink::Black);
    c.text(static_cast<int16_t>(box.x + 4), static_cast<int16_t>(box.y + 6),
           text_, ui::Theme::kFontSmall, gfx::FontFace::Text, gfx::Ink::White);
}

} // namespace app::ebook::overlays
