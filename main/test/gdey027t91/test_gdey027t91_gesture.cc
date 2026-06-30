#include "test_gdey027t91.hpp"

#include "ft6336u.hpp"
#include "gdey027t91.hpp"
#include "i2c/i2c.hpp"
#include "test_log.hpp"
#include "touch_gesture.hpp"

namespace {

const char* const TAG = "test_gdey027t91_gesture";

using app::bsp::driver::ft6336u::Config;
using app::bsp::driver::ft6336u::Ft6336u;
using app::bsp::driver::ft6336u::GestureConfig;
using app::bsp::driver::ft6336u::GestureEvent;
using app::bsp::driver::ft6336u::GestureType;
using app::bsp::driver::ft6336u::TouchGesture;
using app::bsp::driver::gdey027t91::Gdey027t91;

constexpr uint32_t kIdlePollMs = 20;
constexpr uint32_t kCoalesceMs = 12;

const char* gesture_name(GestureType type)
{
    switch (type)
    {
        case GestureType::Press:
            return "Press";
        case GestureType::Move:
            return "Move";
        case GestureType::Release:
            return "Release";
        case GestureType::Tap:
            return "Tap";
        case GestureType::LongPress:
            return "LongPress";
        case GestureType::SwipeUp:
            return "SwipeUp";
        case GestureType::SwipeDown:
            return "SwipeDown";
        case GestureType::SwipeLeft:
            return "SwipeLeft";
        case GestureType::SwipeRight:
            return "SwipeRight";
        default:
            return "None";
    }
}

void log_gesture(const GestureEvent& ev)
{
    app::test::log_kv_fmt(TAG, gesture_name(ev.type), "(%u,%u) start(%u,%u) d(%d,%d) %ums",
                          ev.x, ev.y, ev.start_x, ev.start_y, static_cast<int>(ev.dx),
                          static_cast<int>(ev.dy), static_cast<unsigned>(ev.duration_ms));
}

void dispatch_events(TouchGesture& gesture)
{
    while (gesture.has_event())
        log_gesture(gesture.take_event());
}

} // namespace

extern "C" void test_gdey027t91_gesture(void)
{
    app::test::log_section_begin(TAG, "FT6336U 手势检测测试");

    app::bsp::i2c::I2C i2c;
    Ft6336u touch;
    TouchGesture gesture;

    if (!i2c.init())
    {
        app::test::log_kv(TAG, "I2C", "init 失败");
        app::test::log_section_end(TAG);
        return;
    }

    Config tc;
    tc.panel_width = Gdey027t91::WIDTH;
    tc.panel_height = Gdey027t91::HEIGHT;

    if (!touch.init(i2c.get_bus_handle(), &tc))
    {
        app::test::log_kv(TAG, "FT6336U", "init 失败");
        app::test::log_section_end(TAG);
        return;
    }

    if (!touch.enable_interrupt())
        app::test::log_kv(TAG, "INT", "未启用（将依赖轮询）");

    GestureConfig cfg;
    cfg.panel_width = Gdey027t91::WIDTH;
    cfg.panel_height = Gdey027t91::HEIGHT;

    if (!gesture.init(&touch, &cfg))
    {
        app::test::log_kv(TAG, "TouchGesture", "init 失败");
        app::test::log_section_end(TAG);
        return;
    }

    app::test::log_kv_fmt(TAG, "面板", "%u x %u", Gdey027t91::WIDTH, Gdey027t91::HEIGHT);
    app::test::log_kv(TAG, "操作", "轻点 / 滑动 / 长按，串口打印手势");

    while (true)
    {
        if (!touch.wait_interrupt(kIdlePollMs))
        {
            gesture.poll();
            gesture.tick();
            dispatch_events(gesture);

            if (gesture.is_pressed() && !touch.is_touched())
            {
                gesture.process_sample({});
                dispatch_events(gesture);
            }

            while (touch.poll_interrupt())
                ;
            continue;
        }

        do
        {
            gesture.poll();
            dispatch_events(gesture);

            while (touch.poll_interrupt())
            {
                gesture.poll();
                dispatch_events(gesture);
            }
        } while (touch.wait_interrupt(kCoalesceMs));

        gesture.poll();
        dispatch_events(gesture);

        if (gesture.is_pressed() && !touch.is_touched())
        {
            gesture.process_sample({});
            dispatch_events(gesture);
        }

        while (touch.poll_interrupt())
            ;
    }
}
