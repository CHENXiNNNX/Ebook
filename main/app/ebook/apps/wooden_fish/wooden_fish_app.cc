#include "apps/wooden_fish/wooden_fish_app.hpp"

#include <cstdio>
#include <cstring>

#include "bsp/driver/gdey027t91/framebuffer.hpp"
#include "common/time/time.hpp"
#include "gfx/font.hpp"
#include "gfx/icon.hpp"
#include "presenter/presenter.hpp"
#include "router/refresh_intent.hpp"
#include "ui/strings.hpp"
#include "ui/theme.hpp"
#include "ui/ui_bus.hpp"
#include "platform/wooden_fish_sound.hpp"
#include "ui/widgets.hpp"

namespace app::ebook::apps::wooden_fish {

namespace {

using ui::Theme;
using Fb     = ::app::bsp::driver::gdey027t91::Framebuffer;
using Panel  = ::app::bsp::driver::gdey027t91::Panel;

constexpr uint8_t kStickIconPx     = 36;
constexpr uint8_t kStickPadPx      = 4;
constexpr uint8_t kStickTile       = kStickIconPx + kStickPadPx * 2U;
constexpr uint8_t kStickTileStride = static_cast<uint8_t>((kStickTile + 7U) / 8U);
constexpr uint8_t kFbStride        = static_cast<uint8_t>(Panel::STRIDE);

struct StickRot
{
    float c;
    float s;
};

// 待机 -0.35 rad / 敲击 -1.05 rad（绕 pivot 左旋）
constexpr StickRot kRotIdle  = {0.939393f, -0.342900f};
constexpr StickRot kRotKnock = {0.497571f, -0.867423f};


/** 1-bit：清位=黑（木鱼棒笔划），置位=白（透明底） */
bool stick_tile_black(const uint8_t* tile, uint8_t x, uint8_t y)
{
    if (tile == nullptr || x >= kStickTile || y >= kStickTile)
        return false;
    const uint8_t mask = static_cast<uint8_t>(0x80U >> (x & 7U));
    return (tile[static_cast<uint32_t>(y) * kStickTileStride + (x / 8U)] & mask) == 0;
}

void stick_tile_set_black(uint8_t* tile, uint8_t x, uint8_t y)
{
    if (tile == nullptr || x >= kStickTile || y >= kStickTile)
        return;
    const uint8_t mask = static_cast<uint8_t>(0x80U >> (x & 7U));
    tile[static_cast<uint32_t>(y) * kStickTileStride + (x / 8U)] &= static_cast<uint8_t>(~mask);
}

void render_stick_source(uint8_t* tile)
{
    if (tile == nullptr)
        return;
    std::memset(tile, 0xFF, static_cast<size_t>(kStickTileStride) * kStickTile);

    // Canvas/Fb::set_pixel 按 Panel::STRIDE 寻址，缓冲须匹配行宽
    static uint8_t tmp[static_cast<uint32_t>(kFbStride) * kStickTile]{};
    const core::Rect tile_r{0, 0, kStickTile, kStickTile};
    gfx::Canvas tmp_canvas{tmp, tile_r};
    tmp_canvas.clear(gfx::Ink::White);

    auto& font = gfx::Font::get_instance();
    const uint16_t adv =
        font.advance(gfx::icon::kEbWoodenStick, kStickIconPx, gfx::FontFace::Icon);
    const int16_t gx = static_cast<int16_t>((kStickTile - adv) / 2);
    const int16_t gy = static_cast<int16_t>((kStickTile - kStickIconPx) / 2);
    (void)tmp_canvas.glyph(gx, gy, gfx::icon::kEbWoodenStick, kStickIconPx,
                           gfx::FontFace::Icon);

    for (uint8_t y = 0; y < kStickTile; ++y)
    {
        for (uint8_t x = 0; x < kStickTile; ++x)
        {
            const uint8_t mask = static_cast<uint8_t>(0x80U >> (x & 7U));
            const uint32_t src =
                static_cast<uint32_t>(y) * kFbStride + static_cast<uint32_t>(x / 8U);
            const uint32_t dst =
                static_cast<uint32_t>(y) * kStickTileStride + static_cast<uint32_t>(x / 8U);
            if ((tmp[src] & mask) == 0)
                tile[dst] &= static_cast<uint8_t>(~mask);
        }
    }
}

/** 逆映射旋转烘焙，避免正向逐点投射产生的孤立噪点 */
void bake_rotated_stick(uint8_t* out, const uint8_t* src, const StickRot& rot)
{
    if (out == nullptr || src == nullptr)
        return;

    std::memset(out, 0xFF, static_cast<size_t>(kStickTileStride) * kStickTile);

    const float cx = static_cast<float>(kStickTile) * 0.5f;
    const float cy = static_cast<float>(kStickTile) * 0.5f;

    for (uint8_t dy = 0; dy < kStickTile; ++dy)
    {
        for (uint8_t dx = 0; dx < kStickTile; ++dx)
        {
            const float ox = static_cast<float>(dx) - cx;
            const float oy = static_cast<float>(dy) - cy;
            const float sx = ox * rot.c + oy * rot.s;
            const float sy = -ox * rot.s + oy * rot.c;
            const int16_t ix = static_cast<int16_t>(sx + cx + 0.5f);
            const int16_t iy = static_cast<int16_t>(sy + cy + 0.5f);
            if (ix < 0 || iy < 0 || ix >= kStickTile || iy >= kStickTile)
                continue;
            if (stick_tile_black(src, static_cast<uint8_t>(ix), static_cast<uint8_t>(iy)))
                stick_tile_set_black(out, dx, dy);
        }
    }
}

/** 去掉完全孤立的单像素噪点 */
void prune_isolated_stick(uint8_t* tile)
{
    if (tile == nullptr)
        return;

    for (uint8_t y = 0; y < kStickTile; ++y)
    {
        for (uint8_t x = 0; x < kStickTile; ++x)
        {
            if (!stick_tile_black(tile, x, y))
                continue;

            uint8_t n = 0;
            if (x > 0 && stick_tile_black(tile, static_cast<uint8_t>(x - 1U), y))
                ++n;
            if (x + 1U < kStickTile && stick_tile_black(tile, static_cast<uint8_t>(x + 1U), y))
                ++n;
            if (y > 0 && stick_tile_black(tile, x, static_cast<uint8_t>(y - 1U)))
                ++n;
            if (y + 1U < kStickTile && stick_tile_black(tile, x, static_cast<uint8_t>(y + 1U)))
                ++n;

            if (n == 0)
            {
                const uint8_t mask = static_cast<uint8_t>(0x80U >> (x & 7U));
                tile[static_cast<uint32_t>(y) * kStickTileStride + (x / 8U)] |= mask;
            }
        }
    }
}

void blit_stick_tile(uint8_t* fb, const uint8_t* tile, int16_t pivot_x, int16_t pivot_y,
                     const core::Rect& clip)
{
    if (fb == nullptr || tile == nullptr)
        return;

    const int16_t ox = static_cast<int16_t>(pivot_x - kStickTile / 2);
    const int16_t oy = static_cast<int16_t>(pivot_y - kStickTile / 2);

    for (uint8_t sy = 0; sy < kStickTile; ++sy)
    {
        for (uint8_t sx = 0; sx < kStickTile; ++sx)
        {
            if (!stick_tile_black(tile, sx, sy))
                continue;

            const int16_t px = static_cast<int16_t>(ox + sx);
            const int16_t py = static_cast<int16_t>(oy + sy);
            if (!clip.contains(px, py))
                continue;
            Fb::set_pixel(fb, px, py, true);
        }
    }
}

struct StickTiles
{
    uint8_t src[kStickTileStride * kStickTile]{};
    uint8_t idle[kStickTileStride * kStickTile]{};
    uint8_t knock[kStickTileStride * kStickTile]{};
    bool    ready{false};
};

void ensure_stick_tiles(StickTiles& t)
{
    if (t.ready)
        return;

    render_stick_source(t.src);
    bake_rotated_stick(t.idle, t.src, kRotIdle);
    bake_rotated_stick(t.knock, t.src, kRotKnock);
    prune_isolated_stick(t.idle);
    prune_isolated_stick(t.knock);
    t.ready = true;
}

void anim_timer_cb(void* /*arg*/)
{
    (void)ui::UiBus::get_instance().post_system_hint(
        ui::SystemHintKind::WoodenFishAnimDone, 0);
}

} // namespace

WoodenFishApp& WoodenFishApp::instance()
{
    static WoodenFishApp s;
    return s;
}

const char* WoodenFishApp::title() const { return ui::strings::kAppFish; }

uint32_t WoodenFishApp::icon_cp() const { return gfx::icon::kEbWoodenFish; }

core::Rect WoodenFishApp::body_rect()
{
    return core::Rect{
        0, static_cast<int16_t>(Theme::kListStartY), Theme::kScreenW,
        static_cast<uint16_t>(Theme::kScreenH - Theme::kListStartY)};
}

core::Rect WoodenFishApp::fish_rect()
{
    const core::Rect body = body_rect();
    const int16_t    x    = static_cast<int16_t>(body.x + (body.w - kFishIconSize) / 2);
    const int16_t    y    = static_cast<int16_t>(body.y + (body.h - kFishIconSize) / 2 + 8);
    return core::Rect{x, y, kFishIconSize, kFishIconSize};
}

WoodenFishApp::StickPivot WoodenFishApp::stick_pivot()
{
    const core::Rect fish = fish_rect();
    StickPivot       p{};
    p.x = static_cast<int16_t>(fish.right() + 2);
    p.y = static_cast<int16_t>(fish.y + 2);
    return p;
}

core::Rect WoodenFishApp::merit_rect()
{
    const core::Rect body = body_rect();
    return core::Rect{body.x, static_cast<int16_t>(body.y + 12), body.w, 36};
}

void WoodenFishApp::paint_stick(gfx::Canvas& c, bool knocking)
{
    static StickTiles s_tiles{};
    ensure_stick_tiles(s_tiles);

    const StickPivot pivot = stick_pivot();
    const uint8_t*   tile  = knocking ? s_tiles.knock : s_tiles.idle;
    blit_stick_tile(c.fb(), tile, pivot.x, pivot.y, c.clip());
}

void WoodenFishApp::ensure_anim_timer()
{
    if (anim_timer_ != nullptr)
        return;

    esp_timer_create_args_t args{};
    args.callback              = anim_timer_cb;
    args.arg                   = nullptr;
    args.name                  = "fish_anim";
    args.dispatch_method       = ESP_TIMER_TASK;
    args.skip_unhandled_events = true;
    (void)esp_timer_create(&args, &anim_timer_);
}

void WoodenFishApp::arm_anim_timer()
{
    ensure_anim_timer();
    if (anim_timer_ == nullptr)
        return;

    (void)esp_timer_stop(anim_timer_);
    (void)esp_timer_start_once(anim_timer_,
                               static_cast<uint64_t>(kKnockAnimMs) * 1000ULL);
}

void WoodenFishApp::on_enter()
{
    ensure_anim_timer();
    knock_anim_until_ms_ = 0;
    last_repaint_ms_     = 0;
    pending_repaint_     = false;
    request_repaint(router::intent_partial_full());
}

void WoodenFishApp::on_exit()
{
    knock_anim_until_ms_ = 0;
    if (anim_timer_ != nullptr)
        (void)esp_timer_stop(anim_timer_);
}

void WoodenFishApp::schedule_repaint(bool force)
{
    const int64_t now = ::app::common::time::uptime_ms();
    if (!force)
    {
        if ((now - last_repaint_ms_) < kRepaintMinMs)
        {
            pending_repaint_ = true;
            return;
        }
        if (!presenter::Presenter::instance().wait_idle(0))
        {
            pending_repaint_ = true;
            return;
        }
    }

    last_repaint_ms_ = now;
    pending_repaint_ = false;
    request_repaint(router::intent_partial_full());
}

void WoodenFishApp::on_knock()
{
    const int64_t now    = ::app::common::time::uptime_ms();
    knock_anim_until_ms_ = now + kKnockAnimMs;
    ++merit_;
    (void)platform::WoodenFishSound::get_instance().play_knock();
    arm_anim_timer();
    schedule_repaint(true);
}

void WoodenFishApp::on_ui_event(const ui::UiEvent& ev)
{
    if (ev.kind != ui::UiEventKind::SystemHint)
        return;
    if (ev.payload.system.hint != ui::SystemHintKind::WoodenFishAnimDone)
        return;

    schedule_repaint(true);
}

void WoodenFishApp::paint(gfx::Canvas& c)
{
    ui::widgets::toolbar(c, title());

    const int64_t now      = ::app::common::time::uptime_ms();
    const bool    knocking = now < knock_anim_until_ms_;

    char merit_buf[24];
    (void)std::snprintf(merit_buf, sizeof(merit_buf), ui::strings::kFishMeritFmt,
                        static_cast<unsigned>(merit_));

    gfx::Canvas::TextStyle merit_ts{};
    merit_ts.size_px = Theme::kFontLarge;
    merit_ts.h       = gfx::HAlign::Center;
    merit_ts.v       = gfx::VAlign::Middle;
    c.text_in(merit_rect(), merit_buf, merit_ts);

    const core::Rect fish = fish_rect();
    c.glyph(fish.x, fish.y, gfx::icon::kEbWoodenFish, kFishIconSize, gfx::FontFace::Icon);
    paint_stick(c, knocking);

    const core::Rect body = body_rect();
    gfx::Canvas::TextStyle hint_ts{};
    hint_ts.size_px = Theme::kFontSmall;
    hint_ts.h       = gfx::HAlign::Center;
    hint_ts.v       = gfx::VAlign::Bottom;
    const core::Rect hint_r{
        body.x, static_cast<int16_t>(body.bottom() - 22), body.w, 18};
    c.text_in(hint_r, ui::strings::kFishTapHint, hint_ts);
}

shell::InputResult WoodenFishApp::on_input(const ::app::ebook::input::Event& ev)
{
    using ::app::ebook::input::EventType;

    if (ev.type != EventType::Tap)
        return {};

    const int16_t x = static_cast<int16_t>(ev.x);
    const int16_t y = static_cast<int16_t>(ev.y);

    if (ui::widgets::hit_toolbar_back(x, y))
        return {};

    if (!body_rect().contains(x, y))
        return {};

    on_knock();
    return {true};
}

} // namespace app::ebook::apps::wooden_fish
