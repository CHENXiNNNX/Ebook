#pragma once

#include <cstdint>

#include "router/refresh_intent.hpp"
#include "router/transition.hpp"

namespace app::ebook::presenter {

/** 提交给 Presenter 的一帧请求 */
struct FrameRequest
{
    router::Transition    transition{};
    router::RefreshIntent intent{};
    uint32_t              seq{0};
};

} // namespace app::ebook::presenter
