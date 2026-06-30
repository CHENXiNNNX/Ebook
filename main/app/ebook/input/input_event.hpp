#pragma once

#include "ft6336u.hpp"
#include "touch_gesture.hpp"

namespace app::ebook::input {

/** 复用 BSP 的手势事件 ABI（POD，可直接 enqueue） */
using Event         = ::app::bsp::driver::ft6336u::GestureEvent;
using EventType     = ::app::bsp::driver::ft6336u::GestureType;
using GestureConfig = ::app::bsp::driver::ft6336u::GestureConfig;

inline const char* type_name(EventType t)
{
    switch (t)
    {
        case EventType::None:       return "None";
        case EventType::Press:      return "Press";
        case EventType::Move:       return "Move";
        case EventType::Release:    return "Release";
        case EventType::Tap:        return "Tap";
        case EventType::LongPress:  return "LongPress";
        case EventType::SwipeUp:    return "SwipeUp";
        case EventType::SwipeDown:  return "SwipeDown";
        case EventType::SwipeLeft:  return "SwipeLeft";
        case EventType::SwipeRight: return "SwipeRight";
    }
    return "?";
}

inline bool is_swipe(EventType t)
{
    return t == EventType::SwipeUp   || t == EventType::SwipeDown ||
           t == EventType::SwipeLeft || t == EventType::SwipeRight;
}

inline bool is_tap_like(EventType t)
{
    return t == EventType::Tap || t == EventType::LongPress;
}

} // namespace app::ebook::input
