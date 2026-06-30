#include "input/physical_types.hpp"

namespace app::ebook::input {

const char* physical_key_name(PhysicalKey k)
{
    switch (k)
    {
        case PhysicalKey::Up:   return "Up";
        case PhysicalKey::Mid:  return "Mid";
        case PhysicalKey::Down: return "Down";
    }
    return "?";
}

const char* physical_action_name(PhysicalAction a)
{
    switch (a)
    {
        case PhysicalAction::Press:   return "Press";
        case PhysicalAction::Release: return "Release";
    }
    return "?";
}

} // namespace app::ebook::input
