#pragma once

#include <cstdint>

#include "ft6336u.hpp"

namespace app::bsp::driver::ft6336u {

enum class GestureType : uint8_t
{
    None = 0,
    Press,
    Move,
    Release,
    Tap,
    LongPress,
    SwipeUp,
    SwipeDown,
    SwipeLeft,
    SwipeRight,
};

struct GestureEvent
{
    GestureType type = GestureType::None;
    uint16_t x = 0;
    uint16_t y = 0;
    uint16_t start_x = 0;
    uint16_t start_y = 0;
    int16_t dx = 0;
    int16_t dy = 0;
    uint32_t duration_ms = 0;
};

struct GestureConfig
{
    uint16_t panel_width = 176;
    uint16_t panel_height = 264;

    int16_t map_offset_x = 0;
    int16_t map_offset_y = 0;
    bool swap_xy = false;
    bool mirror_x = false;
    bool mirror_y = false;

    bool emit_press = false;
    bool emit_move = false;
    bool emit_release = false;

    uint16_t tap_max_move_px = 15;
    uint32_t tap_max_duration_ms = 250;
    uint32_t long_press_ms = 700;
    uint8_t long_press_stable_frames = 4;

    uint16_t swipe_min_distance_px = 28;
    uint16_t swipe_axis_lock_px = 16;
    uint32_t swipe_max_duration_ms = 800;
    uint32_t gesture_cooldown_ms = 150;

    uint16_t max_frame_jump_px = 60;
    uint8_t edge_ignore_px = 0;
};

/** 触摸采样 → 点击 / 长按 / 滑动手势 */
class TouchGesture
{
  public:
    TouchGesture();
    ~TouchGesture() = default;

    TouchGesture(const TouchGesture&) = delete;
    TouchGesture& operator=(const TouchGesture&) = delete;

    bool init(Ft6336u* touch, const GestureConfig* config = nullptr);
    void reset();
    bool poll();
    bool process_sample(const TouchSample& sample);
    void tick();

    bool has_event() const { return pending_.type != GestureType::None; }
    GestureEvent take_event();

    bool is_pressed() const
    {
        return state_ == State::Pressed || state_ == State::LongPressSent;
    }

    const TouchPoint& mapped_point() const { return mapped_; }

  private:
    enum class State : uint8_t
    {
        Idle = 0,
        Pressed,
        LongPressSent,
    };

    void map_point(uint16_t raw_x, uint16_t raw_y, TouchPoint& out) const;
    void push_event(GestureType type, uint16_t x, uint16_t y, uint16_t sx, uint16_t sy,
                    int16_t dx, int16_t dy, uint32_t duration_ms);
    GestureType detect_swipe(int16_t dx, int16_t dy) const;
    void finish_contact(uint32_t now_ms);
    bool try_long_press(uint32_t now_ms);
    bool accept_position(uint16_t x, uint16_t y) const;
    bool finger_up() const;
    uint32_t now_ms() const;

    Ft6336u* touch_;
    GestureConfig cfg_;
    State state_;
    GestureEvent pending_;

    TouchPoint mapped_;
    uint16_t start_x_;
    uint16_t start_y_;
    uint16_t last_x_;
    uint16_t last_y_;
    uint32_t press_start_ms_;
    uint32_t last_gesture_ms_;
    bool long_press_fired_;
    uint8_t stable_frames_;
};

} // namespace app::bsp::driver::ft6336u
