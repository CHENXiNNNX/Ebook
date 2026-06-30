#include "test_button.hpp"

#include "button.hpp"
#include "config/config.hpp"
#include "system/task/task.hpp"
#include "test_log.hpp"

namespace {

const char* const TAG = "test_button";

using app::bsp::driver::button::DipId;
using app::bsp::driver::button::DipSwitch;

constexpr uint32_t kIntervalMs = 500;
constexpr uint32_t kSamples = 20;

bool reads_ok(const DipSwitch& dip, bool up, bool mid, bool down, uint8_t mask)
{
    const uint8_t active_count = static_cast<uint8_t>(up) + static_cast<uint8_t>(mid) +
                                 static_cast<uint8_t>(down);
    if (active_count > 1)
    {
        return false;
    }

    uint8_t expect = 0;
    if (up)
    {
        expect |= 1U;
    }
    if (mid)
    {
        expect |= 2U;
    }
    if (down)
    {
        expect |= 4U;
    }
    return mask == expect && dip.read(DipId::UP) == up && dip.read(DipId::MID) == mid &&
           dip.read(DipId::DOWN) == down && dip.read_mask() == mask;
}

} // namespace

extern "C" void test_button(void)
{
    app::test::log_section_begin(TAG, "ADC 拨码测试");

    DipSwitch dip;
    if (!dip.init())
    {
        app::test::log_check(TAG, "初始化", false);
        app::test::log_section_end(TAG);
        return;
    }
    app::test::log_check(TAG, "初始化", true);

    bool ok = true;

    for (uint32_t i = 0; i < kSamples; i++)
    {
        const uint32_t mv = dip.read_voltage_mv();
        const bool up = dip.read(DipId::UP);
        const bool mid = dip.read(DipId::MID);
        const bool down = dip.read(DipId::DOWN);
        const uint8_t mask = dip.read_mask();

        if (!reads_ok(dip, up, mid, down, mask))
        {
            ok = false;
        }

        app::test::log_kv_fmt(TAG, "采样", "[%u/%u] %umV  0x%02X  %u/%u/%u",
                              static_cast<unsigned>(i + 1), static_cast<unsigned>(kSamples),
                              static_cast<unsigned>(mv), mask, up ? 1U : 0U, mid ? 1U : 0U,
                              down ? 1U : 0U);

        app::sys::task::TaskMgr::delay_ms(kIntervalMs);
    }

    app::test::log_check(TAG, "读取", ok);
    app::test::log_check(TAG, "汇总", ok);
    app::test::log_section_end(TAG);
}
