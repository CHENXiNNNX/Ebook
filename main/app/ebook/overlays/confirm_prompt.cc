#include "overlays/confirm_prompt.hpp"

#include <cstring>

#include "router/refresh_intent.hpp"
#include "router/router.hpp"
#include "ui/strings.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

namespace app::ebook::overlays {

namespace {

using ui::Theme;

constexpr uint16_t kDlgW   = 160;
constexpr uint16_t kDlgH   = 88;
constexpr uint16_t kBtnH   = 28;

core::Rect dialog_rect()
{
    const int16_t x = static_cast<int16_t>((Theme::kScreenW - kDlgW) / 2);
    const int16_t y = static_cast<int16_t>((Theme::kScreenH - kDlgH) / 2);
    return core::Rect{x, y, kDlgW, kDlgH};
}

core::Rect yes_rect()
{
    const core::Rect dlg = dialog_rect();
    const uint16_t   half = static_cast<uint16_t>(dlg.w / 2);
    return core::Rect{
        dlg.x, static_cast<int16_t>(dlg.bottom() - kBtnH), half, kBtnH};
}

core::Rect no_rect()
{
    const core::Rect dlg = dialog_rect();
    const uint16_t   half = static_cast<uint16_t>(dlg.w / 2);
    return core::Rect{
        static_cast<int16_t>(dlg.x + half),
        static_cast<int16_t>(dlg.bottom() - kBtnH), half, kBtnH};
}

void repaint()
{
    (void)router::Router::instance().repaint(router::intent_partial_full());
}

} // namespace

ConfirmPrompt& ConfirmPrompt::instance()
{
    static ConfirmPrompt s;
    return s;
}

void ConfirmPrompt::show(const char* message, ChoiceHandler handler, void* user)
{
    if (message == nullptr || handler == nullptr)
        return;

    (void)std::strncpy(message_, message, kMsgMax);
    message_[kMsgMax] = '\0';
    handler_            = handler;
    user_               = user;
    open_               = true;
    repaint();
}

void ConfirmPrompt::close()
{
    if (!open_)
        return;
    open_     = false;
    handler_  = nullptr;
    user_     = nullptr;
    message_[0] = '\0';
    repaint();
}

void ConfirmPrompt::paint(gfx::Canvas& c)
{
    if (!open_ || message_[0] == '\0')
        return;

    c.fill(core::Rect::full(), gfx::Ink::White);
    c.rect(core::Rect::full());

    const core::Rect dlg = dialog_rect();
    c.rect(dlg, 2);

    const core::Rect body{
        static_cast<int16_t>(dlg.x + Theme::kPad),
        static_cast<int16_t>(dlg.y + Theme::kPad),
        static_cast<uint16_t>(dlg.w - Theme::kPad * 2),
        static_cast<uint16_t>(dlg.h - kBtnH - Theme::kPad * 2)};
    gfx::Canvas::TextStyle ts{};
    ts.size_px = Theme::kFontSmall;
    ts.h       = gfx::HAlign::Center;
    ts.v       = gfx::VAlign::Middle;
    c.text_in(body, message_, ts);

    const core::Rect yes = yes_rect();
    const core::Rect no  = no_rect();
    c.hline(yes.x, yes.y, yes.w + no.w);
    c.vline(static_cast<int16_t>(yes.right()), yes.y, kBtnH);
    c.text_in(yes, ui::strings::kDrawYes, ts);
    c.text_in(no, ui::strings::kDrawNo, ts);
}

shell::InputResult ConfirmPrompt::on_input(const ::app::ebook::input::Event& ev)
{
    using ::app::ebook::input::EventType;
    if (!open_ || ev.type != EventType::Tap)
        return {true};

    const int16_t x = static_cast<int16_t>(ev.x);
    const int16_t y = static_cast<int16_t>(ev.y);

    ChoiceHandler handler = handler_;
    void*         user    = user_;

    if (yes_rect().contains(x, y))
    {
        close();
        if (handler)
            handler(true, user);
        return {true};
    }
    if (no_rect().contains(x, y))
    {
        close();
        if (handler)
            handler(false, user);
        return {true};
    }

    return {true};
}

} // namespace app::ebook::overlays
