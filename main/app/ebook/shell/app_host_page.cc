#include "shell/app_host_page.hpp"

#include "apps/app_registry.hpp"
#include "overlays/control_center.hpp"
#include "router/router.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

namespace app::ebook::shell {

AppHostPage& AppHostPage::instance()
{
    static AppHostPage s;
    return s;
}

void AppHostPage::paint(gfx::Canvas& c)
{
    if (apps::App* a = apps::AppRegistry::instance().active())
        a->paint(c);
}

InputResult AppHostPage::on_input(const ::app::ebook::input::Event& ev)
{
    using ::app::ebook::input::EventType;
    auto& reg = apps::AppRegistry::instance();
    auto& r   = router::Router::instance();

    if (ev.type == EventType::SwipeDown &&
        ev.start_y <= ui::Theme::kStatusBarH)
    {
        overlays::ControlCenter::instance().open();
        return {true};
    }

    if (apps::App* a = reg.active())
    {
        const auto out = a->on_input(ev);
        if (out.consumed)
            return out;
    }

    if (ev.type == EventType::Tap &&
        ui::widgets::hit_toolbar_back(static_cast<int16_t>(ev.x), static_cast<int16_t>(ev.y)))
    {
        reg.close_active();
        (void)r.back();
        return {true};
    }
    return {};
}

} // namespace app::ebook::shell
