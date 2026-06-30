#include "apps/settings/settings_app.hpp"

#include <cstring>

#include "apps/settings/lock_password.hpp"
#include "apps/settings/settings_internal.hpp"
#include "overlays/keyboard.hpp"
#include "overlays/toast.hpp"
#include "ui/strings.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

namespace app::ebook::apps::settings {

namespace {

using ui::Theme;
using detail::copy_str;
using detail::tap_row_index;

void paint_sec_row(gfx::Canvas& c, int16_t y, const char* label, const char* value,
                   bool chevron)
{
    ui::widgets::RowStyle rs{};
    rs.label        = label;
    rs.value        = value;
    rs.show_chevron = chevron;
    ui::widgets::list_row(c, core::Rect{0, y, Theme::kScreenW, Theme::kListRowH}, rs);
}

} // namespace

void SettingsApp::paint_security(gfx::Canvas& c)
{
    const auto& lp = LockPassword::get_instance();
    int16_t     y  = Theme::kListStartY;

    if (!lp.enabled())
    {
        paint_sec_row(c, y, ui::strings::kSetSecLock, ui::strings::kSetUnset, true);
        return;
    }

    paint_sec_row(c, y, ui::strings::kSetSecLock, ui::strings::kSetPinSet, false);
    y = static_cast<int16_t>(y + Theme::kListRowH);

    paint_sec_row(c, y, ui::strings::kSetSecChange, nullptr, true);
    y = static_cast<int16_t>(y + Theme::kListRowH);

    paint_sec_row(c, y, ui::strings::kSetSecClose, nullptr, true);
}

void SettingsApp::open_pin_keyboard(const char* title)
{
    overlays::KeyboardConfig kc{};
    kc.default_layer = overlays::KeyboardLayer::Numbers;
    kc.max_len       = LockPassword::kPinLen;
    kc.title         = title;
    overlays::Keyboard::instance().open(kc, on_sec_pin_done, this);
}

void SettingsApp::begin_set_pin_flow(bool require_old)
{
    sec_pending_pin_[0] = '\0';
    if (require_old)
    {
        sec_pin_purpose_ = SecPinPurpose::VerifyForChange;
        open_pin_keyboard(ui::strings::kSetSecEnterOld);
    }
    else
    {
        sec_pin_purpose_ = SecPinPurpose::SetNew;
        open_pin_keyboard(ui::strings::kSetSecEnterNew);
    }
}

shell::InputResult SettingsApp::handle_security(const ::app::ebook::input::Event& ev)
{
    using ::app::ebook::input::EventType;
    if (ev.type != EventType::Tap)
        return {};

    const uint8_t row = tap_row_index(static_cast<int16_t>(ev.y));
    if (row == 0xFF)
        return {};

    const auto& lp = LockPassword::get_instance();

    if (!lp.enabled())
    {
        if (row == 0)
        {
            begin_set_pin_flow(false);
            return {true};
        }
        return {};
    }

    switch (row)
    {
        case 0:
            return {};
        case 1:
            begin_set_pin_flow(true);
            return {true};
        case 2:
            sec_pin_purpose_ = SecPinPurpose::VerifyForClear;
            open_pin_keyboard(ui::strings::kSetSecEnterOld);
            return {true};
        default:
            return {};
    }
}

void SettingsApp::on_sec_pin_done(const char* text, void* user)
{
    auto* self = static_cast<SettingsApp*>(user);
    if (self != nullptr)
        self->on_sec_pin_done_impl(text);
}

void SettingsApp::on_sec_pin_done_impl(const char* text)
{
    auto& toast = overlays::Toast::instance();
    auto& lp    = LockPassword::get_instance();

    if (!LockPassword::valid_pin(text))
    {
        toast.show(ui::strings::kSetSecPinInvalid, 2000);
        sec_pin_purpose_ = SecPinPurpose::None;
        return;
    }

    switch (sec_pin_purpose_)
    {
        case SecPinPurpose::SetNew:
            copy_str(sec_pending_pin_, sizeof(sec_pending_pin_), text);
            sec_pin_purpose_ = SecPinPurpose::ConfirmNew;
            open_pin_keyboard(ui::strings::kSetSecConfirm);
            break;

        case SecPinPurpose::ConfirmNew:
            if (std::strcmp(text, sec_pending_pin_) != 0)
            {
                toast.show(ui::strings::kSetSecMismatch, 2000);
                sec_pin_purpose_ = SecPinPurpose::SetNew;
                sec_pending_pin_[0] = '\0';
                open_pin_keyboard(ui::strings::kSetSecEnterNew);
                break;
            }
            {
                const bool changing = lp.enabled();
                if (lp.set_new(text))
                    toast.show(changing ? ui::strings::kSetSecChanged
                                        : ui::strings::kSetSecSaved,
                               2000);
                else
                    toast.show(ui::strings::kSetSecSaveFail, 2000);
            }
            sec_pin_purpose_ = SecPinPurpose::None;
            request_repaint();
            break;

        case SecPinPurpose::VerifyForChange:
            if (!lp.verify(text))
            {
                toast.show(ui::strings::kSetSecWrong, 2000);
                sec_pin_purpose_ = SecPinPurpose::None;
                break;
            }
            begin_set_pin_flow(false);
            break;

        case SecPinPurpose::VerifyForClear:
            if (!lp.verify(text))
            {
                toast.show(ui::strings::kSetSecWrong, 2000);
                sec_pin_purpose_ = SecPinPurpose::None;
                break;
            }
            lp.clear();
            toast.show(ui::strings::kSetSecCleared, 2000);
            sec_pin_purpose_ = SecPinPurpose::None;
            request_repaint();
            break;

        case SecPinPurpose::None:
        default:
            sec_pin_purpose_ = SecPinPurpose::None;
            break;
    }
}

} // namespace app::ebook::apps::settings
