#include "shell/home_page.hpp"

#include <cstdio>

#include "apps/app.hpp"
#include "apps/reader/reader_app.hpp"
#include "apps/reader/reading_store.hpp"
#include "gfx/icon.hpp"
#include "overlays/control_center.hpp"
#include "router/page_id.hpp"
#include "router/router.hpp"
#include "ui/strings.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

namespace app::ebook::shell {

namespace {

using ui::Theme;
using apps::AppId;

struct QuickItem
{
    const char* label;
    uint32_t    icon;
    AppId       target;
};

constexpr QuickItem kQuick[] = {
    {ui::strings::kAppReader,  gfx::icon::kFaBookReader, AppId::Reader},
    {ui::strings::kAppNotepad, gfx::icon::kFaStickyNote, AppId::Notepad},
    {ui::strings::kAppFish,    gfx::icon::kEbWoodenFish, AppId::WoodenFish},
    {ui::strings::kAppMore,    gfx::icon::kFaEllipsisH,  AppId::None},
};

core::Rect reading_card_rect()
{
    return core::Rect{Theme::kPadLg,
                      static_cast<int16_t>(Theme::kReadingCardY),
                      static_cast<uint16_t>(Theme::kScreenW - Theme::kPadLg * 2),
                      Theme::kReadingCardH};
}

core::Rect reading_btn_rect()
{
    const core::Rect card = reading_card_rect();
    const uint16_t bw     = static_cast<uint16_t>(card.w - 16);
    return core::Rect{
        static_cast<int16_t>(card.x + 8),
        static_cast<int16_t>(card.y + card.h - Theme::kReadingBtnH - 6),
        bw, Theme::kReadingBtnH};
}

core::Rect quick_area_rect()
{
    return core::Rect{0, static_cast<int16_t>(Theme::kQuickY),
                      Theme::kScreenW,
                      static_cast<uint16_t>(Theme::kQuickRows * Theme::kQuickRowH)};
}

void paint_reading_card(gfx::Canvas& c)
{
    auto&            store = apps::reader::ReadingStore::get_instance();
    const core::Rect card  = reading_card_rect();
    const core::Rect btn   = reading_btn_rect();
    c.rect(card);

    const core::Rect lbl_box{
        card.x, static_cast<int16_t>(card.y + 4),
        card.w, static_cast<uint16_t>(Theme::kFontSmall + 2)};
    gfx::Canvas::TextStyle ls{};
    ls.size_px = Theme::kFontSmall;
    ls.h       = gfx::HAlign::Center;
    c.text_in(lbl_box, ui::strings::kReading, ls);

    if (store.has_book())
    {
        const core::Rect title_box{
            card.x, static_cast<int16_t>(card.y + 20),
            card.w, static_cast<uint16_t>(Theme::kFontTitle + 4)};
        gfx::Canvas::TextStyle ts{};
        ts.size_px = Theme::kFontTitle;
        ts.h       = gfx::HAlign::Center;
        c.text_in(title_box, store.title(), ts);

        char info[40];
        (void)std::snprintf(info, sizeof(info), "%u / %u  %u%%",
                            static_cast<unsigned>(store.current_page() + 1U),
                            static_cast<unsigned>(store.total_pages()),
                            static_cast<unsigned>(store.percent()));
        const core::Rect info_box{
            card.x, static_cast<int16_t>(card.y + 46),
            card.w, static_cast<uint16_t>(Theme::kFontSmall + 2)};
        gfx::Canvas::TextStyle ps{};
        ps.size_px = Theme::kFontSmall;
        ps.h       = gfx::HAlign::Center;
        c.text_in(info_box, info, ps);

        const core::Rect bar{
            static_cast<int16_t>(card.x + 10),
            static_cast<int16_t>(card.y + 62),
            static_cast<uint16_t>(card.w - 20),
            Theme::kReadingBarH};
        ui::widgets::progress_bar(c, bar, store.percent());

        c.rect(btn);
        gfx::Canvas::TextStyle bs{};
        bs.size_px = Theme::kFontBody;
        bs.h       = gfx::HAlign::Center;
        bs.v       = gfx::VAlign::Middle;
        c.text_in(btn, ui::strings::kContinueRead, bs);
    }
    else
    {
        const core::Rect empty_box{
            card.x, static_cast<int16_t>(card.y + 40),
            card.w, static_cast<uint16_t>(Theme::kFontBody + 4)};
        gfx::Canvas::TextStyle es{};
        es.size_px = Theme::kFontBody;
        es.h       = gfx::HAlign::Center;
        c.text_in(empty_box, ui::strings::kNoBook, es);

        c.rect(btn);
        gfx::Canvas::TextStyle bs{};
        bs.size_px = Theme::kFontBody;
        bs.h       = gfx::HAlign::Center;
        bs.v       = gfx::VAlign::Middle;
        c.text_in(btn, ui::strings::kGotoShelf, bs);
    }
}

void paint_quick_grid(gfx::Canvas& c)
{
    const core::Rect area = quick_area_rect();
    for (uint8_t i = 0; i < 4; ++i)
    {
        const core::Rect cell = ui::widgets::grid_cell(
            area, Theme::kQuickCols, Theme::kQuickRows, i);
        c.rect(cell);

        const int16_t ix = static_cast<int16_t>(cell.x + (cell.w - Theme::kFontIconLg) / 2);
        const int16_t iy = static_cast<int16_t>(cell.y + (cell.h - 38) / 2);
        c.glyph(ix, iy, kQuick[i].icon, Theme::kFontIconLg, gfx::FontFace::Icon);

        const core::Rect lbl_box{
            cell.x,
            static_cast<int16_t>(iy + Theme::kFontIconLg + 4),
            cell.w,
            static_cast<uint16_t>(Theme::kFontSmall + 2)};
        gfx::Canvas::TextStyle s{};
        s.size_px = Theme::kFontSmall;
        s.h       = gfx::HAlign::Center;
        c.text_in(lbl_box, kQuick[i].label, s);
    }
}

} // namespace

HomePage& HomePage::instance()
{
    static HomePage s;
    return s;
}

void HomePage::paint(gfx::Canvas& c)
{
    paint_reading_card(c);
    paint_quick_grid(c);
}

InputResult HomePage::on_input(const ::app::ebook::input::Event& ev)
{
    using ::app::ebook::input::EventType;
    auto& r = router::Router::instance();

    if (ev.type == EventType::SwipeDown &&
        ev.start_y <= Theme::kStatusBarH)
    {
        overlays::ControlCenter::instance().open();
        return {true};
    }

    if (ev.type != EventType::Tap)
        return {};

    const int16_t x = static_cast<int16_t>(ev.x);
    const int16_t y = static_cast<int16_t>(ev.y);

    if (reading_card_rect().contains(x, y))
    {
        apps::reader::ReaderApp::request_resume_on_enter();
        (void)r.navigate(router::page_app(AppId::Reader));
        return {true};
    }

    const core::Rect area = quick_area_rect();
    const int8_t i = ui::widgets::grid_hit_test(
        area, Theme::kQuickCols, Theme::kQuickRows, x, y);
    if (i < 0)
        return {};

    if (kQuick[i].target == AppId::None)
    {
        (void)r.navigate(router::page_shell(router::ShellPage::AppGrid));
    }
    else
    {
        (void)r.navigate(router::page_app(kQuick[i].target));
    }
    return {true};
}

} // namespace app::ebook::shell
