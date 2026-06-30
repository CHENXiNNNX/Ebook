#pragma once

#include <cstdint>
#include <memory>

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include "core/result.hpp"
#include "presenter/frame_request.hpp"
#include "presenter/present_plan.hpp"
#include "system/task/task.hpp"

namespace app::ebook::presenter {

/** @brief 合成 back_fb 并异步上屏（队列合并；合成在 submit 调用方任务） */
class Presenter
{
  public:
    static Presenter& instance();

    bool init();
    void deinit();
    bool start();
    void stop();

    uint8_t* back_fb();
    core::Status submit(const FrameRequest& req);
    bool wait_idle(uint32_t timeout_ms = portMAX_DELAY);

  private:
    Presenter() = default;

    void task_main();
    core::Status execute(const FrameRequest& req);
    void copy_fb();
    core::Status run_plan(const PresentPlan& plan);

    static constexpr uint16_t kQueueDepth = 4;
    static constexpr uint32_t kStackBytes = 6 * 1024;
    static constexpr uint32_t kSubmitTmoMs = 5000;
    static constexpr ::app::sys::task::Priority kPriority =
        ::app::sys::task::Priority::HIGH;
    static constexpr EventBits_t kBitIdle = 1U << 0;

    QueueHandle_t queue_{nullptr};
    SemaphoreHandle_t fb_mutex_{nullptr};
    EventGroupHandle_t state_evt_{nullptr};
    std::unique_ptr<::app::sys::task::Task> task_;

    uint8_t* back_fb_{nullptr};
    bool running_{false};
    bool started_{false};
};

} // namespace app::ebook::presenter
