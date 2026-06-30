#include "test_gdey027t91.hpp"

#include <cstring>

#include "backlight.hpp"
#include "framebuffer.hpp"
#include "ft6336u.hpp"
#include "gdey027t91.hpp"
#include "i2c/i2c.hpp"
#include "system/task/task.hpp"
#include "test_log.hpp"
#include "touch_gesture.hpp"

namespace {

const char* const TAG = "test_gdey027t91_touch";

using app::bsp::driver::ft6336u::Ft6336u;
using app::bsp::driver::ft6336u::GestureConfig;
using app::bsp::driver::ft6336u::GestureEvent;
using app::bsp::driver::ft6336u::GestureType;
using app::bsp::driver::ft6336u::TouchGesture;
using app::bsp::driver::gdey027t91::Backlight;
using app::bsp::driver::gdey027t91::Framebuffer;
using app::bsp::driver::gdey027t91::Gdey027t91;
using app::bsp::driver::gdey027t91::Present;
using app::bsp::driver::gdey027t91::Window;

constexpr uint16_t kW = Gdey027t91::WIDTH;
constexpr uint16_t kH = Gdey027t91::HEIGHT;

constexpr uint16_t kTopBarY = 244;
constexpr uint16_t kTopBarH = 20;

constexpr uint16_t kChessY = 192;
constexpr uint16_t kChessH = 48;
constexpr uint16_t kChessCell = 16;

constexpr uint16_t kBrightY = 128;
constexpr uint16_t kBrightH = 56;

constexpr uint16_t kBtnY = 16;
constexpr uint16_t kBtnH = 56;

constexpr uint16_t kNumX = 4;
constexpr uint16_t kNumFontH = 32;
constexpr uint16_t kNumY = kBrightY + ((kBrightH - kNumFontH) / 2);

constexpr uint16_t kBarX = 56;
constexpr uint16_t kBarW = 112;
constexpr uint16_t kBarBorderH = 24;
constexpr uint16_t kBarY = kBrightY + ((kBrightH - kBarBorderH) / 2);
constexpr uint16_t kBarBd = 2;
constexpr uint16_t kBarInX = kBarX + kBarBd;
constexpr uint16_t kBarInY = kBarY + kBarBd;
constexpr uint16_t kBarInW = kBarW - (kBarBd * 2);
constexpr uint16_t kBarInH = kBarBorderH - (kBarBd * 2);

constexpr uint16_t kBtnPad = 8;
constexpr uint16_t kBtnEachW = (kW - (kBtnPad * 3)) / 2;
constexpr uint16_t kBtnUpX = kBtnPad;
constexpr uint16_t kBtnDnX = (kBtnPad * 2) + kBtnEachW;
constexpr uint16_t kBtnEachY = kBtnY + 4;
constexpr uint16_t kBtnEachH = kBtnH - 8;

constexpr uint16_t kArrowBase = 24;
constexpr uint16_t kArrowH = 16;

constexpr uint8_t kBrightInit = 50;
constexpr uint8_t kBrightStep = 10;

const uint8_t kFont[10][16] = {
    {0x00, 0x00, 0x3C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00, 0x00, 0x00,
     0x00},
    {0x00, 0x00, 0x18, 0x38, 0x78, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00, 0x00, 0x00,
     0x00},
    {0x00, 0x00, 0x3C, 0x66, 0x06, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x66, 0x7E, 0x00, 0x00, 0x00,
     0x00},
    {0x00, 0x00, 0x3C, 0x66, 0x06, 0x06, 0x1C, 0x06, 0x06, 0x06, 0x66, 0x3C, 0x00, 0x00, 0x00,
     0x00},
    {0x00, 0x00, 0x0C, 0x1C, 0x3C, 0x6C, 0x6C, 0x7E, 0x0C, 0x0C, 0x0C, 0x1E, 0x00, 0x00, 0x00,
     0x00},
    {0x00, 0x00, 0x7E, 0x60, 0x60, 0x7C, 0x06, 0x06, 0x06, 0x06, 0x66, 0x3C, 0x00, 0x00, 0x00,
     0x00},
    {0x00, 0x00, 0x3C, 0x66, 0x60, 0x7C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00, 0x00, 0x00,
     0x00},
    {0x00, 0x00, 0x7E, 0x66, 0x06, 0x0C, 0x0C, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00, 0x00, 0x00,
     0x00},
    {0x00, 0x00, 0x3C, 0x66, 0x66, 0x66, 0x3C, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00, 0x00, 0x00,
     0x00},
    {0x00, 0x00, 0x3C, 0x66, 0x66, 0x66, 0x3E, 0x06, 0x06, 0x06, 0x66, 0x3C, 0x00, 0x00, 0x00,
     0x00},
};

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

void draw_checkerboard(uint8_t* fb)
{
    uint8_t pat_a[Gdey027t91::STRIDE];
    uint8_t pat_b[Gdey027t91::STRIDE];

    const uint16_t cells = kW / kChessCell;
    for (uint16_t c = 0; c < cells; c++)
    {
        const uint8_t va = ((c & 1) != 0) ? 0xFF : 0x00;
        const uint16_t byte_off = (c * kChessCell) / 8;
        const uint16_t byte_cnt = kChessCell / 8;
        std::memset(pat_a + byte_off, va, byte_cnt);
        std::memset(pat_b + byte_off, static_cast<uint8_t>(va ^ 0xFF), byte_cnt);
    }

    for (uint16_t y = kChessY; y < kChessY + kChessH && y < kH; y++)
    {
        const uint16_t cell_row = (y - kChessY) / kChessCell;
        std::memcpy(fb + (y * Gdey027t91::STRIDE), ((cell_row & 1) != 0) ? pat_b : pat_a,
                    Gdey027t91::STRIDE);
    }
}

void draw_digit_2x(uint8_t* fb, uint16_t px, uint16_t py, uint8_t d)
{
    if (d > 9)
        return;
    for (uint8_t row = 0; row < 16; row++)
    {
        const uint8_t bits = kFont[d][row];
        for (uint8_t col = 0; col < 8; col++)
        {
            const bool ink = ((bits >> (7 - col)) & 1) != 0;
            const auto x = static_cast<int16_t>(px + (col * 2));
            const auto y = static_cast<int16_t>(py + ((15 - row) * 2));
            Framebuffer::set_pixel(fb, x, y, ink);
            Framebuffer::set_pixel(fb, static_cast<int16_t>(x + 1), y, ink);
            Framebuffer::set_pixel(fb, x, static_cast<int16_t>(y + 1), ink);
            Framebuffer::set_pixel(fb, static_cast<int16_t>(x + 1), static_cast<int16_t>(y + 1),
                                   ink);
        }
    }
}

void draw_brightness_area(uint8_t* fb, uint8_t val)
{
    Framebuffer::fill_rect(fb, 0, kBrightY, kW, kBrightH, false);

    if (val >= 100)
    {
        draw_digit_2x(fb, kNumX, kNumY, 1);
        draw_digit_2x(fb, kNumX + 16, kNumY, 0);
        draw_digit_2x(fb, kNumX + 32, kNumY, 0);
    }
    else if (val >= 10)
    {
        draw_digit_2x(fb, kNumX, kNumY, static_cast<uint8_t>(val / 10));
        draw_digit_2x(fb, kNumX + 16, kNumY, static_cast<uint8_t>(val % 10));
    }
    else
    {
        draw_digit_2x(fb, kNumX, kNumY, val);
    }

    Framebuffer::draw_border(fb, kBarX, kBarY, kBarW, kBarBorderH, kBarBd);

    const uint16_t filled = static_cast<uint16_t>((static_cast<uint32_t>(val) * kBarInW) / 100);
    if (filled > 0)
        Framebuffer::fill_rect(fb, kBarInX, kBarInY, filled, kBarInH, true);
}

void draw_triangle(uint8_t* fb, uint16_t cx, uint16_t top_y, bool up)
{
    for (uint16_t r = 0; r < kArrowH; r++)
    {
        const uint16_t progress = up ? (kArrowH - 1 - r) : r;
        const uint16_t half = (progress * kArrowBase) / (2 * kArrowH);
        for (auto c = static_cast<int16_t>(cx - half); c <= static_cast<int16_t>(cx + half); c++)
            Framebuffer::set_pixel(fb, c, static_cast<int16_t>(top_y + r), true);
    }
}

void draw_buttons(uint8_t* fb)
{
    Framebuffer::draw_border(fb, kBtnUpX, kBtnEachY, kBtnEachW, kBtnEachH);
    draw_triangle(fb, kBtnUpX + (kBtnEachW / 2), kBtnEachY + ((kBtnEachH - kArrowH) / 2), true);

    Framebuffer::draw_border(fb, kBtnDnX, kBtnEachY, kBtnEachW, kBtnEachH);
    draw_triangle(fb, kBtnDnX + (kBtnEachW / 2), kBtnEachY + ((kBtnEachH - kArrowH) / 2), false);
}

void draw_full_ui(uint8_t* fb, uint8_t brightness)
{
    Framebuffer::fill(fb, Gdey027t91::BUFFER_SIZE, Framebuffer::kWhite);
    Framebuffer::fill_rect(fb, 0, kTopBarY, kW, kTopBarH, true);
    draw_checkerboard(fb);
    draw_brightness_area(fb, brightness);
    draw_buttons(fb);
}

bool hit_button_area(const GestureEvent& ev)
{
    return ev.y >= kBtnY && ev.y < (kBtnY + kBtnH);
}

bool handle_brightness_tap(const GestureEvent& ev, uint8_t& brightness, Backlight& bl, uint8_t* fb,
                           Gdey027t91& epd)
{
    if (ev.type != GestureType::Tap || !hit_button_area(ev))
    {
        return false;
    }

    const uint8_t old_val = brightness;
    if (ev.x < kW / 2)
    {
        brightness = (brightness <= 100 - kBrightStep) ? static_cast<uint8_t>(brightness + kBrightStep)
                                                       : 100;
    }
    else
    {
        brightness = (brightness >= kBrightStep) ? static_cast<uint8_t>(brightness - kBrightStep) : 0;
    }

    if (brightness == old_val)
    {
        return false;
    }

    bl.set_brightness(brightness);
    app::test::log_kv_fmt(TAG, gesture_name(ev.type), "(%u,%u) 亮度 %u%%", ev.x, ev.y, brightness);

    draw_brightness_area(fb, brightness);
    Window win{0, kBrightY, kW, kBrightH};
    if (!epd.present(Present::Partial, win))
    {
        app::test::log_kv(TAG, "电子纸", "局刷失败");
    }
    return true;
}

void drain_gesture_events(TouchGesture& gesture, uint8_t& brightness, Backlight& bl, uint8_t* fb,
                          Gdey027t91& epd)
{
    while (gesture.has_event())
    {
        handle_brightness_tap(gesture.take_event(), brightness, bl, fb, epd);
    }
}

} // namespace

extern "C" void test_gdey027t91_touch(void)
{
    app::test::log_section_begin(TAG, "GDEY027T91 触摸 UI 测试");

    Gdey027t91 epd;
    if (!epd.init())
    {
        app::test::log_kv(TAG, "电子纸", "init 失败");
        app::test::log_section_end(TAG);
        return;
    }

    Backlight bl;
    if (!bl.init())
        app::test::log_kv(TAG, "背光", "init 失败（继续运行）");

    app::bsp::i2c::I2C i2c;
    Ft6336u touch;
    TouchGesture gesture;
    bool touch_ok = false;

    if (i2c.init())
    {
        touch_ok = touch.init(i2c.get_bus_handle());
    }

    if (touch_ok)
    {
        touch.enable_interrupt();

        GestureConfig gcfg;
        gcfg.panel_width = kW;
        gcfg.panel_height = kH;
        touch_ok = gesture.init(&touch, &gcfg);
    }

    if (!touch_ok)
        app::test::log_kv(TAG, "触摸", "init 失败（无交互）");

    uint8_t brightness = kBrightInit;
    bl.set_brightness(brightness);

    uint8_t* fb = epd.fb();
    if (fb == nullptr)
    {
        app::test::log_kv(TAG, "帧缓冲", "为空");
        app::test::log_section_end(TAG);
        return;
    }

    draw_full_ui(fb, brightness);
    if (!epd.present(Present::Base))
    {
        app::test::log_kv(TAG, "电子纸", "Base 刷新失败");
        app::test::log_section_end(TAG);
        return;
    }

    app::test::log_kv_fmt(TAG, "状态", "UI 已显示，亮度 %u%%", brightness);
    app::test::log_kv(TAG, "操作", "点击 [▲]/[▼] 调节亮度；支持 Tap / Swipe / LongPress");

    while (true)
    {
        if (!touch_ok)
        {
            app::sys::task::TaskMgr::delay_ms(100);
            continue;
        }

        if (!touch.wait_interrupt(50))
        {
            gesture.poll();
            drain_gesture_events(gesture, brightness, bl, fb, epd);
            continue;
        }

        do
        {
            gesture.poll();
            drain_gesture_events(gesture, brightness, bl, fb, epd);

            while (touch.poll_interrupt())
            {
                gesture.poll();
                drain_gesture_events(gesture, brightness, bl, fb, epd);
            }
        } while (touch.wait_interrupt(12));

        gesture.poll();
        drain_gesture_events(gesture, brightness, bl, fb, epd);
    }
}
