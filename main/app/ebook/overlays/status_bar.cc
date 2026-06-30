#include "overlays/status_bar.hpp"

#include <cstdio>

#include "apps/app_registry.hpp"
#include "data/clock_provider.hpp"
#include "data/system_state.hpp"
#include "protocol/ntp/ntp.hpp"
#include "router/refresh_intent.hpp"
#include "router/router.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

namespace app::ebook::overlays {

StatusBar& StatusBar::instance()
{
    static StatusBar s;
    return s;
}

core::Rect StatusBar::bounds() const
{
    return ui::Theme::status_bar_rect();
}

bool StatusBar::visible() const
{
    const auto& stack = router::Router::instance().stack();
    if (stack.shell_top() == router::ShellPage::Lock)
        return false;
    if (stack.shell_top() == router::ShellPage::AppHost)
    {
        if (apps::App* a = apps::AppRegistry::instance().active())
            return a->wants_status_bar();
        return true;
    }
    return stack.shell_top() == router::ShellPage::Home ||
           stack.shell_top() == router::ShellPage::AppGrid;
}

void StatusBar::on_tick_clock(uint8_t h, uint8_t m)
{
    if (h == hour_ && m == minute_)
        return;
    hour_   = h;
    minute_ = m;
    if (visible())
        router::Router::instance().repaint(router::intent_partial_full());
}

void StatusBar::on_tick_battery(uint8_t pct)
{
    if (pct == battery_pct_)
        return;
    battery_pct_ = pct;
    if (visible())
        router::Router::instance().repaint(router::intent_partial_full());
}

void StatusBar::on_ntp_sync_done(uint8_t status)
{
    if (static_cast<::app::protocol::ntp::SyncStatus>(status) !=
        ::app::protocol::ntp::SyncStatus::COMPLETED)
        return;

    const auto clk = data::Clock::now();
    hour_          = clk.hour;
    minute_        = clk.minute;
    if (visible())
        router::Router::instance().repaint(router::intent_partial_full());
}

void StatusBar::paint(gfx::Canvas& c)
{
    if (!visible())
        return;

    // 绘制时读系统时间，避免锁屏期间 TickClock 未更新导致显示 00:00
    const auto clk = data::Clock::now();
    hour_          = clk.hour;
    minute_        = clk.minute;

    using ui::Theme;
    char time_buf[8];
    (void)std::snprintf(time_buf, sizeof(time_buf), "%02u:%02u",
                        static_cast<unsigned>(hour_), static_cast<unsigned>(minute_));

    const int16_t y_top = static_cast<int16_t>((Theme::kStatusBarH - Theme::kFontSmall) / 2);
    c.text(Theme::kPad, y_top, time_buf, Theme::kFontSmall);

    battery_pct_ = data::SystemState::get_instance().battery_pct();
    ui::widgets::battery_indicator(
        c, static_cast<int16_t>(Theme::kScreenW - Theme::kPad), 0, battery_pct_);
    c.hline(0, static_cast<int16_t>(Theme::kStatusBarH - 1), Theme::kScreenW);
}

} // namespace app::ebook::overlays
