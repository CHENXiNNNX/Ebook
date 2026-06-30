#pragma once

#include <cstdint>
#include <memory>

#include "input/input_event.hpp"
#include "system/task/task.hpp"
#include "ui/ui_event.hpp"

namespace app::ebook::ui {

/** @brief UI 线程：UiBus 消费、输入分发、Tick/WiFi/NTP 路由 */
class UiLoop
{
  public:
    static UiLoop& instance();

    bool init();
    void deinit();
    bool start();
    void stop();

    uint32_t frame_count() const { return frames_; }

  private:
    UiLoop() = default;

    void run();
    void handle_event(const UiEvent& ev);
    void dispatch_input(const ::app::ebook::input::Event& ev);

    static constexpr uint32_t kStackBytes = 24 * 1024;

    std::unique_ptr<::app::sys::task::Task> task_;
    bool running_{false};
    uint32_t frames_{0};
};

} // namespace app::ebook::ui
