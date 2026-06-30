#include "shell/app_grid_page.hpp"

#include "apps/app_registry.hpp"
#include "gfx/canvas.hpp"
#include "overlays/control_center.hpp"
#include "router/page_id.hpp"
#include "router/refresh_intent.hpp"
#include "router/router.hpp"
#include "ui/strings.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

namespace app::ebook::shell {

namespace {

using ui::Theme;

core::Rect viewport_rect()
{
    return core::Rect{
        0, static_cast<int16_t>(Theme::kAppGridY),
        Theme::kScreenW,
        static_cast<uint16_t>(Theme::kScreenH - Theme::kAppGridY)};
}

uint8_t visible_rows()
{
    const uint16_t r = viewport_rect().h / Theme::kAppGridCellH;
    return (r == 0) ? 1U : static_cast<uint8_t>(r);
}

uint8_t total_rows(uint8_t count)
{
    return static_cast<uint8_t>((count + Theme::kAppGridCols - 1) / Theme::kAppGridCols);
}

uint8_t max_scroll(uint8_t count)
{
    const uint8_t t = total_rows(count);
    const uint8_t v = visible_rows();
    return (t > v) ? static_cast<uint8_t>(t - v) : 0;
}

} // namespace

AppGridPage& AppGridPage::instance()
{
    static AppGridPage s;
    return s;
}

void AppGridPage::on_enter()
{
    scroll_row_ = 0;
}

void AppGridPage::paint(gfx::Canvas& c)
{
    ui::widgets::toolbar(c, ui::strings::kAppsTitle);

    auto&         reg   = apps::AppRegistry::instance();
    const uint8_t count = reg.entry_count();
    if (count == 0)
        return;

    const uint8_t vis  = visible_rows();
    const uint8_t rmax = max_scroll(count);
    if (scroll_row_ > rmax)
        scroll_row_ = rmax;

    const core::Rect vp = viewport_rect();
    const bool needs_scroll = total_rows(count) > vis;
    const uint16_t reserve  = needs_scroll ? 6U : 0U;
    const core::Rect view_area{
        vp.x, vp.y,
        static_cast<uint16_t>(vp.w - reserve),
        static_cast<uint16_t>(vis * Theme::kAppGridCellH)};

    for (uint8_t vr = 0; vr < vis; ++vr)
    {
        for (uint8_t col = 0; col < Theme::kAppGridCols; ++col)
        {
            const uint8_t real_row = static_cast<uint8_t>(scroll_row_ + vr);
            const uint8_t idx =
                static_cast<uint8_t>(real_row * Theme::kAppGridCols + col);
            if (idx >= count)
                break;

            const uint8_t view_idx = static_cast<uint8_t>(vr * Theme::kAppGridCols + col);
            const core::Rect cell = ui::widgets::grid_cell(
                view_area, Theme::kAppGridCols, vis, view_idx, Theme::kPad);

            const apps::AppEntry e = reg.entry_at(idx);
            c.rect(cell);

            if (e.icon_cp != 0)
            {
                const int16_t ix =
                    static_cast<int16_t>(cell.x + (cell.w - Theme::kAppIconSize) / 2);
                const int16_t iy = static_cast<int16_t>(cell.y + 6);
                c.glyph(ix, iy, e.icon_cp, Theme::kAppIconSize, gfx::FontFace::Icon);
            }

            const int16_t lbl_y =
                static_cast<int16_t>(cell.y + 6 + Theme::kAppIconSize + 2);
            const uint16_t lbl_h =
                static_cast<uint16_t>(cell.h - static_cast<uint16_t>(lbl_y - cell.y) - 2);
            const core::Rect lbl{cell.x, lbl_y, cell.w, lbl_h};
            gfx::Canvas::TextStyle s{};
            s.size_px = Theme::kFontSmall;
            s.h       = gfx::HAlign::Center;
            s.v       = gfx::VAlign::Top;
            s.padding = 2;
            c.text_in(lbl, e.title, s);
        }
    }

    if (needs_scroll)
    {
        ui::widgets::scrollbar(c,
                               static_cast<uint16_t>(vp.y), vp.h,
                               scroll_row_, total_rows(count), vis);
    }
}

InputResult AppGridPage::on_input(const ::app::ebook::input::Event& ev)
{
    using ::app::ebook::input::EventType;
    auto& reg = apps::AppRegistry::instance();
    auto& r   = router::Router::instance();

    if (ev.type == EventType::SwipeDown &&
        ev.start_y <= Theme::kStatusBarH)
    {
        overlays::ControlCenter::instance().open();
        return {true};
    }

    const uint8_t count = reg.entry_count();
    const uint8_t vis   = visible_rows();
    const uint8_t rmax  = max_scroll(count);

    if (ev.start_y > Theme::kStatusBarH + Theme::kToolbarH)
    {
        if (ev.type == EventType::SwipeUp && scroll_row_ < rmax)
        {
            ++scroll_row_;
            (void)r.repaint(router::intent_partial_full());
            return {true};
        }
        if (ev.type == EventType::SwipeDown && scroll_row_ > 0)
        {
            --scroll_row_;
            (void)r.repaint(router::intent_partial_full());
            return {true};
        }
    }

    if (ev.type == EventType::Tap &&
        ui::widgets::hit_toolbar_back(static_cast<int16_t>(ev.x), static_cast<int16_t>(ev.y)))
    {
        (void)r.back();
        return {true};
    }

    if (ev.type != EventType::Tap)
        return {};

    const core::Rect vp = viewport_rect();
    const core::Rect view_area{
        vp.x, vp.y, vp.w,
        static_cast<uint16_t>(vis * Theme::kAppGridCellH)};
    const int8_t view_idx = ui::widgets::grid_hit_test(
        view_area, Theme::kAppGridCols, vis,
        static_cast<int16_t>(ev.x), static_cast<int16_t>(ev.y), Theme::kPad);
    if (view_idx < 0)
        return {};

    const uint8_t vr  = static_cast<uint8_t>(view_idx / Theme::kAppGridCols);
    const uint8_t col = static_cast<uint8_t>(view_idx % Theme::kAppGridCols);
    const uint8_t real_idx =
        static_cast<uint8_t>((scroll_row_ + vr) * Theme::kAppGridCols + col);
    if (real_idx >= count)
        return {};

    const apps::AppEntry e = reg.entry_at(real_idx);
    if (e.id == apps::AppId::None)
        return {};

    (void)r.navigate(router::page_app(e.id, 0));
    return {true};
}

} // namespace app::ebook::shell
