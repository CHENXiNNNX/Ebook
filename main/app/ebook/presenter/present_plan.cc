#include "presenter/present_plan.hpp"

namespace app::ebook::presenter {

PresentPlan make_plan(router::RefreshIntent intent, bool can_partial)
{
    PresentPlan plan{};

    auto push = [&](PresentOp op, const core::Rect& rect = core::Rect::full()) {
        if (plan.count < 2)
        {
            plan.steps[plan.count].op   = op;
            plan.steps[plan.count].rect = rect;
            ++plan.count;
        }
    };

    switch (intent.waveform)
    {
        case router::Waveform::Partial:
            if (!can_partial)
                push(PresentOp::Bootstrap);
            push(PresentOp::Partial, core::Rect::full());
            break;
        case router::Waveform::Fast:
            if (!can_partial)
                push(PresentOp::Bootstrap);
            push(PresentOp::Fast);
            break;
        case router::Waveform::Full:
            push(PresentOp::Full);
            break;
    }

    return plan;
}

} // namespace app::ebook::presenter
