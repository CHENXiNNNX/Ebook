#include "router/transition.hpp"

namespace app::ebook::router {

const char* action_name(NavAction a)
{
    switch (a)
    {
        case NavAction::Forward: return "FWD";
        case NavAction::Back:    return "BACK";
        case NavAction::Replace: return "REPLACE";
        case NavAction::Repaint: return "REPAINT";
    }
    return "?";
}

} // namespace app::ebook::router
