#include "apps/settings/settings_app.hpp"

#include "apps/settings/settings_internal.hpp"
#include "data/system_state.hpp"
#include "gfx/icon.hpp"
#include "ui/strings.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

namespace app::ebook::apps::settings {

namespace {

using ui::Theme;
using detail::draw_slider_row;
using detail::slider_hit_pct;
using detail::tap_row_index;

} // namespace

void SettingsApp::paint_sound(gfx::Canvas& c)
{
    auto&   sys = data::SystemState::get_instance();
    int16_t y   = Theme::kListStartY;

    draw_slider_row(c, y, ui::strings::kSetVolume, sys.volume());
    y = static_cast<int16_t>(y + Theme::kListRowH);

    ui::widgets::RowStyle rs{};
    rs.label           = ui::strings::kSetMute;
    rs.value_icon      = gfx::icon::toggle(sys.mute());
    rs.value_icon_size = 20;
    rs.show_chevron    = false;
    ui::widgets::list_row(c, core::Rect{0, y, Theme::kScreenW, Theme::kListRowH}, rs);
}

shell::InputResult SettingsApp::handle_sound(const ::app::ebook::input::Event& ev)
{
    using ::app::ebook::input::EventType;
    if (ev.type != EventType::Tap)
        return {};

    const int16_t x = static_cast<int16_t>(ev.x);
    const int16_t y = static_cast<int16_t>(ev.y);
    const uint8_t row = tap_row_index(y);
    if (row == 0xFF)
        return {};

    auto& sys = data::SystemState::get_instance();

    if (row == 0)
    {
        uint8_t v = 0;
        if (slider_hit_pct(x, v))
            sys.set_volume(v);
        request_repaint();
        return {true};
    }
    if (row == 1)
    {
        sys.set_mute(!sys.mute());
        request_repaint();
        return {true};
    }
    return {};
}

} // namespace app::ebook::apps::settings
