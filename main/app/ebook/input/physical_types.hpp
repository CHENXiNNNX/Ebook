#pragma once

#include <cstdint>

namespace app::ebook::input {

/** 拨码档位（与 BSP DipId 对应，ebook 层抽象） */
enum class PhysicalKey : uint8_t
{
    Up = 0,
    Mid,
    Down,
};

enum class PhysicalAction : uint8_t
{
    Press = 0,
    Release,
};

struct PhysicalEvent
{
    PhysicalKey    key{PhysicalKey::Up};
    PhysicalAction action{PhysicalAction::Press};
};

const char* physical_key_name(PhysicalKey k);
const char* physical_action_name(PhysicalAction a);

} // namespace app::ebook::input
