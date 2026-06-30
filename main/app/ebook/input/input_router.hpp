#pragma once

#include <memory>

#include "ft6336u.hpp"
#include "system/task/task.hpp"
#include "touch_gesture.hpp"

#include "input/gesture_tuner.hpp"
#include "input/input_event.hpp"

namespace app::ebook::input {

/** @brief 触摸采集任务：wait_interrupt → gesture → UiBus（不在采集线程改 UI） */
class InputRouter
{
  public:
    static InputRouter& get_instance();

    bool init(::app::bsp::driver::ft6336u::Ft6336u* touch);
    void deinit();

    bool start();
    void stop();

    void set_profile(Profile p);

    ::app::bsp::driver::ft6336u::TouchGesture& gesture() { return gesture_; }

  private:
    InputRouter() = default;
    ~InputRouter() = default;
    InputRouter(const InputRouter&) = delete;
    InputRouter& operator=(const InputRouter&) = delete;

    void task_main();
    void drain_events();
    void process_release();

    static constexpr uint32_t kIdlePollMs = 20;
    static constexpr uint32_t kCoalesceMs = 12;
    static constexpr uint32_t kStackBytes = 4096;

    ::app::bsp::driver::ft6336u::Ft6336u*     dev_{nullptr};
    ::app::bsp::driver::ft6336u::TouchGesture gesture_;
    GestureConfig                             cfg_{};
    std::unique_ptr<::app::sys::task::Task>   task_;
    bool                                      running_{false};
};

} // namespace app::ebook::input
