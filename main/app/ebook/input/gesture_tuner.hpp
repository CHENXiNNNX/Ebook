#pragma once

#include <cstdint>

#include "input/input_event.hpp"

namespace app::ebook::input {

enum class Profile : uint8_t
{
    Normal = 0,
    Keyboard,
    Drawing,
};

inline constexpr uint16_t kSwipeDisabledPx    = UINT16_MAX;
inline constexpr uint32_t kLongPressDisabledMs = UINT32_MAX;

GestureConfig make_config(Profile p);

} // namespace app::ebook::input
