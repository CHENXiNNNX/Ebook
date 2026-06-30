#include "apps/settings/settings_app.hpp"

#include <cstdio>
#include <cstring>

#include "storage/storage.hpp"
#include "ui/strings.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

namespace app::ebook::apps::settings {

namespace {

using ui::Theme;

} // namespace

void SettingsApp::paint_storage(gfx::Canvas& c)
{
    auto& sto = ::app::common::storage::StorageMgr::get_instance();

    char internal_str[24] = "-";
    char assets_str[24]   = "-";
    char sd_str[40]       = {};

    size_t total = 0;
    size_t used  = 0;
    if (sto.query_internal_usage(&total, &used) == ESP_OK)
    {
        (void)std::snprintf(internal_str, sizeof(internal_str), "%u/%u KB",
                            static_cast<unsigned>(used / 1024),
                            static_cast<unsigned>(total / 1024));
    }
    if (sto.query_assets_usage(&total, &used) == ESP_OK)
    {
        (void)std::snprintf(assets_str, sizeof(assets_str), "%u/%u KB",
                            static_cast<unsigned>(used / 1024),
                            static_cast<unsigned>(total / 1024));
    }

    uint64_t sd_total = 0;
    uint64_t sd_used  = 0;
    if (sto.query_sd_usage(&sd_total, &sd_used) == ESP_OK && sd_total > 0)
    {
        (void)std::snprintf(sd_str, sizeof(sd_str), "%lu/%lu MB",
                            static_cast<unsigned long>(sd_used / (1024ULL * 1024ULL)),
                            static_cast<unsigned long>(sd_total / (1024ULL * 1024ULL)));
    }
    else
    {
        std::strncpy(sd_str, ui::strings::kSetStNotIn, sizeof(sd_str) - 1);
        sd_str[sizeof(sd_str) - 1] = '\0';
    }

    int16_t y = Theme::kListStartY;
    struct RowDef
    {
        const char* lbl;
        const char* val;
    };
    const RowDef rows[] = {
        {ui::strings::kSetStInternal, internal_str},
        {ui::strings::kSetStAssets, assets_str},
        {ui::strings::kSetStSd, sd_str},
    };
    for (const RowDef& r : rows)
    {
        ui::widgets::RowStyle rs{};
        rs.label        = r.lbl;
        rs.value        = r.val;
        rs.show_chevron = false;
        ui::widgets::list_row(c, core::Rect{0, y, Theme::kScreenW, Theme::kListRowH}, rs);
        y = static_cast<int16_t>(y + Theme::kListRowH);
    }
}

} // namespace app::ebook::apps::settings
