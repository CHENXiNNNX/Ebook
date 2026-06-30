#include "apps/settings/settings_app.hpp"

#include <cstdio>
#include <cstring>

#include "apps/settings/settings_internal.hpp"
#include "data/system_state.hpp"
#include "gfx/icon.hpp"
#include "network/bluetooth/bluetooth.hpp"
#include "overlays/keyboard.hpp"
#include "ui/strings.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

namespace app::ebook::apps::settings {

namespace {

using ui::Theme;
using detail::tap_row_index;

} // namespace

void SettingsApp::paint_bluetooth(gfx::Canvas& c)
{
    int16_t y = Theme::kListStartY;
    auto&   sys = data::SystemState::get_instance();

    {
        ui::widgets::RowStyle rs{};
        rs.label           = ui::strings::kCtlBt;
        rs.value_icon      = gfx::icon::toggle(sys.bluetooth());
        rs.value_icon_size = 20;
        rs.show_chevron    = false;
        ui::widgets::list_row(c, core::Rect{0, y, Theme::kScreenW, Theme::kListRowH}, rs);
    }
    y = static_cast<int16_t>(y + Theme::kListRowH);

    {
        ui::widgets::RowStyle rs{};
        rs.label        = ui::strings::kSetBtName;
        rs.value        = bt_name_;
        rs.show_chevron = true;
        ui::widgets::list_row(c, core::Rect{0, y, Theme::kScreenW, Theme::kListRowH}, rs);
    }
    y = static_cast<int16_t>(y + Theme::kListRowH);

    {
        char cnt[8];
        const uint8_t n = sys.bluetooth()
                              ? ::app::network::bluetooth::BleMgr::get_instance().get_conn_count()
                              : 0;
        (void)std::snprintf(cnt, sizeof(cnt), ui::strings::kSetConnCountFmt,
                            static_cast<unsigned>(n));
        ui::widgets::RowStyle rs{};
        rs.label        = ui::strings::kSetBtClients;
        rs.value        = cnt;
        rs.show_chevron = false;
        ui::widgets::list_row(c, core::Rect{0, y, Theme::kScreenW, Theme::kListRowH}, rs);
    }
}

shell::InputResult SettingsApp::handle_bluetooth(const ::app::ebook::input::Event& ev)
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
        sys.set_bluetooth(!sys.bluetooth());
        bt_apply_state();
        request_repaint();
        return {true};
    }
    if (row == 1)
    {
        overlays::KeyboardConfig kc{};
        kc.default_layer = overlays::KeyboardLayer::Letters;
        kc.max_len       = 32;
        kc.initial_text  = bt_name_;
        kc.title         = ui::strings::kSetBtName;
        overlays::Keyboard::instance().open(kc, on_bt_name_done, this);
        return {true};
    }
    return {};
}

void SettingsApp::bt_enter()
{
    bt_apply_state();
}

void SettingsApp::bt_leave() {}

void SettingsApp::bt_apply_state()
{
    auto& mgr = ::app::network::bluetooth::BleMgr::get_instance();
    const bool on = data::SystemState::get_instance().bluetooth();
    if (on)
    {
        if (!mgr.is_init())
            mgr.init(bt_name_);
        if (!mgr.is_advertising())
        {
            ::app::network::bluetooth::AdvCfg cfg;
            cfg.device_name = bt_name_;
            mgr.start_advertising(cfg);
        }
    }
    else if (mgr.is_advertising())
    {
        mgr.stop_advertising();
    }
}

void SettingsApp::on_bt_name_done(const char* text, void* user)
{
    auto* self = static_cast<SettingsApp*>(user);
    if (self == nullptr || text == nullptr)
        return;
    std::strncpy(self->bt_name_, text, sizeof(self->bt_name_) - 1);
    self->bt_name_[sizeof(self->bt_name_) - 1] = '\0';
    self->bt_apply_state();
    self->request_repaint();
}

} // namespace app::ebook::apps::settings
