#include "shell/lock_page.hpp"

#include <cstdio>
#include <cstring>

#include "apps/settings/lock_password.hpp"
#include "data/clock_provider.hpp"
#include "data/system_state.hpp"
#include "gfx/font.hpp"
#include "gfx/text_layout.hpp"
#include "overlays/keyboard.hpp"
#include "overlays/toast.hpp"
#include "router/page_id.hpp"
#include "router/refresh_intent.hpp"
#include "router/router.hpp"
#include "ui/strings.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

namespace app::ebook::shell {

namespace {

using ui::Theme;

struct Quote
{
    const char* text;
    const char* source;
};

constexpr Quote kQuotes[] = {
    {"\u8BFB\u4E66\u662F\u5728\u522B\u4EBA\u601D\u60F3\u7684\u5E2E\u52A9\u4E0B\uFF0C\u5EFA\u7ACB\u8D77\u81EA\u5DF1\u7684\u601D\u60F3\u3002", "\u9C81\u5DF4\u91D1"},
    {"\u4E66\u7C4D\u662F\u4EBA\u7C7B\u8FDB\u6B65\u7684\u9636\u68AF\u3002", "\u9AD8\u5C14\u57FA"},
    {"\u8BFB\u4E00\u672C\u597D\u4E66\uFF0C\u5C31\u662F\u548C\u8BB8\u591A\u9AD8\u5C1A\u7684\u4EBA\u8C08\u8BDD\u3002", "\u6B4C\u5FB7"},
    {"\u9ED1\u53D1\u4E0D\u77E5\u52E4\u5B66\u65E9\uFF0C\u767D\u9996\u65B9\u6094\u8BFB\u4E66\u8FDF\u3002", "\u989C\u771F\u537F"},
    {"\u4E66\u5230\u7528\u65F6\u65B9\u6068\u5C11\u3002", "\u9646\u6E38"},
    {"\u95EE\u6E20\u90A3\u5F97\u6E05\u5982\u8BB8\uFF0C\u4E3A\u6709\u6E90\u5934\u6D3B\u6C34\u6765\u3002", "\u6731\u7199"},
    {"\u8179\u6709\u8BD7\u4E66\u6C14\u81EA\u534E\u3002", "\u82CF\u8F7C"},
    {"\u8BFB\u4E66\u7834\u4E07\u5377\uFF0C\u4E0B\u7B14\u5982\u6709\u795E\u3002", "\u675C\u752B"},
};
constexpr size_t kQuoteCount = sizeof(kQuotes) / sizeof(kQuotes[0]);

core::Rect time_box_rect()
{
    return core::Rect{0, static_cast<int16_t>(Theme::kLockTimeY),
                      Theme::kScreenW, static_cast<uint16_t>(Theme::kFontHuge + 8)};
}

void on_lock_pin_done(const char* text, void* /*user*/)
{
    if (apps::settings::LockPassword::get_instance().verify(text))
    {
        (void)router::Router::instance().navigate(router::page_shell(router::ShellPage::Home));
        return;
    }
    overlays::Toast::instance().show(ui::strings::kSetSecWrong, 2000);
}

void open_lock_pin_keyboard()
{
    overlays::KeyboardConfig kc{};
    kc.default_layer = overlays::KeyboardLayer::Numbers;
    kc.max_len       = apps::settings::LockPassword::kPinLen;
    kc.title         = ui::strings::kLockEnterPin;
    overlays::Keyboard::instance().open(kc, on_lock_pin_done, nullptr);
}

} // namespace

LockPage& LockPage::instance()
{
    static LockPage s;
    return s;
}

void LockPage::paint(gfx::Canvas& c)
{
    const auto clk = data::Clock::now();

    char date_buf[48];
    char wd_buf[16];
    clk.format_date_cn(date_buf, sizeof(date_buf));
    (void)std::snprintf(wd_buf, sizeof(wd_buf), "  %s", clk.weekday_name_cn());

    const int16_t chrome_y =
        static_cast<int16_t>((Theme::kLockChromeH - Theme::kFontSmall) / 2);

    c.text(Theme::kPad, chrome_y, date_buf, Theme::kFontSmall);
    const uint16_t dw = gfx::Font::get_instance().measure(date_buf, Theme::kFontSmall);
    c.text(static_cast<int16_t>(Theme::kPad + dw), chrome_y, wd_buf, Theme::kFontSmall);

    const uint8_t batt = data::SystemState::get_instance().battery_pct();
    ui::widgets::battery_indicator(
        c, static_cast<int16_t>(Theme::kScreenW - Theme::kPad), 0, batt);
    c.hline(0, static_cast<int16_t>(Theme::kLockChromeH - 1), Theme::kScreenW);

    char hm[8];
    clk.format_time_hm(hm, sizeof(hm));
    gfx::Canvas::TextStyle ts{};
    ts.size_px = Theme::kFontHuge;
    ts.h       = gfx::HAlign::Center;
    ts.v       = gfx::VAlign::Top;
    c.text_in(time_box_rect(), hm, ts);

    const Quote&   q  = kQuotes[clk.day_of_year % kQuoteCount];
    const uint16_t qx = Theme::kPadLg;
    const uint16_t qw = static_cast<uint16_t>(Theme::kScreenW - qx * 2);

    c.text(qx, Theme::kLockQuoteY, ui::strings::kDailyQuote, Theme::kFontSmall);

    gfx::LineSlice lines[Theme::kLockQuoteMaxLines];
    const uint8_t  n = gfx::wrap_text(q.text, Theme::kFontBody, qw,
                                       Theme::kLockQuoteMaxLines, lines);

    int16_t y = static_cast<int16_t>(Theme::kLockQuoteY + 14);
    for (uint8_t i = 0; i < n; ++i)
    {
        char buf[96];
        const size_t len = static_cast<size_t>(lines[i].end - lines[i].begin);
        const size_t cap = (len < sizeof(buf) - 1) ? len : sizeof(buf) - 1;
        std::memcpy(buf, lines[i].begin, cap);
        buf[cap] = '\0';
        c.text(qx, y, buf, Theme::kFontBody);
        y = static_cast<int16_t>(y + Theme::kLockQuoteStep);
    }

    char src_buf[48];
    (void)std::snprintf(src_buf, sizeof(src_buf), "\u2014\u2014 %s", q.source);
    c.text(qx, y, src_buf, Theme::kFontSmall);

    const core::Rect hint_box{0, static_cast<int16_t>(Theme::kLockHintY),
                              Theme::kScreenW, static_cast<uint16_t>(Theme::kFontSmall + 4)};
    gfx::Canvas::TextStyle hs{};
    hs.size_px = Theme::kFontSmall;
    hs.h       = gfx::HAlign::Center;
    c.text_in(hint_box, ui::strings::kSwipeToUnlock, hs);
}

void LockPage::on_ui_event(const ui::UiEvent& ev)
{
    if (router::Router::instance().stack().shell_top() != router::ShellPage::Lock)
        return;

    if (ev.kind == ui::UiEventKind::TickClock)
    {
        if (ev.payload.clock.hour == hour_ && ev.payload.clock.minute == minute_)
            return;
        hour_   = ev.payload.clock.hour;
        minute_ = ev.payload.clock.minute;
        router::Router::instance().repaint(router::intent_partial_full());
        return;
    }

    if (ev.kind == ui::UiEventKind::TickBattery)
    {
        if (ev.payload.battery.pct == battery_pct_)
            return;
        battery_pct_ = ev.payload.battery.pct;
        router::Router::instance().repaint(router::intent_partial_full());
    }
}

InputResult LockPage::on_input(const ::app::ebook::input::Event& ev)
{
    using ::app::ebook::input::EventType;
    if (ev.type == EventType::SwipeUp)
    {
        if (apps::settings::LockPassword::get_instance().enabled())
            open_lock_pin_keyboard();
        else
            (void)router::Router::instance().navigate(router::page_shell(router::ShellPage::Home));
        return {true};
    }
    return {};
}

} // namespace app::ebook::shell
