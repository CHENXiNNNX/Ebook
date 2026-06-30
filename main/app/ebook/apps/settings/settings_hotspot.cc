#include "apps/settings/settings_app.hpp"

#include <cstdio>

#include <esp_netif.h>

#include "apps/settings/settings_internal.hpp"
#include "core/log.hpp"
#include "data/persist.hpp"
#include "data/system_state.hpp"
#include "gfx/icon.hpp"
#include "network/wifi/wifi.hpp"
#include "overlays/keyboard.hpp"
#include "overlays/toast.hpp"
#include "protocol/file_server/file_server.hpp"
#include "ui/strings.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

namespace app::ebook::apps::settings {

namespace {

using ui::Theme;
using detail::copy_str;
using detail::TAG;
using detail::tap_row_index;

} // namespace

void SettingsApp::paint_hotspot(gfx::Canvas& c)
{
    int16_t y = Theme::kListStartY;
    auto&   sys = data::SystemState::get_instance();

    struct RowDef
    {
        const char* label;
        const char* value;
        bool        chevron;
        uint32_t    icon;
    };
    const uint32_t toggle = gfx::icon::toggle(sys.hotspot());
    const RowDef rows[]   = {
        {ui::strings::kSetHotspotSwitch, nullptr, false, toggle},
        {ui::strings::kSetHotspotName, hotspot_name_, true, 0},
        {ui::strings::kSetHotspotPwd, hotspot_pwd_, true, 0},
    };
    for (const RowDef& r : rows)
    {
        ui::widgets::RowStyle rs{};
        rs.label        = r.label;
        rs.value        = r.value;
        rs.show_chevron = r.chevron;
        if (r.icon != 0)
        {
            rs.value_icon      = r.icon;
            rs.value_icon_size = 20;
        }
        ui::widgets::list_row(c, core::Rect{0, y, Theme::kScreenW, Theme::kListRowH}, rs);
        y = static_cast<int16_t>(y + Theme::kListRowH);
    }

    char cnt[8];
    const uint8_t n = sys.hotspot()
                          ? ::app::network::wifi::WiFiMgr::get_instance().get_ap_sta_count()
                          : 0;
    (void)std::snprintf(cnt, sizeof(cnt), ui::strings::kSetConnCountFmt,
                        static_cast<unsigned>(n));
    ui::widgets::RowStyle rs{};
    rs.label        = ui::strings::kSetHotspotClients;
    rs.value        = cnt;
    rs.show_chevron = false;
    ui::widgets::list_row(c, core::Rect{0, y, Theme::kScreenW, Theme::kListRowH}, rs);
}

shell::InputResult SettingsApp::handle_hotspot(const ::app::ebook::input::Event& ev)
{
    using ::app::ebook::input::EventType;
    if (ev.type != EventType::Tap)
        return {};

    const uint8_t row = tap_row_index(static_cast<int16_t>(ev.y));
    if (row == 0xFF)
        return {};

    auto& sys = data::SystemState::get_instance();

    if (row == 0)
    {
        sys.set_hotspot(!sys.hotspot());
        hotspot_apply_state();
        request_repaint();
        return {true};
    }

    overlays::KeyboardConfig kc{};
    kc.default_layer = overlays::KeyboardLayer::Letters;
    kc.max_len       = 32;

    if (row == 1)
    {
        kc.initial_text = hotspot_name_;
        kc.title        = ui::strings::kSetHotspotName;
        overlays::Keyboard::instance().open(kc, on_hotspot_name_done, this);
        return {true};
    }
    if (row == 2)
    {
        kc.initial_text = hotspot_pwd_;
        kc.title        = ui::strings::kSetHotspotPwd;
        overlays::Keyboard::instance().open(kc, on_hotspot_pwd_done, this);
        return {true};
    }
    return {};
}

void SettingsApp::hotspot_apply_state()
{
    auto& mgr = ::app::network::wifi::WiFiMgr::get_instance();
    const bool on = data::SystemState::get_instance().hotspot();
    if (on)
    {
        if (!mgr.init())
        {
            EBOOK_LOGW(TAG, "wifi init failed; cannot enable hotspot");
            return;
        }
        if (!mgr.enable_ap(hotspot_name_, hotspot_pwd_))
        {
            EBOOK_LOGW(TAG, "enable_ap failed");
            data::SystemState::get_instance().set_hotspot(false);
            overlays::Toast::instance().show(ui::strings::kWifiFailed, 2000);
            return;
        }
        if (::app::protocol::file_server::start() == ESP_OK)
        {
            char ip[16] = "192.168.4.1";
            esp_netif_t* ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
            esp_netif_ip_info_t info{};
            if (ap != nullptr && esp_netif_get_ip_info(ap, &info) == ESP_OK && info.ip.addr != 0)
            {
                (void)std::snprintf(ip, sizeof(ip), IPSTR, IP2STR(&info.ip));
            }
            overlays::Toast::instance().show(ip, 3000);
        }
    }
    else
    {
        ::app::protocol::file_server::stop();
        mgr.disable_ap();
    }
}

void SettingsApp::load_hotspot_prefs()
{
    char buf[33] = {};
    if (data::Persist::get_str("hs_name", buf, sizeof(buf)) && buf[0] != '\0')
        copy_str(hotspot_name_, sizeof(hotspot_name_), buf);
    buf[0] = '\0';
    if (data::Persist::get_str("hs_pwd", buf, sizeof(buf)) && buf[0] != '\0')
        copy_str(hotspot_pwd_, sizeof(hotspot_pwd_), buf);
}

void SettingsApp::save_hotspot_prefs()
{
    data::Persist::set_str("hs_name", hotspot_name_);
    data::Persist::set_str("hs_pwd", hotspot_pwd_);
    data::Persist::commit();
}

void SettingsApp::on_hotspot_name_done(const char* text, void* user)
{
    auto* self = static_cast<SettingsApp*>(user);
    if (self == nullptr)
        return;
    copy_str(self->hotspot_name_, sizeof(self->hotspot_name_), text);
    self->save_hotspot_prefs();
    if (data::SystemState::get_instance().hotspot())
        self->hotspot_apply_state();
    self->request_repaint();
}

void SettingsApp::on_hotspot_pwd_done(const char* text, void* user)
{
    auto* self = static_cast<SettingsApp*>(user);
    if (self == nullptr)
        return;
    copy_str(self->hotspot_pwd_, sizeof(self->hotspot_pwd_), text);
    self->save_hotspot_prefs();
    if (data::SystemState::get_instance().hotspot())
        self->hotspot_apply_state();
    self->request_repaint();
}

} // namespace app::ebook::apps::settings
