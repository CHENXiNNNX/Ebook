#include "power.hpp"

#include "esp_log.h"

static const char* const TAG = "Power";

namespace app::sys::power {

PowerMgr& PowerMgr::get_instance()
{
    static PowerMgr instance;
    return instance;
}

void PowerMgr::set_exit_low_power_cb(ExitLowPowerCallback callback)
{
    exit_callback_ = callback;
}

void PowerMgr::enter_light_sleep()
{
    ESP_LOGI(TAG, "进入 Light Sleep");
    esp_light_sleep_start();
    if (exit_callback_)
    {
        exit_callback_();
    }
}

void PowerMgr::enter_deep_sleep()
{
    ESP_LOGI(TAG, "进入 Deep Sleep");
    esp_deep_sleep_start();
}

} // namespace app::sys::power
