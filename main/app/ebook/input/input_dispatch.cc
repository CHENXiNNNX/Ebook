#include "input/input_dispatch.hpp"

#include "apps/app_registry.hpp"
#include "apps/reader/reader_app.hpp"
#include "apps/settings/auto_lock.hpp"
#include "data/system_state.hpp"
#include "input/input_event.hpp"
#include "overlays/control_center.hpp"
#include "overlays/keyboard.hpp"
#include "router/router.hpp"
#include "shell/app_grid_page.hpp"
#include "shell/app_host_page.hpp"
#include "shell/home_page.hpp"
#include "shell/lock_page.hpp"
#include "ui/theme.hpp"

namespace app::ebook::input {

namespace {

using ui::Theme;

Event synthetic_swipe(EventType type)
{
    Event ev{};
    ev.type    = type;
    ev.x       = static_cast<uint16_t>(Theme::kScreenW / 2);
    ev.y       = static_cast<uint16_t>(Theme::kListStartY + Theme::kListRowH);
    ev.start_x = ev.x;
    ev.start_y = ev.y;
    return ev;
}

void route_gesture(const Event& ev)
{
    using router::OverlayId;
    using router::Router;
    using router::ShellPage;

    if (overlays::Keyboard::instance().is_open())
    {
        (void)overlays::Keyboard::instance().on_input(ev);
        return;
    }

    const auto& stack = Router::instance().stack();
    if (stack.overlay_top() == OverlayId::ControlCenter)
    {
        (void)overlays::ControlCenter::instance().on_input(ev);
        return;
    }
    if (stack.overlay_top() != OverlayId::None)
        return;

    switch (stack.shell_top())
    {
        case ShellPage::Lock:
            (void)shell::LockPage::instance().on_input(ev);
            break;
        case ShellPage::Home:
            (void)shell::HomePage::instance().on_input(ev);
            break;
        case ShellPage::AppGrid:
            (void)shell::AppGridPage::instance().on_input(ev);
            break;
        case ShellPage::AppHost:
            (void)shell::AppHostPage::instance().on_input(ev);
            break;
    }
}

void adjust_brightness(int delta)
{
    auto& sys = data::SystemState::get_instance();
    int     v = static_cast<int>(sys.brightness()) + delta;
    if (v < 0)
        v = 0;
    if (v > 100)
        v = 100;
    sys.set_brightness(static_cast<uint8_t>(v));
    (void)router::Router::instance().repaint(router::intent_partial_full());
}

void adjust_volume(int delta)
{
    auto& sys = data::SystemState::get_instance();
    int     v = static_cast<int>(sys.volume()) + delta;
    if (v < 0)
        v = 0;
    if (v > 100)
        v = 100;
    sys.set_volume(static_cast<uint8_t>(v));
    (void)router::Router::instance().repaint(router::intent_partial_full());
}

} // namespace

void dispatch_semantic(SemanticAction action)
{
    switch (action)
    {
        case SemanticAction::PageNext:
            route_gesture(synthetic_swipe(EventType::SwipeLeft));
            break;
        case SemanticAction::PagePrev:
            route_gesture(synthetic_swipe(EventType::SwipeRight));
            break;
        case SemanticAction::ListUp:
            route_gesture(synthetic_swipe(EventType::SwipeUp));
            break;
        case SemanticAction::ListDown:
            route_gesture(synthetic_swipe(EventType::SwipeDown));
            break;
        case SemanticAction::Back:
            if (router::Router::instance().stack().shell_top() == router::ShellPage::Lock)
                break;
            if (router::Router::instance().stack().shell_top() == router::ShellPage::AppHost)
            {
                if (apps::App* a = apps::AppRegistry::instance().active())
                {
                    if (a->on_semantic_back())
                        break;
                }
                apps::AppRegistry::instance().close_active();
                (void)router::Router::instance().back();
            }
            else
            {
                (void)router::Router::instance().back();
            }
            break;
        case SemanticAction::Menu:
            if (router::Router::instance().stack().shell_top() == router::ShellPage::AppHost &&
                apps::AppRegistry::instance().active_id() == apps::AppId::Reader)
            {
                apps::reader::ReaderApp::instance().toggle_reading_menu();
            }
            break;
        case SemanticAction::Home:
            if (router::Router::instance().stack().shell_top() != router::ShellPage::Home)
                (void)router::Router::instance().replace_shell(router::ShellPage::Home);
            break;
        case SemanticAction::BrightnessUp:
            adjust_brightness(10);
            break;
        case SemanticAction::BrightnessDown:
            adjust_brightness(-10);
            break;
        case SemanticAction::VolumeUp:
            adjust_volume(10);
            break;
        case SemanticAction::VolumeDown:
            adjust_volume(-10);
            break;
        case SemanticAction::None:
        default:
            break;
    }
}

void dispatch_physical(const PhysicalEvent& ev)
{
    if (overlays::Keyboard::instance().is_open())
        return;

    if (router::Router::instance().stack().overlay_top() == router::OverlayId::ControlCenter)
        return;

    const InputContext ctx = detect_context();
    if (ctx == InputContext::Global)
        return;

    apps::settings::AutoLock::get_instance().notify_activity();

    const SemanticAction act = resolve_physical(ev.key, ev.action, ctx);
    if (act != SemanticAction::None)
        dispatch_semantic(act);
}

} // namespace app::ebook::input
