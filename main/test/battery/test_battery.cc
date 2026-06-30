#include "test_battery.hpp"

#include "battery.hpp"
#include "system/task/task.hpp"
#include "test_log.hpp"

namespace {
const char* const TAG = "test_battery";

using app::bsp::battery::Battery;

constexpr uint32_t kReadIntervalMs = 2000;
constexpr uint32_t kReadCount = 10;

} // namespace

extern "C" void test_battery(void)
{
    app::test::log_section_begin(TAG, "电池电量检测测试");

    Battery bat;
    if (!bat.init())
    {
        app::test::log_kv(TAG, "电池", "init 失败（ADC / 校准）");
        app::test::log_check(TAG, "初始化", false);
        app::test::log_section_end(TAG);
        return;
    }
    app::test::log_check(TAG, "初始化", true);

    bool ok_voltage = true;

    for (uint32_t i = 0; i < kReadCount; i++)
    {
        uint32_t mv = bat.read_voltage_mv();
        uint8_t pct = bat.read_percent();

        app::test::log_kv_fmt(TAG, "采样", "[%u/%u]  %u mV  %u%%", static_cast<unsigned>(i + 1),
                              static_cast<unsigned>(kReadCount), static_cast<unsigned>(mv), pct);

        // 合理性校验：锂电池电压应在 2500~4300 mV
        if (mv < 2500 || mv > 4300)
        {
            app::test::log_kv_fmt(TAG, "异常", "电压 %u mV 超出合理范围", static_cast<unsigned>(mv));
            ok_voltage = false;
        }

        app::sys::task::TaskMgr::delay_ms(kReadIntervalMs);
    }

    app::test::log_check(TAG, "电压范围", ok_voltage);
    app::test::log_check(TAG, "电池测试汇总", ok_voltage);
    app::test::log_section_end(TAG);
}
