#include "apps/settings/settings_app.hpp"

#include "apps/settings/settings_internal.hpp"
#include "input/key_bindings.hpp"
#include "ui/strings.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

namespace app::ebook::apps::settings {

namespace {

using ::app::ebook::input::InputContext;
using ::app::ebook::input::KeyBindings;
using ::app::ebook::input::PhysicalKey;
using ui::Theme;
using detail::tap_row_index;

constexpr uint8_t kKeysMenuRows = 4;

InputContext ctx_from_menu_row(uint8_t row)
{
    switch (row)
    {
        case 0: return InputContext::Reader;
        case 1: return InputContext::List;
        case 2: return InputContext::Global;
        default: return InputContext::Reader;
    }
}

constexpr PhysicalKey kSlotKeys[3] = {
    PhysicalKey::Up,
    PhysicalKey::Mid,
    PhysicalKey::Down,
};

} // namespace

void SettingsApp::enter_keys_edit(InputContext ctx)
{
    keys_edit_ctx_ = ctx;
    page_          = SettingsPage::KeysEdit;
    request_repaint();
}

void SettingsApp::paint_keys(gfx::Canvas& c)
{
    int16_t y = Theme::kListStartY;

    for (uint8_t i = 0; i < 3; ++i)
    {
        ui::widgets::RowStyle rs{};
        rs.label        = KeyBindings::context_label(ctx_from_menu_row(i));
        rs.show_chevron = true;
        ui::widgets::list_row(c, core::Rect{0, y, Theme::kScreenW, Theme::kListRowH}, rs);
        y = static_cast<int16_t>(y + Theme::kListRowH);
    }

    {
        ui::widgets::RowStyle rs{};
        rs.label        = ui::strings::kKeyRestore;
        rs.show_chevron = false;
        ui::widgets::list_row(c, core::Rect{0, y, Theme::kScreenW, Theme::kListRowH}, rs);
    }
}

void SettingsApp::paint_keys_edit(gfx::Canvas& c)
{
    auto& kb  = KeyBindings::instance();
    int16_t y = Theme::kListStartY;

    for (uint8_t i = 0; i < 3; ++i)
    {
        const PhysicalKey slot = kSlotKeys[i];
        ui::widgets::RowStyle rs{};
        rs.label        = KeyBindings::key_label(slot);
        rs.value        = KeyBindings::action_label(kb.get(keys_edit_ctx_, slot));
        rs.show_chevron = false;
        ui::widgets::list_row(c, core::Rect{0, y, Theme::kScreenW, Theme::kListRowH}, rs);
        y = static_cast<int16_t>(y + Theme::kListRowH);
    }
}

shell::InputResult SettingsApp::handle_keys(const ::app::ebook::input::Event& ev)
{
    using ::app::ebook::input::EventType;
    if (ev.type != EventType::Tap)
        return {};

    const uint8_t row = tap_row_index(static_cast<int16_t>(ev.y));
    if (row >= kKeysMenuRows)
        return {};

    if (row < 3)
    {
        enter_keys_edit(ctx_from_menu_row(row));
        return {true};
    }

    KeyBindings::instance().restore_defaults();
    request_repaint();
    return {true};
}

shell::InputResult SettingsApp::handle_keys_edit(const ::app::ebook::input::Event& ev)
{
    using ::app::ebook::input::EventType;
    if (ev.type != EventType::Tap)
        return {};

    const uint8_t row = tap_row_index(static_cast<int16_t>(ev.y));
    if (row >= 3)
        return {};

    KeyBindings::instance().cycle(keys_edit_ctx_, kSlotKeys[row]);
    request_repaint();
    return {true};
}

} // namespace app::ebook::apps::settings
