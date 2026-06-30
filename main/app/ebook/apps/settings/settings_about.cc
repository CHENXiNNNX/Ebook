#include "apps/settings/settings_app.hpp"

#include <cstdio>
#include <cstring>

#include <esp_mac.h>

#include "apps/settings/settings_internal.hpp"
#include "network/wifi/wifi.hpp"
#include "ota/ota.hpp"
#include "system/info/info.hpp"
#include "ui/strings.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

namespace app::ebook::apps::settings {

namespace {

using ui::Theme;
using detail::scroll_rows;

} // namespace

void SettingsApp::paint_about(gfx::Canvas& c)
{
    char fw[24] = {};
    std::strncpy(fw,
                 ::app::common::ota::OtaMgr::get_instance().get_current_version().c_str(),
                 sizeof(fw) - 1);

    char cpu[16] = {};
    (void)std::snprintf(cpu, sizeof(cpu), "%u MHz",
                        static_cast<unsigned>(
                            ::app::sys::info::CpuInfo::get_cpu_info().get_cpu_freq() / 1000000U));

    char mac[20] = "-";
    uint8_t m[6] = {};
    if (esp_read_mac(m, ESP_MAC_WIFI_STA) == ESP_OK)
    {
        (void)std::snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2],
                            m[3], m[4], m[5]);
    }

    char ip[20];
    const auto& info = ::app::network::wifi::WiFiMgr::get_instance().get_info();
    if (info.state == ::app::network::wifi::State::CONNECTED &&
        (info.ip[0] | info.ip[1] | info.ip[2] | info.ip[3]) != 0)
    {
        (void)std::snprintf(ip, sizeof(ip), "%u.%u.%u.%u", info.ip[0], info.ip[1], info.ip[2],
                            info.ip[3]);
    }
    else
    {
        std::strncpy(ip, ui::strings::kIpNone, sizeof(ip) - 1);
        ip[sizeof(ip) - 1] = '\0';
    }

    const auto mi = ::app::sys::info::MemoryInfo::get_memory_info();
    char       sram[24];
    char       psram[24];
    (void)std::snprintf(sram, sizeof(sram), "%u/%u KB",
                        static_cast<unsigned>(mi.get_sram_free() / 1024),
                        static_cast<unsigned>(mi.get_sram_total() / 1024));
    (void)std::snprintf(psram, sizeof(psram), "%u/%u KB",
                        static_cast<unsigned>(mi.get_psram_free() / 1024),
                        static_cast<unsigned>(mi.get_psram_total() / 1024));

    struct RowDef
    {
        const char* lbl;
        const char* val;
    };
    const RowDef rows[] = {
        {ui::strings::kAboutDeviceName, ui::strings::kSetDeviceModel},
        {ui::strings::kSetCpu, cpu},
        {ui::strings::kAboutMac, mac},
        {ui::strings::kAboutIp, ip},
        {ui::strings::kSetFirmware, fw},
        {ui::strings::kAboutSram, sram},
        {ui::strings::kAboutPsram, psram},
    };

    const uint8_t total   = sizeof(rows) / sizeof(rows[0]);
    const uint8_t visible = Theme::kListVisibleRows;
    const uint8_t max_off = (total > visible) ? static_cast<uint8_t>(total - visible) : 0;
    if (about_scroll_row_ > max_off)
        about_scroll_row_ = max_off;

    const bool     has_scroll = total > visible;
    const uint16_t row_w      = static_cast<uint16_t>(Theme::kScreenW - (has_scroll ? 6 : 0));

    int16_t y = Theme::kListStartY;
    const uint8_t end = static_cast<uint8_t>(about_scroll_row_ + visible);
    for (uint8_t i = about_scroll_row_; i < end && i < total; ++i)
    {
        ui::widgets::RowStyle rs{};
        rs.label        = rows[i].lbl;
        rs.value        = rows[i].val;
        rs.show_chevron = false;
        ui::widgets::list_row(c, core::Rect{0, y, row_w, Theme::kListRowH}, rs);
        y = static_cast<int16_t>(y + Theme::kListRowH);
    }

    if (has_scroll)
    {
        ui::widgets::scrollbar(c, Theme::kListStartY, Theme::kListRegionH, about_scroll_row_,
                               total, visible);
    }
}

shell::InputResult SettingsApp::handle_about(const ::app::ebook::input::Event& ev)
{
    using ::app::ebook::input::EventType;
    constexpr uint8_t kAboutTotal = 7;
    const uint8_t     visible     = Theme::kListVisibleRows;

    if (ev.start_y > Theme::kStatusBarH + Theme::kToolbarH)
    {
        if (scroll_rows(about_scroll_row_, kAboutTotal, visible, ev.type))
        {
            request_repaint();
            return {true};
        }
    }
    return {};
}

} // namespace app::ebook::apps::settings
