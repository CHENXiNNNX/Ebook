#include "ui/ui_bus.hpp"

namespace app::ebook::ui {

namespace {

UiEvent make_event(UiEventKind k)
{
    UiEvent ev{};
    ev.kind = k;
    return ev;
}

} // namespace

UiBus& UiBus::get_instance()
{
    static UiBus s;
    return s;
}

bool UiBus::init()
{
    if (queue_ != nullptr) return true;
    queue_ = xQueueCreate(kQueueDepth, sizeof(UiEvent));
    return queue_ != nullptr;
}

void UiBus::deinit()
{
    if (queue_ != nullptr)
    {
        vQueueDelete(queue_);
        queue_ = nullptr;
    }
}

bool UiBus::post(const UiEvent& ev, uint32_t timeout_ms)
{
    if (queue_ == nullptr) return false;
    return xQueueSend(queue_, &ev, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

bool UiBus::post_isr(const UiEvent& ev)
{
    if (queue_ == nullptr) return false;
    BaseType_t hp_wake = pdFALSE;
    const BaseType_t ok = xQueueSendFromISR(queue_, &ev, &hp_wake);
    portYIELD_FROM_ISR(hp_wake);
    return ok == pdTRUE;
}

bool UiBus::post_input(const ::app::ebook::input::Event& ev)
{
    UiEvent e = make_event(UiEventKind::Input);
    e.payload.input = ev;
    return post(e, 0);
}

bool UiBus::post_physical(::app::ebook::input::PhysicalKey key,
                          ::app::ebook::input::PhysicalAction action)
{
    UiEvent e = make_event(UiEventKind::PhysicalInput);
    e.payload.physical.key    = static_cast<uint8_t>(key);
    e.payload.physical.action = static_cast<uint8_t>(action);
    return post(e, 0);
}

bool UiBus::post_tick_clock(uint8_t hour, uint8_t minute, uint8_t second)
{
    UiEvent e = make_event(UiEventKind::TickClock);
    e.payload.clock.hour   = hour;
    e.payload.clock.minute = minute;
    e.payload.clock.second = second;
    return post(e, 0);
}

bool UiBus::post_tick_battery(uint8_t pct, uint32_t mv)
{
    UiEvent e = make_event(UiEventKind::TickBattery);
    e.payload.battery.pct = pct;
    e.payload.battery.mv  = mv;
    return post(e, 0);
}

bool UiBus::post_wifi_state(uint8_t state, uint8_t fail)
{
    UiEvent e = make_event(UiEventKind::WifiState);
    e.payload.wifi.state = state;
    e.payload.wifi.fail  = fail;
    return post(e, 0);
}

bool UiBus::post_wifi_scan_done()
{
    return post(make_event(UiEventKind::WifiScanDone), 0);
}

bool UiBus::post_ntp_sync_done(uint8_t status)
{
    UiEvent e = make_event(UiEventKind::NtpSyncDone);
    e.payload.system.hint  = SystemHintKind::None;
    e.payload.system.value = status;
    return post(e, 0);
}

bool UiBus::post_system_hint(SystemHintKind hint, uint32_t value)
{
    UiEvent e = make_event(UiEventKind::SystemHint);
    e.payload.system.hint  = hint;
    e.payload.system.value = value;

    // ReaderIndexDone 必须送达（队列满时阻塞），否则 UI 卡在索引页
    const uint32_t to = (hint == SystemHintKind::ReaderIndexDone)
                            ? portMAX_DELAY : 0;
    return post(e, to);
}

bool UiBus::recv(UiEvent& out, uint32_t timeout_ms)
{
    if (queue_ == nullptr) return false;
    return xQueueReceive(queue_, &out, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

} // namespace app::ebook::ui
