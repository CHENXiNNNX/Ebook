#pragma once

#include <cstdint>

#include "input/physical_types.hpp"

namespace app::ebook::input {

enum class InputContext : uint8_t
{
    Global = 0,
    Reader,
    List,
};

enum class SemanticAction : uint8_t
{
    None = 0,
    PageNext,
    PagePrev,
    ListUp,
    ListDown,
    Back,
    BrightnessUp,
    BrightnessDown,
    VolumeUp,
    VolumeDown,
    Menu,
    Home,
};

InputContext    detect_context();
SemanticAction  resolve_physical(PhysicalKey key, PhysicalAction action, InputContext ctx);

const char* semantic_action_name(SemanticAction a);

} // namespace app::ebook::input
