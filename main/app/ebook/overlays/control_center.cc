#include "overlays/control_center.hpp"

#include <cstdio>

#include "data/system_state.hpp"
#include "gfx/font.hpp"
#include "gfx/icon.hpp"
#include "router/page_id.hpp"
#include "router/refresh_intent.hpp"
#include "router/router.hpp"
#include "apps/settings/power_save.hpp"
#include "ui/strings.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

namespace app::ebook::overlays {

namespace {

using ui::Theme;

constexpr uint8_t kTileCount  = 5;
constexpr uint8_t kLevelCount = 2;
constexpr uint8_t kLevelStep  = 10;

using apps::settings::PowerSave;

constexpr uint16_t kLevelIconColW = 14;
constexpr uint16_t kLevelLabelW   = 28;
constexpr uint16_t kLevelGap      = 4;

constexpr uint32_t kLevelIcons[kLevelCount] = {
    gfx::icon::kFaSun,
    gfx::icon::kFaVolumeUp,
};

constexpr uint16_t panel_y() { return Theme::kCtlY; }
constexpr uint16_t panel_h() { return Theme::kScreenH - Theme::kCtlY; }

constexpr uint16_t tile_w()
{
    return static_cast<uint16_t>(
        (Theme::kScreenW - Theme::kCtlPad * 2 -
         (kTileCount - 1) * Theme::kCtlTileGap) /
        kTileCount);
}

constexpr uint16_t header_bottom() { return panel_y() + Theme::kCtlTitleH; }
constexpr uint16_t tiles_top() { return static_cast<uint16_t>(header_bottom() + 5); }
constexpr uint16_t tiles_bottom()
{
    return static_cast<uint16_t>(tiles_top() + Theme::kCtlTileCellH);
}
constexpr uint16_t levels_top()
{
    return static_cast<uint16_t>(tiles_bottom() + Theme::kCtlSectionGap);
}
constexpr uint16_t footer_top()
{
    return static_cast<uint16_t>(panel_y() + panel_h() - Theme::kCtlFooterH);
}

constexpr uint32_t kTileIcons[kTileCount - 1] = {
    gfx::icon::kFaWifi,
    gfx::icon::kEbBluetooth,
    gfx::icon::kFaMoon,
    gfx::icon::kFaLock,
};

uint32_t tile_icon(uint8_t idx)
{
    if (idx == 4)
    {
        return PowerSave::get_instance().enabled() ? gfx::icon::kFaBatteryQuarter
                                                   : gfx::icon::kFaBatteryFull;
    }
    if (idx < kTileCount - 1)
        return kTileIcons[idx];
    return gfx::icon::kFaBatteryFull;
}

struct LevelRowLayout
{
    core::Rect row;
    core::Rect minus;
    core::Rect plus;
    core::Rect pct_area;
};

uint8_t step_clamp(uint8_t v, int delta)
{
    int n = static_cast<int>(v) + delta;
    if (n < 0)   n = 0;
    if (n > 100) n = 100;
    return static_cast<uint8_t>(n);
}

core::Rect make_tile_rect(uint8_t idx)
{
    const uint8_t col = idx % kTileCount;
    const uint16_t x  = static_cast<uint16_t>(
        Theme::kCtlPad + col * (tile_w() + Theme::kCtlTileGap));
    return core::Rect{static_cast<int16_t>(x), static_cast<int16_t>(tiles_top()),
                      tile_w(), Theme::kCtlTileCellH};
}

core::Rect make_level_row_rect(uint8_t idx)
{
    const uint16_t y = static_cast<uint16_t>(levels_top() + idx * Theme::kCtlLevelPitch);
    return core::Rect{
        static_cast<int16_t>(Theme::kCtlPad),
        static_cast<int16_t>(y),
        Theme::kScreenW - Theme::kCtlPad * 2,
        Theme::kCtlLevelRowH};
}

LevelRowLayout make_level_layout(uint8_t idx)
{
    LevelRowLayout L{};
    L.row = make_level_row_rect(idx);

    const int16_t plus_x = L.row.right() - static_cast<int16_t>(Theme::kCtlLevelBtnSz);
    L.plus = core::Rect{plus_x, L.row.y, Theme::kCtlLevelBtnSz, L.row.h};

    const int16_t minus_x =
        plus_x - static_cast<int16_t>(kLevelGap + Theme::kCtlLevelBtnSz);
    L.minus = core::Rect{minus_x, L.row.y, Theme::kCtlLevelBtnSz, L.row.h};

    const int16_t mid_x =
        L.row.x + static_cast<int16_t>(kLevelIconColW + kLevelLabelW + kLevelGap);
    const int16_t mid_w = minus_x - mid_x - static_cast<int16_t>(kLevelGap);
    if (mid_w > 0)
        L.pct_area = core::Rect{mid_x, L.row.y, static_cast<uint16_t>(mid_w), L.row.h};

    return L;
}


void paint_header(gfx::Canvas& c)
{
    const core::Rect bar{0, static_cast<int16_t>(panel_y()), Theme::kScreenW, Theme::kCtlTitleH};
    ui::widgets::label_in(c, bar, ui::strings::kCtlTitle, gfx::HAlign::Center,
                          gfx::VAlign::Middle, Theme::kFontBody);

    c.hline(Theme::kPad, static_cast<int16_t>(header_bottom()),
            Theme::kScreenW - Theme::kPad * 2);
}

void paint_tile(gfx::Canvas& c, const core::Rect& cell, uint32_t icon_cp, bool active)
{
    const int16_t icon_x =
        static_cast<int16_t>(cell.x + (cell.w - Theme::kCtlTileIconSz) / 2);
    const int16_t icon_y =
        static_cast<int16_t>(cell.y + (cell.h - Theme::kCtlTileIconSz) / 2);
    c.glyph(icon_x, icon_y, icon_cp, Theme::kCtlTileIconSz, gfx::FontFace::Icon);

    if (active)
        c.invert(cell);
}

void draw_step_btn(gfx::Canvas& c, const core::Rect& r, uint32_t icon_cp)
{
    c.rect(r);
    const int16_t ix = static_cast<int16_t>(r.x + (r.w - Theme::kFontIconMd) / 2);
    const int16_t iy = static_cast<int16_t>(r.y + (r.h - Theme::kFontIconMd) / 2);
    c.glyph(ix, iy, icon_cp, Theme::kFontIconMd, gfx::FontFace::Icon);
}

void paint_level_row(gfx::Canvas& c, const LevelRowLayout& L, uint32_t row_icon,
                     const char* label, uint8_t pct)
{
    const int16_t ty = static_cast<int16_t>(L.row.y + (L.row.h - Theme::kFontSmall) / 2);

    const int16_t ix =
        static_cast<int16_t>(L.row.x + (kLevelIconColW - Theme::kCtlLevelIconSz) / 2);
    const int16_t iy =
        static_cast<int16_t>(L.row.y + (L.row.h - Theme::kCtlLevelIconSz) / 2);
    c.glyph(ix, iy, row_icon, Theme::kCtlLevelIconSz, gfx::FontFace::Icon);

    const int16_t label_x = L.row.x + static_cast<int16_t>(kLevelIconColW);
    c.text(label_x, ty, label, Theme::kFontSmall);

    if (!L.pct_area.empty())
    {
        char buf[8];
        (void)std::snprintf(buf, sizeof(buf), "%u%%", static_cast<unsigned>(pct));
        ui::widgets::label_in(c, L.pct_area, buf, gfx::HAlign::Center, gfx::VAlign::Middle,
                              Theme::kFontSmall);
    }

    draw_step_btn(c, L.minus, gfx::icon::kFaMinus);
    draw_step_btn(c, L.plus, gfx::icon::kFaPlus);
}

} // namespace

ControlCenter& ControlCenter::instance()
{
    static ControlCenter s;
    return s;
}

bool ControlCenter::is_open() const
{
    return router::Router::instance().stack().overlay_top() ==
           router::OverlayId::ControlCenter;
}

core::Rect ControlCenter::bounds() const
{
    return core::Rect{0, static_cast<int16_t>(panel_y()), Theme::kScreenW, panel_h()};
}

void ControlCenter::repaint_panel()
{
    (void)router::Router::instance().repaint(router::intent_partial_full());
}

void ControlCenter::open()
{
    if (is_open())
        return;
    const router::RefreshIntent intent = router::intent_partial_full();
    (void)router::Router::instance().open_overlay(router::OverlayId::ControlCenter,
                                                   &intent);
}

void ControlCenter::close()
{
    if (!is_open())
        return;
    const router::RefreshIntent intent = router::intent_partial_full();
    (void)router::Router::instance().close_overlay(&intent);
}

core::Rect ControlCenter::tile_rect(uint8_t idx) const
{
    return make_tile_rect(idx);
}

core::Rect ControlCenter::level_row_rect(uint8_t idx) const
{
    return make_level_row_rect(idx);
}

core::Rect ControlCenter::level_minus_rect(uint8_t idx) const
{
    return make_level_layout(idx).minus;
}

core::Rect ControlCenter::level_plus_rect(uint8_t idx) const
{
    return make_level_layout(idx).plus;
}

bool ControlCenter::adjust_level(uint8_t idx, int delta)
{
    auto& sys = data::SystemState::get_instance();
    if (idx == 0)
    {
        const uint8_t v = step_clamp(sys.brightness(), delta);
        if (v == sys.brightness()) return false;
        sys.set_brightness(v);
        return true;
    }
    if (idx == 1)
    {
        const uint8_t v = step_clamp(sys.volume(), delta);
        if (v == sys.volume()) return false;
        sys.set_volume(v);
        return true;
    }
    return false;
}

shell::InputResult ControlCenter::on_input(const ::app::ebook::input::Event& ev)
{
    using ::app::ebook::input::EventType;
    if (!is_open())
        return {};

    auto& sys = data::SystemState::get_instance();
    auto& r   = router::Router::instance();

    if (ev.type == EventType::SwipeUp)
    {
        close();
        return {true};
    }

    if (ev.type != EventType::Tap)
        return {true};

    const int16_t x = static_cast<int16_t>(ev.x);
    const int16_t y = static_cast<int16_t>(ev.y);

    for (uint8_t i = 0; i < kLevelCount; ++i)
    {
        const LevelRowLayout L = make_level_layout(i);
        if (L.minus.contains(x, y))
        {
            if (adjust_level(i, -static_cast<int>(kLevelStep)))
                repaint_panel();
            return {true};
        }
        if (L.plus.contains(x, y))
        {
            if (adjust_level(i, static_cast<int>(kLevelStep)))
                repaint_panel();
            return {true};
        }
    }

    for (uint8_t i = 0; i < kTileCount; ++i)
    {
        if (!make_tile_rect(i).contains(x, y))
            continue;
        switch (i)
        {
            case 0:
                sys.set_wifi(!sys.wifi());
                break;
            case 1:
                sys.set_bluetooth(!sys.bluetooth());
                break;
            case 2:
                sys.set_night_mode(!sys.night_mode());
                return {true};
            case 3:
                // replace_shell 会清 overlay，无需先 close
                (void)r.replace_shell(router::ShellPage::Lock);
                return {true};
            case 4:
                PowerSave::get_instance().toggle_manual();
                break;
            default:
                break;
        }
        repaint_panel();
        return {true};
    }

    return {true};
}

void ControlCenter::paint(gfx::Canvas& c)
{
    auto& sys = data::SystemState::get_instance();
    const bool on_lock =
        router::Router::instance().stack().shell_top() == router::ShellPage::Lock;

    const core::Rect panel = bounds();
    c.fill(panel, gfx::Ink::White);

    paint_header(c);

    const bool states[kTileCount] = {
        sys.wifi(), sys.bluetooth(), sys.night_mode(), on_lock,
        PowerSave::get_instance().enabled(),
    };
    for (uint8_t i = 0; i < kTileCount; ++i)
        paint_tile(c, make_tile_rect(i), tile_icon(i), states[i]);

    c.hline(Theme::kPad, static_cast<int16_t>(tiles_bottom()),
            Theme::kScreenW - Theme::kPad * 2);

    static const char* level_labels[kLevelCount] = {
        ui::strings::kCtlBrightness,
        ui::strings::kCtlVolume,
    };
    const uint8_t level_vals[kLevelCount] = {sys.brightness(), sys.volume()};
    for (uint8_t i = 0; i < kLevelCount; ++i)
    {
        const LevelRowLayout L = make_level_layout(i);
        paint_level_row(c, L, kLevelIcons[i], level_labels[i], level_vals[i]);
    }

    const core::Rect footer{0, static_cast<int16_t>(footer_top()), Theme::kScreenW,
                            Theme::kCtlFooterH};
    ui::widgets::label_in(c, footer, ui::strings::kCtlSwipeUp, gfx::HAlign::Center,
                          gfx::VAlign::Middle, Theme::kFontSmall);

    c.rect(panel);
}

} // namespace app::ebook::overlays
