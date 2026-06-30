#include "apps/settings/settings_app.hpp"

#include <cstdio>
#include <vector>

#include "apps/settings/settings_internal.hpp"
#include "data/clock_provider.hpp"
#include "data/persist.hpp"
#include "data/system_state.hpp"
#include "gfx/icon.hpp"
#include "network/wifi/wifi.hpp"
#include "overlays/toast.hpp"
#include "protocol/ntp/ntp.hpp"
#include "ui/strings.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

namespace app::ebook::apps::settings {

namespace {

using ui::Theme;
using detail::ensure_wifi;
using detail::kTzCount;
using detail::kTzOffsets;
using detail::tap_row_index;

} // namespace

void SettingsApp::paint_time(gfx::Canvas& c)
{
    int16_t y = Theme::kListStartY;

    char now_buf[24];
    auto clk = data::Clock::now();
    (void)std::snprintf(now_buf, sizeof(now_buf), "%04u-%02u-%02u %02u:%02u",
                        static_cast<unsigned>(clk.year), static_cast<unsigned>(clk.month),
                        static_cast<unsigned>(clk.day), static_cast<unsigned>(clk.hour),
                        static_cast<unsigned>(clk.minute));

    char tz_buf[12];
    (void)std::snprintf(tz_buf, sizeof(tz_buf), "UTC%+d", static_cast<int>(tz_offset_h_));

    struct RowDef
    {
        const char* label;
        const char* value;
        uint32_t    value_icon;
        bool        chevron;
    };
    const RowDef rows[] = {
        {ui::strings::kSetCurTime, now_buf, 0, false},
        {ui::strings::kSetTimezone, tz_buf, 0, true},
        {ui::strings::kSetAutoSync, nullptr, gfx::icon::toggle(ntp_auto_sync_), false},
        {ui::strings::kSetSyncNow, ui::strings::kSetClickSync, 0, true},
    };
    for (const RowDef& r : rows)
    {
        ui::widgets::RowStyle rs{};
        rs.label           = r.label;
        rs.value           = r.value;
        rs.value_icon      = r.value_icon;
        rs.value_icon_size = 20;
        rs.show_chevron    = r.chevron;
        ui::widgets::list_row(c, core::Rect{0, y, Theme::kScreenW, Theme::kListRowH}, rs);
        y = static_cast<int16_t>(y + Theme::kListRowH);
    }
}

shell::InputResult SettingsApp::handle_time(const ::app::ebook::input::Event& ev)
{
    using ::app::ebook::input::EventType;
    if (ev.type != EventType::Tap)
        return {};

    const uint8_t row = tap_row_index(static_cast<int16_t>(ev.y));
    if (row == 0xFF)
        return {};

    if (row == 1)
    {
        tz_idx_ = static_cast<uint8_t>((tz_idx_ + 1) % kTzCount);
        apply_tz_from_index();
        save_time_prefs();
        request_repaint();
        return {true};
    }
    if (row == 2)
    {
        ntp_auto_sync_ = !ntp_auto_sync_;
        save_time_prefs();
        if (ntp_auto_sync_)
            try_ntp_auto_sync();
        request_repaint();
        return {true};
    }
    if (row == 3)
    {
        if (!data::SystemState::get_instance().wifi() ||
            !::app::network::wifi::WiFiMgr::get_instance().is_connected())
        {
            overlays::Toast::instance().show(ui::strings::kNeedWifiFirst, 2000);
            return {true};
        }
        if (!ensure_ntp_started())
        {
            overlays::Toast::instance().show(ui::strings::kTimeSyncFail, 2500);
            return {true};
        }
        overlays::Toast::instance().show(ui::strings::kWifiConnecting, 1500);
        request_repaint();
        return {true};
    }
    return {};
}

void SettingsApp::on_ntp_sync_done(uint8_t status)
{
    using NtpStatus = ::app::protocol::ntp::SyncStatus;
    auto& toast     = overlays::Toast::instance();
    switch (static_cast<NtpStatus>(status))
    {
        case NtpStatus::COMPLETED:
            toast.show(ui::strings::kTimeSyncOk, 2000);
            break;
        case NtpStatus::FAILED:
            toast.show(ui::strings::kTimeSyncFail, 2500);
            break;
        default:
            return;
    }
    if (page_ == SettingsPage::Time)
        request_repaint();
}

bool SettingsApp::ensure_ntp_started()
{
    auto& ntp = ::app::protocol::ntp::NtpMgr::get_instance();
    if (!ntp.is_init() && !ntp.init())
        return false;
    if (ntp.is_started())
        return true;

    static const std::vector<std::string> kServers = {
        "ntp.aliyun.com", "cn.pool.ntp.org", "pool.ntp.org",
    };
    if (!ntp.configure(kServers))
        return false;
    return ntp.start();
}

void SettingsApp::try_ntp_auto_sync()
{
    if (!ntp_auto_sync_)
        return;
    if (!data::SystemState::get_instance().wifi())
        return;
    if (!::app::network::wifi::WiFiMgr::get_instance().is_connected())
        return;
    (void)ensure_ntp_started();
}

void SettingsApp::try_ntp_auto_sync_on_connect(uint8_t wifi_state)
{
    if (static_cast<::app::network::wifi::State>(wifi_state) !=
        ::app::network::wifi::State::CONNECTED)
        return;
    if (!ntp_auto_sync_)
        return;
    (void)ensure_ntp_started();
}

void SettingsApp::load_time_prefs()
{
    uint8_t v = 0;
    if (data::Persist::get_u8("tz_idx", v) && v < kTzCount)
        tz_idx_ = v;
    bool b = true;
    if (data::Persist::get_bool("ntp_auto", b))
        ntp_auto_sync_ = b;
}

void SettingsApp::save_time_prefs()
{
    data::Persist::set_u8("tz_idx", tz_idx_);
    data::Persist::set_bool("ntp_auto", ntp_auto_sync_);
    data::Persist::commit();
}

void SettingsApp::apply_tz_from_index()
{
    if (tz_idx_ >= kTzCount)
        tz_idx_ = 0;
    tz_offset_h_ = kTzOffsets[tz_idx_];
    char tz[12];
    (void)std::snprintf(tz, sizeof(tz), "CST%+d", -static_cast<int>(tz_offset_h_));
    (void)::app::protocol::ntp::NtpMgr::get_instance().set_timezone(tz);
}

} // namespace app::ebook::apps::settings
