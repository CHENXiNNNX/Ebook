#include "touch_gesture.hpp"

#include <algorithm>
#include <cstdlib>

#include <esp_timer.h>

namespace app::bsp::driver::ft6336u {

namespace {

bool is_final_gesture(GestureType type)
{
    switch (type)
    {
        case GestureType::Tap:
        case GestureType::LongPress:
        case GestureType::SwipeUp:
        case GestureType::SwipeDown:
        case GestureType::SwipeLeft:
        case GestureType::SwipeRight:
            return true;
        default:
            return false;
    }
}

} // namespace

TouchGesture::TouchGesture()
    : touch_(nullptr)
    , state_(State::Idle)
    , start_x_(0)
    , start_y_(0)
    , last_x_(0)
    , last_y_(0)
    , press_start_ms_(0)
    , last_gesture_ms_(0)
    , long_press_fired_(false)
    , stable_frames_(0)
{
}

bool TouchGesture::init(Ft6336u* touch, const GestureConfig* config)
{
    if (touch == nullptr || !touch->is_init())
        return false;

    touch_ = touch;
    if (config != nullptr)
        cfg_ = *config;

    reset();
    return true;
}

void TouchGesture::reset()
{
    state_ = State::Idle;
    pending_ = {};
    mapped_ = {};
    start_x_ = 0;
    start_y_ = 0;
    last_x_ = 0;
    last_y_ = 0;
    press_start_ms_ = 0;
    long_press_fired_ = false;
    stable_frames_ = 0;
}

uint32_t TouchGesture::now_ms() const
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

void TouchGesture::map_point(uint16_t raw_x, uint16_t raw_y, TouchPoint& out) const
{
    // swap/mirror 由 esp_lcd_touch（Ft6336u）完成，此处仅偏移与裁边
    int32_t x = static_cast<int32_t>(raw_x) + cfg_.map_offset_x;
    int32_t y = static_cast<int32_t>(raw_y) + cfg_.map_offset_y;

    x = std::max<int32_t>(0, std::min<int32_t>(x, cfg_.panel_width - 1));
    y = std::max<int32_t>(0, std::min<int32_t>(y, cfg_.panel_height - 1));

    out.x = static_cast<uint16_t>(x);
    out.y = static_cast<uint16_t>(y);
}

void TouchGesture::push_event(GestureType type, uint16_t x, uint16_t y, uint16_t sx, uint16_t sy,
                              int16_t dx, int16_t dy, uint32_t duration_ms)
{
    pending_ = {type, x, y, sx, sy, dx, dy, duration_ms};

    if (is_final_gesture(type))
        last_gesture_ms_ = now_ms();
}

GestureEvent TouchGesture::take_event()
{
    const GestureEvent ev = pending_;
    pending_ = {};
    return ev;
}

bool TouchGesture::finger_up() const
{
    return touch_ == nullptr || !touch_->is_touched();
}

bool TouchGesture::accept_position(uint16_t x, uint16_t y) const
{
    if (cfg_.edge_ignore_px > 0)
    {
        const uint16_t max_x = static_cast<uint16_t>(cfg_.panel_width - 1);
        const uint16_t max_y = static_cast<uint16_t>(cfg_.panel_height - 1);
        if (x <= cfg_.edge_ignore_px || x >= static_cast<uint16_t>(max_x - cfg_.edge_ignore_px) ||
            y <= cfg_.edge_ignore_px || y >= static_cast<uint16_t>(max_y - cfg_.edge_ignore_px))
        {
            return false;
        }
    }

    if (state_ == State::Idle)
        return true;

    const int32_t dx = std::abs(static_cast<int32_t>(x) - static_cast<int32_t>(last_x_));
    const int32_t dy = std::abs(static_cast<int32_t>(y) - static_cast<int32_t>(last_y_));
    if (dx + dy > static_cast<int32_t>(cfg_.max_frame_jump_px))
        return false;

    return true;
}

GestureType TouchGesture::detect_swipe(int16_t dx, int16_t dy) const
{
    const uint16_t adx = static_cast<uint16_t>(std::abs(dx));
    const uint16_t ady = static_cast<uint16_t>(std::abs(dy));

    if (adx < cfg_.swipe_min_distance_px && ady < cfg_.swipe_min_distance_px)
    {
        return GestureType::None;
    }

    if (adx >= ady)
    {
        if (adx < cfg_.swipe_axis_lock_px)
            return GestureType::None;
        return (dx > 0) ? GestureType::SwipeRight : GestureType::SwipeLeft;
    }

    if (ady < cfg_.swipe_axis_lock_px)
        return GestureType::None;
    return (dy > 0) ? GestureType::SwipeDown : GestureType::SwipeUp;
}

void TouchGesture::finish_contact(uint32_t t)
{
    const uint32_t duration = t - press_start_ms_;
    const int16_t dx = static_cast<int16_t>(last_x_ - start_x_);
    const int16_t dy = static_cast<int16_t>(last_y_ - start_y_);
    const int16_t adx = static_cast<int16_t>(std::abs(dx));
    const int16_t ady = static_cast<int16_t>(std::abs(dy));

    GestureType type = GestureType::None;

    if (!long_press_fired_ && (t - last_gesture_ms_) >= cfg_.gesture_cooldown_ms)
    {
        if (duration <= cfg_.swipe_max_duration_ms)
            type = detect_swipe(dx, dy);

        if (type == GestureType::None && adx <= static_cast<int16_t>(cfg_.tap_max_move_px) &&
            ady <= static_cast<int16_t>(cfg_.tap_max_move_px) && duration <= cfg_.tap_max_duration_ms)
        {
            type = GestureType::Tap;
        }
    }

    if (type != GestureType::None)
        push_event(type, last_x_, last_y_, start_x_, start_y_, dx, dy, duration);
    else if (cfg_.emit_release)
        push_event(GestureType::Release, last_x_, last_y_, start_x_, start_y_, dx, dy, duration);

    state_ = State::Idle;
    long_press_fired_ = false;
}

bool TouchGesture::try_long_press(uint32_t t)
{
    if (long_press_fired_ || state_ != State::Pressed)
        return false;

    const int16_t dx = static_cast<int16_t>(last_x_ - start_x_);
    const int16_t dy = static_cast<int16_t>(last_y_ - start_y_);
    const int16_t adx = static_cast<int16_t>(std::abs(dx));
    const int16_t ady = static_cast<int16_t>(std::abs(dy));
    const uint32_t held = t - press_start_ms_;

    if (held < cfg_.long_press_ms || adx > static_cast<int16_t>(cfg_.tap_max_move_px) ||
        ady > static_cast<int16_t>(cfg_.tap_max_move_px) ||
        stable_frames_ < cfg_.long_press_stable_frames)
    {
        return false;
    }

    long_press_fired_ = true;
    state_ = State::LongPressSent;
    push_event(GestureType::LongPress, last_x_, last_y_, start_x_, start_y_, dx, dy, held);
    return true;
}

void TouchGesture::tick()
{
    if (pending_.type != GestureType::None)
        return;

    const uint32_t t = now_ms();

    if (state_ != State::Idle)
    {
        if (finger_up())
        {
            finish_contact(t);
            return;
        }
        if ((t - press_start_ms_) > 3000)
        {
            finish_contact(t);
            return;
        }
    }

    if (state_ != State::Pressed || long_press_fired_)
        return;

    try_long_press(t);
}

bool TouchGesture::process_sample(const TouchSample& sample)
{
    if (pending_.type != GestureType::None)
        return true;

    const uint32_t t = now_ms();

    if (sample.count == 0)
    {
        if (state_ == State::Idle)
            return false;
        if (!finger_up())
            return false;
        finish_contact(t);
        return pending_.type != GestureType::None;
    }

    map_point(sample.points[0].x, sample.points[0].y, mapped_);
    if (!accept_position(mapped_.x, mapped_.y))
        return false;

    if (state_ == State::Idle)
    {
        state_ = State::Pressed;
        start_x_ = last_x_ = mapped_.x;
        start_y_ = last_y_ = mapped_.y;
        press_start_ms_ = t;
        long_press_fired_ = false;
        stable_frames_ = 1;

        if (cfg_.emit_press)
        {
            push_event(GestureType::Press, mapped_.x, mapped_.y, start_x_, start_y_, 0, 0, 0);
            return true;
        }
        return false;
    }

    const int32_t dfx = std::abs(static_cast<int32_t>(mapped_.x) - static_cast<int32_t>(last_x_));
    const int32_t dfy = std::abs(static_cast<int32_t>(mapped_.y) - static_cast<int32_t>(last_y_));
    if (dfx <= 1 && dfy <= 1)
        stable_frames_ = static_cast<uint8_t>(std::min<uint16_t>(255, stable_frames_ + 1));
    else
        stable_frames_ = 0;

    last_x_ = mapped_.x;
    last_y_ = mapped_.y;

    const int16_t dx = static_cast<int16_t>(mapped_.x - start_x_);
    const int16_t dy = static_cast<int16_t>(mapped_.y - start_y_);
    const int16_t adx = static_cast<int16_t>(std::abs(dx));
    const int16_t ady = static_cast<int16_t>(std::abs(dy));
    const uint32_t held = t - press_start_ms_;

    if (try_long_press(t))
        return true;

    if (cfg_.emit_move && state_ == State::Pressed && (adx > 2 || ady > 2))
    {
        push_event(GestureType::Move, mapped_.x, mapped_.y, start_x_, start_y_, dx, dy, held);
        return true;
    }

    return false;
}

bool TouchGesture::poll()
{
    if (touch_ == nullptr || pending_.type != GestureType::None)
        return pending_.type != GestureType::None;

    TouchSample sample = {};
    if (!touch_->read(sample))
        return false;

    return process_sample(sample);
}

} // namespace app::bsp::driver::ft6336u
