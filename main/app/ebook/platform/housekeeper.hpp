#pragma once

#include <memory>

#include "system/task/task.hpp"

namespace app::ebook::platform {

/** @brief 1s 周期任务：时钟/电池 tick、SD 热插拔、闹钟、AutoLock（经 UiBus，不直接改 UI） */
class Housekeeper
{
  public:
    static Housekeeper& get_instance();

    bool start();
    void stop();

  private:
    Housekeeper() = default;
    ~Housekeeper() = default;

    void run();

    static constexpr uint32_t kPeriodMs          = 1000;
    static constexpr uint32_t kBatteryEveryTicks = 5;
    static constexpr uint32_t kStackBytes        = 4096;

    std::unique_ptr<::app::sys::task::Task> task_;
    bool running_{false};
};

} // namespace app::ebook::platform
