#include "ui/list_view.hpp"

#include "ui/theme.hpp"

namespace app::ebook::ui {

namespace {

core::Rect effective_area(const core::Rect& a)
{
    return a.empty() ? Theme::list_region() : a;
}

uint8_t visible_rows(const core::Rect& a)
{
    if (a.h == 0) return Theme::kListVisibleRows;
    return static_cast<uint8_t>(a.h / Theme::kListRowH);
}

uint8_t cap_scroll(uint8_t total, uint8_t visible)
{
    return (total > visible) ? static_cast<uint8_t>(total - visible) : 0;
}

} // namespace

void ListView::set_total(uint8_t total)
{
    total_ = total;
    const uint8_t mx = cap_scroll(total_, visible_rows(effective_area(area_)));
    if (scroll_ > mx) scroll_ = mx;
}

void ListView::set_scroll(uint8_t s)
{
    const uint8_t mx = cap_scroll(total_, visible_rows(effective_area(area_)));
    scroll_ = (s > mx) ? mx : s;
}

void ListView::paint(gfx::Canvas& c)
{
    if (!provider_) return;
    const core::Rect area = effective_area(area_);
    if (area.empty()) return;

    const uint8_t visible = visible_rows(area);
    const uint8_t end     = static_cast<uint8_t>(scroll_ + visible);

    int16_t y = area.y;
    for (uint8_t i = scroll_; i < end && i < total_; ++i)
    {
        widgets::RowStyle s{};
        provider_(i, s);

        const core::Rect row{area.x, y,
                             static_cast<uint16_t>(area.w - Theme::kScrollbarW - 2),
                             Theme::kListRowH};
        widgets::list_row(c, row, s);
        y = static_cast<int16_t>(y + Theme::kListRowH);
    }

    widgets::scrollbar(c, static_cast<uint16_t>(area.y), area.h,
                       scroll_, total_, visible);
}

ListView::InputOutcome ListView::handle_input(const ::app::ebook::input::Event& ev)
{
    using ::app::ebook::input::EventType;

    InputOutcome out{};
    const core::Rect area = effective_area(area_);
    const uint8_t visible = visible_rows(area);

    if (ev.type == EventType::Tap)
    {
        const int16_t x = static_cast<int16_t>(ev.x);
        const int16_t y = static_cast<int16_t>(ev.y);
        if (!area.contains(x, y)) return out;

        const uint8_t local = static_cast<uint8_t>((y - area.y) / Theme::kListRowH);
        if (local >= visible) return out;

        const uint8_t idx = static_cast<uint8_t>(scroll_ + local);
        if (idx >= total_) return out;

        out.consumed     = true;
        out.tap_consumed = true;
        if (tap_) tap_(idx);
        return out;
    }

    if (ev.type == EventType::SwipeUp || ev.type == EventType::SwipeDown)
    {
        if (!area.contains(static_cast<int16_t>(ev.start_x),
                           static_cast<int16_t>(ev.start_y)))
            return out;

        const uint8_t mx  = cap_scroll(total_, visible);
        const uint8_t old = scroll_;
        if (ev.type == EventType::SwipeUp   && scroll_ < mx) ++scroll_;
        else if (ev.type == EventType::SwipeDown && scroll_ > 0) --scroll_;

        out.consumed       = true;
        out.scroll_changed = (scroll_ != old);
        return out;
    }

    return out;
}

} // namespace app::ebook::ui
