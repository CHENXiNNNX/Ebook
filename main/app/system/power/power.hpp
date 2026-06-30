#pragma once

#include <functional>

#include "esp_sleep.h"

namespace app::sys::power {

/** Light / Deep Sleep 封装 */
class PowerMgr
{
  public:
    using ExitLowPowerCallback = std::function<void()>;

    static PowerMgr& get_instance();

    void set_exit_low_power_cb(ExitLowPowerCallback callback);

    void enter_light_sleep();

    /** 不返回，芯片重启 */
    void enter_deep_sleep();

  private:
    PowerMgr() = default;
    ~PowerMgr() = default;
    PowerMgr(const PowerMgr&) = delete;
    PowerMgr& operator=(const PowerMgr&) = delete;

    ExitLowPowerCallback exit_callback_;
};

} // namespace app::sys::power
