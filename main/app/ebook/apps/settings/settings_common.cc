#include "apps/settings/settings_internal.hpp"

#include <cstdio>
#include <cstring>

#include "core/log.hpp"
#include "gfx/font.hpp"
#include "network/wifi/wifi.hpp"
#include "ui/strings.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

namespace app::ebook::apps::settings::detail {

namespace {

using ui::Theme;

} // namespace

const char* page_title(SettingsPage p)
{
    switch (p)
    {
        case SettingsPage::Menu:      return ui::strings::kAppSettings;
        case SettingsPage::Wifi:      return ui::strings::kSetWifi;
        case SettingsPage::Bluetooth: return ui::strings::kSetBt;
        case SettingsPage::Hotspot:   return ui::strings::kSetHotspot;
        case SettingsPage::Display:   return ui::strings::kSetDisplay;
        case SettingsPage::Keys:      return ui::strings::kSetKeys;
        case SettingsPage::KeysEdit:  return ui::strings::kSetKeys;
        case SettingsPage::Battery:    return ui::strings::kSetBattery;
        case SettingsPage::Sound:     return ui::strings::kSetSound;
        case SettingsPage::Time:      return ui::strings::kSetTime;
        case SettingsPage::Storage:   return ui::strings::kSetStorage;
        case SettingsPage::Security:  return ui::strings::kSetSecurity;
        case SettingsPage::About:     return ui::strings::kSetAbout;
    }
    return ui::strings::kAppSettings;
}

void copy_str(char* dst, size_t cap, const char* src)
{
    if (dst == nullptr || cap == 0)
        return;
    if (src == nullptr)
    {
        dst[0] = '\0';
        return;
    }
    std::strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

bool ensure_wifi()
{
    if (::app::network::wifi::WiFiMgr::get_instance().init())
        return true;
    EBOOK_LOGW(TAG, "wifi init failed");
    return false;
}

void draw_slider_row(gfx::Canvas& c, int16_t y, const char* label, uint8_t pct)
{
    const core::Rect row{0, y, Theme::kScreenW, Theme::kListRowH};
    const int16_t  txt_y = static_cast<int16_t>(row.y + (row.h - Theme::kFontBody) / 2);
    c.text(Theme::kPadLg, txt_y, label, Theme::kFontBody);

    char pct_buf[8];
    (void)std::snprintf(pct_buf, sizeof(pct_buf), "%u%%", static_cast<unsigned>(pct));
    const uint16_t pct_w = gfx::Font::get_instance().measure(pct_buf, Theme::kFontBody);
    const int16_t  bar_x = static_cast<int16_t>(Theme::kPadLg + 60);
    const uint16_t bar_w =
        static_cast<uint16_t>(Theme::kScreenW - bar_x - Theme::kPadLg - pct_w - 4);
    const int16_t bar_y = static_cast<int16_t>(row.y + (row.h - 8) / 2);
    ui::widgets::progress_bar(c, core::Rect{bar_x, bar_y, bar_w, 8}, pct);

    c.text(static_cast<int16_t>(bar_x + bar_w + 4), txt_y, pct_buf, Theme::kFontBody);
    c.hline(Theme::kPadLg, static_cast<int16_t>(row.y + row.h - 1),
            static_cast<uint16_t>(Theme::kScreenW - Theme::kPadLg * 2));
}

bool slider_hit_pct(int16_t x, uint8_t& out_pct)
{
    constexpr int16_t bar_x = Theme::kPadLg + 60;
    constexpr int16_t bar_r = Theme::kScreenW - Theme::kPadLg - 30;
    if (x < bar_x || x >= bar_r)
        return false;
    out_pct = static_cast<uint8_t>(
        (static_cast<uint32_t>(x - bar_x) * 100U) / static_cast<uint32_t>(bar_r - bar_x));
    return true;
}

bool scroll_rows(uint8_t& scroll_row, uint8_t total, uint8_t visible,
                 ::app::ebook::input::EventType type)
{
    const uint8_t max_off = (total > visible) ? static_cast<uint8_t>(total - visible) : 0;
    if (type == ::app::ebook::input::EventType::SwipeUp && scroll_row < max_off)
    {
        ++scroll_row;
        return true;
    }
    if (type == ::app::ebook::input::EventType::SwipeDown && scroll_row > 0)
    {
        --scroll_row;
        return true;
    }
    return false;
}

uint8_t tap_row_index(int16_t y)
{
    if (y < Theme::kListStartY)
        return 0xFF;
    return static_cast<uint8_t>((y - Theme::kListStartY) / Theme::kListRowH);
}

} // namespace app::ebook::apps::settings::detail
