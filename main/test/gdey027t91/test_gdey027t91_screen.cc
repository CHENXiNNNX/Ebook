#include "test_gdey027t91.hpp"

#include <cstring>

#include "backlight.hpp"
#include "framebuffer.hpp"
#include "gdey027t91.hpp"
#include "system/task/task.hpp"
#include "test_log.hpp"

namespace {
const char* const TAG = "test_gdey027t91_screen";

using app::bsp::driver::gdey027t91::Backlight;
using app::bsp::driver::gdey027t91::Framebuffer;
using app::bsp::driver::gdey027t91::Gdey027t91;
using app::bsp::driver::gdey027t91::Present;
using app::bsp::driver::gdey027t91::Window;

constexpr uint32_t kStepDelayMs = 2000;

void fill_solid(uint8_t* fb, uint32_t len, uint8_t color)
{
    if (fb != nullptr && len > 0)
        std::memset(fb, color, len);
}

/** 横向条纹，便于目视确认行列与分辨率 */
void fill_horizontal_bars(uint8_t* fb)
{
    if (fb == nullptr)
        return;

    for (uint16_t y = 0; y < Gdey027t91::HEIGHT; y++)
    {
        const bool black = ((y / 16) % 2) == 0;
        Framebuffer::fill_rect(fb, 0, y, Gdey027t91::WIDTH, 1, black);
    }
}

} // namespace

extern "C" void test_gdey027t91_screen(void)
{
    app::test::log_section_begin(TAG, "GDEY027T91 屏幕基础测试");

    Gdey027t91 epd;
    if (!epd.init())
    {
        app::test::log_kv(TAG, "电子纸", "init 失败（SPI / 引脚 / 供电）");
        app::test::log_section_end(TAG);
        return;
    }
    app::test::log_kv_fmt(TAG, "分辨率", "%u x %u，帧缓冲 %u 字节", Gdey027t91::WIDTH,
                          Gdey027t91::HEIGHT, Gdey027t91::BUFFER_SIZE);
    app::test::log_kv(TAG, "电子纸", "init 正常");

    Backlight backlight;
    if (!backlight.init())
    {
        app::test::log_kv(TAG, "背光", "init 失败（可继续测屏）");
    }
    else
    {
        backlight.set_brightness(50);
        app::test::log_kv_fmt(TAG, "背光", "已打开，亮度 %u%%", backlight.brightness());
    }

    uint8_t* fb = epd.fb();
    if (fb == nullptr)
    {
        app::test::log_kv(TAG, "帧缓冲", "获取失败");
        app::test::log_section_end(TAG);
        return;
    }

    bool ok_full_white = false;
    bool ok_full_black = false;
    bool ok_fast_bars = false;
    bool ok_partial = false;

    // 1. 全刷新清屏（白）
    app::test::log_kv(TAG, "步骤", "全刷新 → 全白");
    epd.fill(0xFF);
    ok_full_white = epd.present(Present::Full);
    app::test::log_check(TAG, "全白", ok_full_white);
    app::sys::task::TaskMgr::delay_ms(kStepDelayMs);

    // 2. 全刷新填黑
    app::test::log_kv(TAG, "步骤", "全刷新 → 全黑");
    fill_solid(fb, Gdey027t91::BUFFER_SIZE, 0x00);
    ok_full_black = epd.present(Present::Full);
    app::test::log_check(TAG, "全黑", ok_full_black);
    app::sys::task::TaskMgr::delay_ms(kStepDelayMs);

    // 3. 快速刷新条纹
    app::test::log_kv(TAG, "步骤", "快速刷新 → 横向条纹");
    fill_horizontal_bars(fb);
    ok_fast_bars = epd.present(Present::Fast);
    app::test::log_check(TAG, "快速条纹", ok_fast_bars);
    app::sys::task::TaskMgr::delay_ms(kStepDelayMs);

    // 4. 局部刷新：白底 + 黑块
    app::test::log_kv(TAG, "步骤", "局部刷新 → 白底中央黑块");
    fill_solid(fb, Gdey027t91::BUFFER_SIZE, 0xFF);
    if (!epd.present(Present::Base))
    {
        app::test::log_check(TAG, "局刷基准", false);
    }
    else
    {
        constexpr uint16_t part_x = 48;
        constexpr uint16_t part_y = 64;
        constexpr uint16_t part_w = 64;
        constexpr uint16_t part_h = 96;

        Framebuffer::fill_rect(fb, part_x, part_y, part_w, part_h, true);
        Window win{part_x, part_y, part_w, part_h};
        ok_partial = epd.present(Present::Partial, win);
        app::test::log_check(TAG, "局部黑块(硬件窗口)", ok_partial);
    }
    app::sys::task::TaskMgr::delay_ms(kStepDelayMs);

    // 5. 收尾：恢复全白
    app::test::log_kv(TAG, "步骤", "收尾 → 全白");
    epd.fill(0xFF);
    const bool ok_cleanup = epd.present(Present::Full);
    app::test::log_check(TAG, "收尾全白", ok_cleanup);
    app::sys::task::TaskMgr::delay_ms(kStepDelayMs);

    const bool all_ok = ok_full_white && ok_full_black && ok_fast_bars && ok_partial && ok_cleanup;
    app::test::log_check(TAG, "屏幕基础测试汇总", all_ok);
    app::test::log_section_end(TAG);
}
