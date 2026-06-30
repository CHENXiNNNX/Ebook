#include "ui/ui_loop.hpp"

#include "apps/app_registry.hpp"
#include "input/input_dispatch.hpp"
#include "input/physical_types.hpp"
#include "apps/settings/auto_lock.hpp"
#include "apps/settings/settings_app.hpp"
#include "core/log.hpp"
#include "data/system_state.hpp"
#include "apps/settings/power_save.hpp"
#include "overlays/confirm_prompt.hpp"
#include "overlays/control_center.hpp"
#include "overlays/keyboard.hpp"
#include "overlays/status_bar.hpp"
#include "overlays/toast.hpp"
#include "router/page_id.hpp"
#include "router/refresh_intent.hpp"
#include "router/router.hpp"
#include "shell/app_grid_page.hpp"
#include "shell/app_host_page.hpp"
#include "shell/home_page.hpp"
#include "shell/lock_page.hpp"
#include "ui/strings.hpp"
#include "ui/ui_bus.hpp"

static const char* const TAG = "UiLoop";

namespace app::ebook::ui {

UiLoop& UiLoop::instance()
{
    static UiLoop s;
    return s;
}

bool UiLoop::init()
{
    data::SystemState::get_instance().set_night_mode_observer(
        [](bool /*enabled*/) {
            (void)router::Router::instance().repaint(router::intent_partial_full());
        });
    return UiBus::get_instance().init();
}

void UiLoop::deinit()
{
    stop();
}

bool UiLoop::start()
{
    if (running_)
        return true;

    ::app::sys::task::Cfg cfg;
    cfg.name       = "ebook_ui";
    cfg.stack_size = kStackBytes;
    cfg.priority   = ::app::sys::task::Priority::NORMAL;
    cfg.use_psram  = true;

    running_ = true;
    task_    = std::make_unique<::app::sys::task::Task>(
        [](void* arg) { static_cast<UiLoop*>(arg)->run(); }, cfg, this);
    if (!task_->start())
    {
        running_ = false;
        task_.reset();
        EBOOK_LOGE(TAG, "task start failed");
        return false;
    }

    (void)router::Router::instance().replace_shell(router::ShellPage::Lock);
    EBOOK_LOGI(TAG, "started");
    return true;
}

void UiLoop::stop()
{
    if (!running_)
        return;
    running_ = false;
    if (task_)
    {
        task_->destroy();
        task_.reset();
    }
}

void UiLoop::dispatch_input(const ::app::ebook::input::Event& ev)
{
    apps::settings::AutoLock::get_instance().notify_activity();

    if (overlays::ConfirmPrompt::instance().is_open())
    {
        (void)overlays::ConfirmPrompt::instance().on_input(ev);
        return;
    }

    if (overlays::Keyboard::instance().is_open())
    {
        (void)overlays::Keyboard::instance().on_input(ev);
        return;
    }

    const auto& stack = router::Router::instance().stack();
    if (stack.overlay_top() == router::OverlayId::ControlCenter)
    {
        (void)overlays::ControlCenter::instance().on_input(ev);
        return;
    }
    if (stack.overlay_top() != router::OverlayId::None)
        return;

    switch (stack.shell_top())
    {
        case router::ShellPage::Lock:
            (void)shell::LockPage::instance().on_input(ev);
            break;
        case router::ShellPage::Home:
            (void)shell::HomePage::instance().on_input(ev);
            break;
        case router::ShellPage::AppGrid:
            (void)shell::AppGridPage::instance().on_input(ev);
            break;
        case router::ShellPage::AppHost:
            (void)shell::AppHostPage::instance().on_input(ev);
            break;
    }
}

void UiLoop::handle_event(const UiEvent& ev)
{
    switch (ev.kind)
    {
        case UiEventKind::Input:
            dispatch_input(ev.payload.input);
            break;

        case UiEventKind::PhysicalInput:
        {
            ::app::ebook::input::PhysicalEvent pe{};
            pe.key =
                static_cast<::app::ebook::input::PhysicalKey>(ev.payload.physical.key);
            pe.action = static_cast<::app::ebook::input::PhysicalAction>(
                ev.payload.physical.action);
            ::app::ebook::input::dispatch_physical(pe);
            break;
        }

        case UiEventKind::TickClock:
            if (router::Router::instance().stack().shell_top() == router::ShellPage::Lock)
            {
                shell::LockPage::instance().on_ui_event(ev);
            }
            else if (router::Router::instance().stack().shell_top() == router::ShellPage::AppHost)
            {
                if (apps::App* a = apps::AppRegistry::instance().active())
                {
                    if (!a->wants_status_bar())
                        a->on_ui_event(ev);
                    else
                        overlays::StatusBar::instance().on_tick_clock(
                            ev.payload.clock.hour, ev.payload.clock.minute);
                }
            }
            else
            {
                overlays::StatusBar::instance().on_tick_clock(
                    ev.payload.clock.hour, ev.payload.clock.minute);
            }
            break;
        case UiEventKind::TickBattery:
            apps::settings::PowerSave::get_instance().on_battery_pct(ev.payload.battery.pct);
            if (router::Router::instance().stack().shell_top() == router::ShellPage::Lock)
            {
                shell::LockPage::instance().on_ui_event(ev);
            }
            else if (router::Router::instance().stack().shell_top() == router::ShellPage::AppHost)
            {
                if (apps::App* a = apps::AppRegistry::instance().active())
                {
                    if (!a->wants_status_bar())
                        a->on_ui_event(ev);
                    else
                        overlays::StatusBar::instance().on_tick_battery(ev.payload.battery.pct);
                }
            }
            else
            {
                overlays::StatusBar::instance().on_tick_battery(ev.payload.battery.pct);
            }
            break;
        case UiEventKind::WifiState:
        case UiEventKind::WifiScanDone:
            apps::settings::SettingsApp::instance().on_ui_event(ev);
            break;
        case UiEventKind::NtpSyncDone:
            apps::settings::SettingsApp::instance().on_ui_event(ev);
            overlays::StatusBar::instance().on_ntp_sync_done(
                static_cast<uint8_t>(ev.payload.system.value));
            break;
        case UiEventKind::SystemHint:
        {
            const auto hint = ev.payload.system.hint;
            if (hint == SystemHintKind::ToastExpire &&
                overlays::Toast::instance().visible())
            {
                overlays::Toast::instance().hide();
                (void)router::Router::instance().repaint(router::intent_partial_full());
            }
            else if (hint == SystemHintKind::ClockAlarm)
            {
                overlays::Toast::instance().show(strings::kClkAlarmRing, 5000);
                (void)router::Router::instance().repaint(router::intent_partial_full());
            }
            else if (hint == SystemHintKind::AutoLock)
            {
                auto& r = router::Router::instance();
                if (r.stack().shell_top() != router::ShellPage::Lock)
                    (void)r.replace_shell(router::ShellPage::Lock);
                apps::settings::AutoLock::get_instance().notify_activity();
            }

            if (router::Router::instance().stack().shell_top() == router::ShellPage::AppHost)
            {
                if (apps::App* a = apps::AppRegistry::instance().active())
                    a->on_ui_event(ev);
            }
            break;
        }

        case UiEventKind::None:
        default:
            break;
    }
}

void UiLoop::run()
{
    auto& bus = UiBus::get_instance();
    UiEvent ev{};
    while (running_)
    {
        if (!bus.recv(ev, portMAX_DELAY))
            continue;

        handle_event(ev);
        while (running_ && bus.recv(ev, 0))
            handle_event(ev);

        ++frames_;
    }
}

} // namespace app::ebook::ui
