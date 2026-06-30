#include "composer/composer.hpp"

#include "apps/app_registry.hpp"
#include "data/system_state.hpp"
#include "gfx/canvas.hpp"
#include "overlays/confirm_prompt.hpp"
#include "overlays/control_center.hpp"
#include "overlays/keyboard.hpp"
#include "overlays/status_bar.hpp"
#include "overlays/toast.hpp"
#include "router/router.hpp"
#include "shell/app_grid_page.hpp"
#include "shell/app_host_page.hpp"
#include "shell/home_page.hpp"
#include "shell/lock_page.hpp"

namespace app::ebook::composer {

Composer& Composer::instance()
{
    static Composer s;
    return s;
}

void Composer::paint(uint8_t* back_fb)
{
    if (back_fb == nullptr)
        return;

    const bool dark = data::SystemState::get_instance().night_mode();
    gfx::Canvas canvas{back_fb, core::Rect::full(), dark};
    canvas.clear(gfx::Ink::White);

    const auto& stack = router::Router::instance().stack();

    auto paint_app_host = [&]() {
        shell::AppHostPage::instance().paint(canvas);
        if (apps::App* app = apps::AppRegistry::instance().active())
        {
            if (app->wants_status_bar())
                overlays::StatusBar::instance().paint(canvas);
            app->paint_overlay(canvas);
        }
    };

    switch (stack.shell_top())
    {
        case router::ShellPage::Lock:
            shell::LockPage::instance().paint(canvas);
            break;
        case router::ShellPage::Home:
            shell::HomePage::instance().paint(canvas);
            overlays::StatusBar::instance().paint(canvas);
            break;
        case router::ShellPage::AppGrid:
            shell::AppGridPage::instance().paint(canvas);
            overlays::StatusBar::instance().paint(canvas);
            break;
        case router::ShellPage::AppHost:
            paint_app_host();
            break;
    }

    if (stack.overlay_top() == router::OverlayId::ControlCenter)
        overlays::ControlCenter::instance().paint(canvas);
    else if (overlays::Keyboard::instance().is_open())
        overlays::Keyboard::instance().paint(canvas);

    overlays::Toast::instance().paint(canvas);
    if (overlays::ConfirmPrompt::instance().is_open())
        overlays::ConfirmPrompt::instance().paint(canvas);
}

} // namespace app::ebook::composer
