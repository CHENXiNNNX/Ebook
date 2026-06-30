#include "apps/settings/settings_app.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

#include "apps/settings/settings_internal.hpp"
#include "data/system_state.hpp"
#include "gfx/icon.hpp"
#include "network/wifi/wifi.hpp"
#include "overlays/keyboard.hpp"
#include "overlays/toast.hpp"
#include "ui/strings.hpp"
#include "ui/theme.hpp"
#include "ui/ui_bus.hpp"
#include "ui/widgets.hpp"

namespace app::ebook::apps::settings {

namespace {

using ui::Theme;
using detail::copy_str;
using detail::ensure_wifi;
using detail::scroll_rows;
using detail::tap_row_index;

constexpr uint8_t kPendingCap = 16;

struct PendingScan
{
    SettingsApp::WifiAp items[kPendingCap]{};
    uint8_t             count{0};
    bool                ready{false};
};

PendingScan g_pending{};
std::mutex  g_pending_mtx;

} // namespace

uint8_t SettingsApp::build_wifi_rows(WifiRowDesc* out, uint8_t cap) const
{
    if (out == nullptr || cap == 0)
        return 0;

    uint8_t n = 0;
    out[n++] = {WifiRowKind::Toggle, 0};
    if (!data::SystemState::get_instance().wifi() || n >= cap)
        return n;

    for (uint8_t i = 0; i < wifi_saved_count_ && n < cap; ++i)
        out[n++] = {WifiRowKind::SavedAp, i};

    if (n >= cap)
        return n;
    out[n++] = {WifiRowKind::ScanTrigger, 0};

    if (!wifi_scanning_)
    {
        for (uint8_t i = 0; i < wifi_scan_count_ && n < cap; ++i)
            out[n++] = {WifiRowKind::ScannedAp, i};
    }
    return n;
}

void SettingsApp::paint_wifi(gfx::Canvas& c)
{
    WifiRowDesc rows[32];
    const uint8_t total   = build_wifi_rows(rows, 32);
    const uint8_t visible = Theme::kListVisibleRows;
    const uint8_t max_off = (total > visible) ? static_cast<uint8_t>(total - visible) : 0;
    if (wifi_scroll_row_ > max_off)
        wifi_scroll_row_ = max_off;

    const bool     has_scroll = total > visible;
    const uint16_t row_w      = static_cast<uint16_t>(Theme::kScreenW - (has_scroll ? 6 : 0));
    const auto&    sys        = data::SystemState::get_instance();

    int16_t y = Theme::kListStartY;
    char    val_buf[24];

    const uint8_t end = static_cast<uint8_t>(wifi_scroll_row_ + visible);
    for (uint8_t r = wifi_scroll_row_; r < end && r < total; ++r)
    {
        ui::widgets::RowStyle rs{};
        const WifiRowDesc& row = rows[r];

        switch (row.kind)
        {
            case WifiRowKind::Toggle:
                rs.label           = ui::strings::kCtlWifi;
                rs.value_icon      = gfx::icon::toggle(sys.wifi());
                rs.value_icon_size = 20;
                rs.show_chevron    = false;
                break;
            case WifiRowKind::SavedAp:
                if (row.index < wifi_saved_count_)
                {
                    rs.label            = wifi_saved_[row.index].ssid;
                    rs.show_chevron     = false;
                    rs.action_icon      = gfx::icon::kFaTimes;
                    rs.action_icon_size = 16;
                    if (wifi_connected_ssid_[0] != '\0' &&
                        std::strcmp(wifi_saved_[row.index].ssid, wifi_connected_ssid_) == 0)
                        rs.icon_cp = gfx::icon::kFaCheck;
                }
                break;
            case WifiRowKind::ScanTrigger:
                rs.label        = ui::strings::kSetWifiScan;
                rs.value        = wifi_scanning_ ? ui::strings::kSetWifiScanning
                                                 : ui::strings::kSetWifiClickScan;
                rs.show_chevron = !wifi_scanning_;
                break;
            case WifiRowKind::ScannedAp:
                if (row.index < wifi_scan_count_)
                {
                    (void)std::snprintf(val_buf, sizeof(val_buf), "%s%d",
                                        wifi_scan_[row.index].encrypted ? "* " : "  ",
                                        static_cast<int>(wifi_scan_[row.index].rssi));
                    rs.label        = wifi_scan_[row.index].ssid;
                    rs.value        = val_buf;
                    rs.show_chevron = true;
                    if (wifi_connected_ssid_[0] != '\0' &&
                        std::strcmp(wifi_scan_[row.index].ssid, wifi_connected_ssid_) == 0)
                        rs.icon_cp = gfx::icon::kFaCheck;
                }
                break;
        }
        ui::widgets::list_row(c, core::Rect{0, y, row_w, Theme::kListRowH}, rs);
        y = static_cast<int16_t>(y + Theme::kListRowH);
    }

    if (has_scroll)
    {
        ui::widgets::scrollbar(c, Theme::kListStartY, Theme::kListRegionH, wifi_scroll_row_,
                               total, visible);
    }
}

shell::InputResult SettingsApp::handle_wifi(const ::app::ebook::input::Event& ev)
{
    using ::app::ebook::input::EventType;
    auto& sys = data::SystemState::get_instance();

    WifiRowDesc rows[32];
    const uint8_t total   = build_wifi_rows(rows, 32);
    const uint8_t visible = Theme::kListVisibleRows;

    if (ev.start_y > Theme::kStatusBarH + Theme::kToolbarH)
    {
        if (scroll_rows(wifi_scroll_row_, total, visible, ev.type))
        {
            request_repaint();
            return {true};
        }
    }
    if (ev.type != EventType::Tap)
        return {};

    const int16_t x = static_cast<int16_t>(ev.x);
    const int16_t y = static_cast<int16_t>(ev.y);
    const uint8_t local = tap_row_index(y);
    if (local >= visible)
        return {};

    const uint8_t idx = static_cast<uint8_t>(wifi_scroll_row_ + local);
    if (idx >= total)
        return {};

    const bool     has_scroll = total > visible;
    const uint16_t row_w      = static_cast<uint16_t>(Theme::kScreenW - (has_scroll ? 6 : 0));
    const core::Rect row_rect{
        0, static_cast<int16_t>(Theme::kListStartY + local * Theme::kListRowH), row_w,
        Theme::kListRowH};

    const WifiRowDesc& row = rows[idx];
    switch (row.kind)
    {
        case WifiRowKind::Toggle:
            sys.set_wifi(!sys.wifi());
            if (sys.wifi())
            {
                if (!ensure_wifi())
                    sys.set_wifi(false);
                else
                    wifi_refresh_saved();
            }
            else
            {
                ::app::network::wifi::WiFiMgr::get_instance().disconnect();
            }
            request_repaint();
            return {true};

        case WifiRowKind::SavedAp:
            if (row.index >= wifi_saved_count_)
                return {};
            if (ui::widgets::hit_row_action(row_rect, x, y))
                wifi_forget(wifi_saved_[row.index].ssid);
            else
                wifi_reconnect_saved(wifi_saved_[row.index].ssid);
            request_repaint();
            return {true};

        case WifiRowKind::ScanTrigger:
            if (!wifi_scanning_)
                wifi_start_scan();
            request_repaint();
            return {true};

        case WifiRowKind::ScannedAp:
            if (row.index >= wifi_scan_count_)
                return {};
            {
                const WifiAp& ap = wifi_scan_[row.index];
                if (ap.encrypted)
                {
                    copy_str(wifi_pending_ssid_, sizeof(wifi_pending_ssid_), ap.ssid);
                    wifi_pending_idx_ = row.index;
                    overlays::KeyboardConfig kc{};
                    kc.default_layer = overlays::KeyboardLayer::Letters;
                    kc.max_len       = 63;
                    kc.title         = ui::strings::kSetWifiPwd;
                    overlays::Keyboard::instance().open(kc, on_wifi_pwd_done, this);
                }
                else
                {
                    wifi_connect_to(row.index, "");
                }
            }
            return {true};
    }
    return {};
}

void SettingsApp::on_wifi_state(uint8_t state, uint8_t fail)
{
    using NetState = ::app::network::wifi::State;
    using NetFail  = ::app::network::wifi::FailureReason;

    try_ntp_auto_sync_on_connect(state);
    if (page_ != SettingsPage::Wifi)
        return;

    const NetState st          = static_cast<NetState>(state);
    const NetFail  fl          = static_cast<NetFail>(fail);
    const uint8_t  prev_state  = wifi_state_;
    wifi_state_                = state;

    auto& toast = overlays::Toast::instance();
    char  msg[64];

    switch (st)
    {
        case NetState::CONNECTED:
        {
            const auto& info = ::app::network::wifi::WiFiMgr::get_instance().get_info();
            copy_str(wifi_connected_ssid_, sizeof(wifi_connected_ssid_), info.ssid);
            (void)std::snprintf(msg, sizeof(msg), ui::strings::kWifiConnectedFmt,
                                wifi_connected_ssid_);
            toast.show(msg, 2500);
            wifi_refresh_saved();
            break;
        }
        case NetState::FAILED:
        {
            const char* reason = ui::strings::kWifiFailed;
            switch (fl)
            {
                case NetFail::WRONG_PASSWORD:
                    reason = ui::strings::kWifiWrongPwd;
                    break;
                case NetFail::TIMEOUT:
                    reason = ui::strings::kWifiTimeout;
                    break;
                case NetFail::NETWORK_NOT_FOUND:
                    reason = ui::strings::kWifiNotFound;
                    break;
                default:
                    break;
            }
            toast.show(reason, 2500);
            wifi_connected_ssid_[0] = '\0';
            break;
        }
        case NetState::CONNECTING:
            if (prev_state != state)
                toast.show(ui::strings::kWifiConnecting, 2000);
            break;
        case NetState::DISCONNECTED:
            wifi_connected_ssid_[0] = '\0';
            break;
    }
    request_repaint();
}

void SettingsApp::on_wifi_scan_done()
{
    wifi_scanning_   = false;
    wifi_scan_count_ = 0;

    {
        std::lock_guard<std::mutex> lk(g_pending_mtx);
        const uint8_t n = (g_pending.count < kMaxScan) ? g_pending.count : kMaxScan;
        for (uint8_t i = 0; i < n; ++i)
            wifi_scan_[i] = g_pending.items[i];
        wifi_scan_count_ = n;
        std::sort(wifi_scan_, wifi_scan_ + wifi_scan_count_,
                  [](const WifiAp& a, const WifiAp& b) { return a.rssi > b.rssi; });
        g_pending.ready = false;
    }

    if (page_ == SettingsPage::Wifi)
        request_repaint();
}

void SettingsApp::wifi_enter()
{
    wifi_scanning_          = false;
    wifi_scan_count_        = 0;
    wifi_saved_count_       = 0;
    wifi_pending_idx_       = 0xFF;
    wifi_pending_ssid_[0]   = '\0';
    wifi_connected_ssid_[0] = '\0';
    wifi_state_             = 0;
    wifi_scroll_row_        = 0;

    if (!data::SystemState::get_instance().wifi())
        return;
    if (!ensure_wifi())
        return;

    const auto& info = ::app::network::wifi::WiFiMgr::get_instance().get_info();
    wifi_state_      = static_cast<uint8_t>(info.state);
    if (info.state == ::app::network::wifi::State::CONNECTED)
    {
        copy_str(wifi_connected_ssid_, sizeof(wifi_connected_ssid_), info.ssid);
        (void)::app::network::wifi::WiFiMgr::get_instance().persist_sta_creds();
    }

    wifi_refresh_saved();
}

void SettingsApp::wifi_leave()
{
    wifi_scanning_ = false;
}

void SettingsApp::wifi_start_scan()
{
    if (wifi_scanning_)
        return;

    wifi_scanning_   = true;
    wifi_scan_count_ = 0;

    auto& mgr = ::app::network::wifi::WiFiMgr::get_instance();
    if (!mgr.scan([](const std::vector<::app::network::wifi::ApInfo>& aps) {
            std::lock_guard<std::mutex> lk(g_pending_mtx);
            g_pending.count = 0;
            for (const auto& ap : aps)
            {
                if (g_pending.count >= kPendingCap)
                    break;
                if (ap.ssid[0] == '\0')
                    continue;
                SettingsApp::WifiAp& w = g_pending.items[g_pending.count++];
                std::strncpy(w.ssid, ap.ssid, sizeof(w.ssid) - 1);
                w.ssid[sizeof(w.ssid) - 1] = '\0';
                w.rssi                       = ap.rssi;
                w.encrypted                  = ap.encrypted;
            }
            g_pending.ready = true;
            (void)ui::UiBus::get_instance().post_wifi_scan_done();
        }))
    {
        wifi_scanning_ = false;
    }
}

void SettingsApp::wifi_refresh_saved()
{
    wifi_saved_count_ = 0;
    std::vector<::app::network::wifi::Credentials> creds;
    if (!::app::network::wifi::WiFiMgr::get_instance().get_creds(creds))
        return;

    for (const auto& c : creds)
    {
        if (wifi_saved_count_ >= kMaxSaved)
            break;
        if (c.ssid[0] == '\0')
            continue;
        copy_str(wifi_saved_[wifi_saved_count_].ssid, sizeof(wifi_saved_[wifi_saved_count_].ssid),
                 c.ssid);
        ++wifi_saved_count_;
    }
}

void SettingsApp::wifi_connect_to(uint8_t scan_idx, const char* password)
{
    if (scan_idx >= wifi_scan_count_)
        return;
    const WifiAp& ap = wifi_scan_[scan_idx];
    (void)::app::network::wifi::WiFiMgr::get_instance().connect(ap.ssid, password, 45000);
}

void SettingsApp::wifi_reconnect_saved(const char* ssid)
{
    if (ssid == nullptr || ssid[0] == '\0')
        return;

    auto& mgr = ::app::network::wifi::WiFiMgr::get_instance();
    std::vector<::app::network::wifi::Credentials> creds;
    if (!mgr.get_creds(creds))
        return;

    for (const auto& c : creds)
    {
        if (std::strcmp(c.ssid, ssid) == 0)
        {
            (void)mgr.connect(ssid, c.password, 45000);
            return;
        }
    }
    (void)mgr.connect(ssid, "", 45000);
}

void SettingsApp::wifi_forget(const char* ssid)
{
    if (ssid == nullptr || ssid[0] == '\0')
        return;

    auto& mgr = ::app::network::wifi::WiFiMgr::get_instance();
    (void)mgr.remove_creds(ssid);

    if (wifi_connected_ssid_[0] != '\0' && std::strcmp(ssid, wifi_connected_ssid_) == 0)
    {
        mgr.disconnect();
        wifi_connected_ssid_[0] = '\0';
    }
    wifi_refresh_saved();

    char msg[64];
    (void)std::snprintf(msg, sizeof(msg), "\u5DF2\u5FD8\u8BB0 %s", ssid);
    overlays::Toast::instance().show(msg, 2000);
}

void SettingsApp::on_wifi_pwd_done(const char* text, void* user)
{
    auto* self = static_cast<SettingsApp*>(user);
    if (self == nullptr)
        return;
    if (self->wifi_pending_idx_ >= self->wifi_scan_count_)
        return;
    self->wifi_connect_to(self->wifi_pending_idx_, text ? text : "");
    self->wifi_pending_idx_ = 0xFF;
}

} // namespace app::ebook::apps::settings
