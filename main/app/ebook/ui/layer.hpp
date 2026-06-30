#pragma once

#include <cstdint>

#include "core/geometry.hpp"
#include "gfx/canvas.hpp"
#include "input/input_event.hpp"
#include "shell/page.hpp"
#include "ui/ui_event.hpp"

namespace app::ebook::ui {

using InputResult = shell::InputResult;

/** 浮层基类（键盘等）；`InputResult` 为 shell 别名 */
class Layer
{
  public:
    virtual ~Layer() = default;

    virtual void on_attach() {}
    virtual void on_detach() {}

    virtual bool visible() const { return true; }
    virtual bool modal() const { return false; }
    virtual bool wants_status_bar() const { return true; }
    virtual core::Rect bounds() const = 0;

    virtual InputResult on_input(const ::app::ebook::input::Event& ev)
    {
        (void)ev;
        return {};
    }

    virtual void paint(gfx::Canvas& canvas) = 0;

    virtual void on_ui_event(const UiEvent& ev)
    {
        switch (ev.kind)
        {
            case UiEventKind::TickClock:
                on_tick_clock(ev.payload.clock.hour, ev.payload.clock.minute,
                              ev.payload.clock.second);
                break;
            case UiEventKind::TickBattery:
                on_tick_battery(ev.payload.battery.pct);
                break;
            case UiEventKind::WifiState:
                on_wifi_state(ev.payload.wifi.state, ev.payload.wifi.fail);
                break;
            case UiEventKind::WifiScanDone:
                on_wifi_scan_done();
                break;
            case UiEventKind::NtpSyncDone:
                on_ntp_sync_done(static_cast<uint8_t>(ev.payload.system.value));
                break;
            case UiEventKind::SystemHint:
                on_system_hint(ev.payload.system.hint, ev.payload.system.value);
                break;
            default:
                break;
        }
    }

    virtual void on_tick_clock(uint8_t h, uint8_t m, uint8_t s)
    {
        (void)h; (void)m; (void)s;
    }
    virtual void on_tick_battery(uint8_t pct) { (void)pct; }
    virtual void on_wifi_state(uint8_t state, uint8_t fail)
    {
        (void)state; (void)fail;
    }
    virtual void on_wifi_scan_done() {}
    virtual void on_ntp_sync_done(uint8_t status) { (void)status; }
    virtual void on_system_hint(SystemHintKind kind, uint32_t value)
    {
        (void)kind; (void)value;
    }
};

} // namespace app::ebook::ui
