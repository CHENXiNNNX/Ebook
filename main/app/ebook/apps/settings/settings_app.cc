#include "apps/settings/settings_app.hpp"

#include "apps/settings/settings_internal.hpp"
#include "gfx/icon.hpp"
#include "input/key_bindings.hpp"
#include "ui/strings.hpp"
#include "ui/theme.hpp"
#include "ui/ui_event.hpp"
#include "ui/widgets.hpp"

namespace app::ebook::apps::settings {

namespace {

using ui::Theme;
using detail::page_title;

constexpr uint8_t kMenuItems = 11;

const char* const kMenuLabels[kMenuItems] = {
    ui::strings::kSetWifi, ui::strings::kSetBt, ui::strings::kSetHotspot,
    ui::strings::kSetDisplay, ui::strings::kSetKeys, ui::strings::kSetBattery,
    ui::strings::kSetSound,
    ui::strings::kSetTime,
    ui::strings::kSetStorage, ui::strings::kSetSecurity,
    ui::strings::kSetAbout,
};
constexpr uint32_t kMenuIcons[kMenuItems] = {
    gfx::icon::kFaWifi, gfx::icon::kEbBluetooth, gfx::icon::kFaHotspot,
    gfx::icon::kFaTv, gfx::icon::kFaKeyboard, gfx::icon::kFaBatteryFull,
    gfx::icon::kFaVolumeUp,
    gfx::icon::kFaClock,
    gfx::icon::kFaHdd, gfx::icon::kFaShield,
    gfx::icon::kFaInfoCircle,
};
constexpr SettingsPage kPageOfMenuRow[kMenuItems] = {
    SettingsPage::Wifi, SettingsPage::Bluetooth, SettingsPage::Hotspot,
    SettingsPage::Display, SettingsPage::Keys, SettingsPage::Battery,
    SettingsPage::Sound,
    SettingsPage::Time,
    SettingsPage::Storage, SettingsPage::Security,
    SettingsPage::About,
};

} // namespace

SettingsApp& SettingsApp::instance()
{
    static SettingsApp s;
    return s;
}

SettingsApp::SettingsApp()
{
    menu_list_.set_total(kMenuItems);
    menu_list_.set_provider([](uint8_t i, ui::widgets::RowStyle& s) {
        s.label        = (i < kMenuItems) ? kMenuLabels[i] : "";
        s.icon_cp      = (i < kMenuItems) ? kMenuIcons[i] : 0U;
        s.show_chevron = true;
    });
    menu_list_.set_tap_handler([this](uint8_t i) {
        if (i < kMenuItems)
            enter_page(kPageOfMenuRow[i]);
    });

    load_time_prefs();
    apply_tz_from_index();
}

const char* SettingsApp::title() const
{
    return ui::strings::kAppSettings;
}

uint32_t SettingsApp::icon_cp() const
{
    return gfx::icon::kFaCog;
}

void SettingsApp::on_enter()
{
    page_ = SettingsPage::Menu;
    menu_list_.set_scroll(0);
    load_hotspot_prefs();
}

void SettingsApp::on_exit()
{
    exit_current_page();
    page_ = SettingsPage::Menu;
}

void SettingsApp::enter_page(SettingsPage p)
{
    exit_current_page();
    page_ = p;
    switch (p)
    {
        case SettingsPage::Wifi:
            wifi_enter();
            break;
        case SettingsPage::Bluetooth:
            bt_enter();
            break;
        case SettingsPage::Time:
            apply_tz_from_index();
            break;
        case SettingsPage::About:
            about_scroll_row_ = 0;
            break;
        default:
            break;
    }
    request_repaint();
}

void SettingsApp::exit_current_page()
{
    if (page_ == SettingsPage::Wifi)
        wifi_leave();
    else if (page_ == SettingsPage::Bluetooth)
        bt_leave();
    else if (page_ == SettingsPage::Security)
    {
        sec_pin_purpose_    = SecPinPurpose::None;
        sec_pending_pin_[0] = '\0';
    }
}

bool SettingsApp::back_to_menu()
{
    if (page_ == SettingsPage::Menu)
        return false;
    if (page_ == SettingsPage::KeysEdit)
    {
        page_ = SettingsPage::Keys;
        request_repaint();
        return true;
    }
    exit_current_page();
    page_ = SettingsPage::Menu;
    request_repaint();
    return true;
}

bool SettingsApp::on_semantic_back()
{
    return back_to_menu();
}

void SettingsApp::paint(gfx::Canvas& c)
{
    const char* title = page_title(page_);
    if (page_ == SettingsPage::KeysEdit)
        title = ::app::ebook::input::KeyBindings::context_label(keys_edit_ctx_);
    ui::widgets::toolbar(c, title);
    switch (page_)
    {
        case SettingsPage::Menu:
            paint_menu(c);
            break;
        case SettingsPage::Wifi:
            paint_wifi(c);
            break;
        case SettingsPage::Bluetooth:
            paint_bluetooth(c);
            break;
        case SettingsPage::Hotspot:
            paint_hotspot(c);
            break;
        case SettingsPage::Display:
            paint_display(c);
            break;
        case SettingsPage::Keys:
            paint_keys(c);
            break;
        case SettingsPage::KeysEdit:
            paint_keys_edit(c);
            break;
        case SettingsPage::Battery:
            paint_placeholder(c);
            break;
        case SettingsPage::Sound:
            paint_sound(c);
            break;
        case SettingsPage::Time:
            paint_time(c);
            break;
        case SettingsPage::Storage:
            paint_storage(c);
            break;
        case SettingsPage::Security:
            paint_security(c);
            break;
        case SettingsPage::About:
            paint_about(c);
            break;
        default:
            paint_placeholder(c);
            break;
    }
}

void SettingsApp::paint_menu(gfx::Canvas& c)
{
    menu_list_.paint(c);
}

void SettingsApp::paint_placeholder(gfx::Canvas& c)
{
    const core::Rect box{0, static_cast<int16_t>(Theme::kListStartY + 20),
                         Theme::kScreenW, Theme::kFontBody};
    gfx::Canvas::TextStyle s{};
    s.size_px = Theme::kFontBody;
    s.h       = gfx::HAlign::Center;
    c.text_in(box, ui::strings::kWip, s);
}

shell::InputResult SettingsApp::on_input(const ::app::ebook::input::Event& ev)
{
    using ::app::ebook::input::EventType;

    if (ev.type == EventType::Tap && page_ != SettingsPage::Menu &&
        ui::widgets::hit_toolbar_back(static_cast<int16_t>(ev.x), static_cast<int16_t>(ev.y)))
    {
        if (back_to_menu())
            return {true};
    }

    switch (page_)
    {
        case SettingsPage::Menu:
            return handle_menu(ev);
        case SettingsPage::Wifi:
            return handle_wifi(ev);
        case SettingsPage::Hotspot:
            return handle_hotspot(ev);
        case SettingsPage::Bluetooth:
            return handle_bluetooth(ev);
        case SettingsPage::Display:
            return handle_display(ev);
        case SettingsPage::Keys:
            return handle_keys(ev);
        case SettingsPage::KeysEdit:
            return handle_keys_edit(ev);
        case SettingsPage::Sound:
            return handle_sound(ev);
        case SettingsPage::Time:
            return handle_time(ev);
        case SettingsPage::Security:
            return handle_security(ev);
        case SettingsPage::About:
            return handle_about(ev);
        default:
            return {};
    }
}

shell::InputResult SettingsApp::handle_menu(const ::app::ebook::input::Event& ev)
{
    const auto out = menu_list_.handle_input(ev);
    if (out.scroll_changed)
        request_repaint();
    return {out.consumed};
}

void SettingsApp::on_ui_event(const ui::UiEvent& ev)
{
    switch (ev.kind)
    {
        case ui::UiEventKind::WifiState:
            on_wifi_state(ev.payload.wifi.state, ev.payload.wifi.fail);
            break;
        case ui::UiEventKind::WifiScanDone:
            on_wifi_scan_done();
            break;
        case ui::UiEventKind::NtpSyncDone:
            on_ntp_sync_done(static_cast<uint8_t>(ev.payload.system.value));
            break;
        default:
            break;
    }
}

} // namespace app::ebook::apps::settings
