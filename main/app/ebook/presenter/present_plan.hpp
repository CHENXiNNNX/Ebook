#pragma once

#include <cstdint>

#include "core/geometry.hpp"
#include "router/refresh_intent.hpp"

namespace app::ebook::presenter {

enum class PresentOp : uint8_t
{
    Bootstrap,
    Partial,
    Fast,
    Full,
};

struct PresentStep
{
    PresentOp  op{PresentOp::Partial};
    core::Rect rect{};
};

struct PresentPlan
{
    PresentStep steps[2]{};
    uint8_t     count{0};
};

/** Waveform → Bootstrap? + Partial/Fast/Full 步骤 */
PresentPlan make_plan(router::RefreshIntent intent, bool can_partial);

} // namespace app::ebook::presenter
