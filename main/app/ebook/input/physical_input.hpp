#pragma once

#include <memory>

#include "button.hpp"
#include "system/task/task.hpp"

namespace app::ebook::input {

/**
 * @brief 拨码采集任务：ADC 消抖 + 边沿检测 → UiBus::post_physical
 *
 * 禁止在 App / UiLoop 外直接读 DipSwitch。
 */
class PhysicalInput
{
  public:
    static PhysicalInput& get_instance();

    bool init();
    void deinit();
    bool start();
    void stop();

    bool is_ready() const { return dip_.is_init(); }

  private:
    PhysicalInput() = default;

    void task_main();
    void poll_once();

    static constexpr uint32_t kPollMs            = 25;
    static constexpr uint8_t  kDebounceSamples   = 3;
    static constexpr uint32_t kCooldownMs        = 180;
    static constexpr uint32_t kStackBytes        = 3072;

    ::app::bsp::driver::button::DipSwitch dip_{};
    std::unique_ptr<::app::sys::task::Task> task_;
    bool running_{false};

    ::app::bsp::driver::button::DipId stable_{::app::bsp::driver::button::DipId::NONE};
    ::app::bsp::driver::button::DipId pending_{::app::bsp::driver::button::DipId::NONE};
    uint8_t  pending_count_{0};
    uint32_t last_emit_ms_{0};
};

} // namespace app::ebook::input
