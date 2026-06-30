#pragma once

#include <cstdint>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "input/input_event.hpp"
#include "input/physical_types.hpp"
#include "ui/ui_event.hpp"

namespace app::ebook::ui {

/** @brief 多生产者 → UiLoop 事件队列（深度 32） */
class UiBus
{
  public:
    static constexpr uint16_t kQueueDepth = 32;

    static UiBus& get_instance();

    bool init();
    void deinit();
    bool ready() const { return queue_ != nullptr; }

    bool post(const UiEvent& ev, uint32_t timeout_ms = 0);
    bool post_isr(const UiEvent& ev);

    bool post_input(const ::app::ebook::input::Event& ev);
    bool post_physical(::app::ebook::input::PhysicalKey key,
                       ::app::ebook::input::PhysicalAction action);
    bool post_tick_clock(uint8_t hour, uint8_t minute, uint8_t second = 255);
    bool post_tick_battery(uint8_t pct, uint32_t mv);
    bool post_wifi_state(uint8_t state, uint8_t fail);
    bool post_wifi_scan_done();
    bool post_ntp_sync_done(uint8_t status);
    bool post_system_hint(SystemHintKind hint, uint32_t value = 0);

    bool recv(UiEvent& out, uint32_t timeout_ms);

  private:
    UiBus() = default;
    ~UiBus() = default;
    UiBus(const UiBus&) = delete;
    UiBus& operator=(const UiBus&) = delete;

    QueueHandle_t queue_{nullptr};
};

} // namespace app::ebook::ui
