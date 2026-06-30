#pragma once

#include "router/page_id.hpp"

namespace app::ebook::router {

enum class NavAction : uint8_t
{
    Forward,
    Back,
    Replace,
    Repaint,
};

struct Transition
{
    PageId    from{};
    PageId    to{};
    NavAction action{NavAction::Forward};
};

const char* action_name(NavAction a);

} // namespace app::ebook::router
