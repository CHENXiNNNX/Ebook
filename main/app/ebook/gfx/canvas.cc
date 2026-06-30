#include "gfx/canvas.hpp"

#include "bsp/driver/gdey027t91/framebuffer.hpp"

namespace app::ebook::gfx {

namespace {

using Fb = ::app::bsp::driver::gdey027t91::Framebuffer;

inline bool is_black(Ink i) { return i == Ink::Black; }

} // namespace

void Canvas::clear(Ink ink)
{
    if (empty()) return;
    const Ink phys = physical(ink);
    Fb::fill_rect(fb_,
                  static_cast<uint16_t>(clip_.x),
                  static_cast<uint16_t>(clip_.y),
                  clip_.w, clip_.h, is_black(phys));
}

void Canvas::fill(const core::Rect& r, Ink ink)
{
    if (empty()) return;
    const core::Rect cr = clip_of(r);
    if (cr.empty()) return;
    const Ink phys = physical(ink);
    Fb::fill_rect(fb_,
                  static_cast<uint16_t>(cr.x),
                  static_cast<uint16_t>(cr.y),
                  cr.w, cr.h, is_black(phys));
}

void Canvas::invert(const core::Rect& r)
{
    if (empty()) return;
    const core::Rect cr = clip_of(r);
    if (cr.empty()) return;
    Fb::invert_rect(fb_,
                    static_cast<uint16_t>(cr.x),
                    static_cast<uint16_t>(cr.y),
                    cr.w, cr.h);
}

void Canvas::rect(const core::Rect& r, uint8_t thickness, Ink ink)
{
    if (empty() || thickness == 0 || r.w == 0 || r.h == 0) return;

    const uint16_t t = thickness;

    fill(core::Rect{r.x, r.y, r.w, t}, ink);
    if (r.h > t)
        fill(core::Rect{r.x, static_cast<int16_t>(r.y + r.h - t), r.w, t}, ink);

    if (r.h > 2 * t)
    {
        const uint16_t mid_h = static_cast<uint16_t>(r.h - 2 * t);
        const int16_t  mid_y = static_cast<int16_t>(r.y + t);
        fill(core::Rect{r.x, mid_y, t, mid_h}, ink);
        if (r.w > t)
            fill(core::Rect{static_cast<int16_t>(r.x + r.w - t), mid_y, t, mid_h}, ink);
    }
}

void Canvas::hline(int16_t x, int16_t y, uint16_t w, Ink ink)
{
    fill(core::Rect{x, y, w, 1}, ink);
}

void Canvas::vline(int16_t x, int16_t y, uint16_t h, Ink ink)
{
    fill(core::Rect{x, y, 1, h}, ink);
}

void Canvas::pixel(int16_t x, int16_t y, Ink ink)
{
    if (empty() || !clip_.contains(x, y)) return;
    Fb::set_pixel(fb_, x, y, is_black(physical(ink)));
}

uint16_t Canvas::glyph(int16_t x, int16_t y_top, uint32_t cp, uint8_t size_px,
                       FontFace face, Ink ink)
{
    if (empty()) return 0;
    return Font::get_instance().draw_glyph(
        fb_, x, y_top, cp, size_px, face, physical(ink), clip_);
}

uint16_t Canvas::text(int16_t x, int16_t y_top, const char* utf8, uint8_t size_px,
                      FontFace face, Ink ink)
{
    if (empty() || utf8 == nullptr) return 0;
    return Font::get_instance().draw_text(
        fb_, x, y_top, utf8, size_px, face, physical(ink), clip_);
}

void Canvas::text_in(const core::Rect& box, const char* utf8, const TextStyle& style)
{
    if (empty() || utf8 == nullptr || box.empty()) return;

    auto& font = Font::get_instance();
    const uint16_t pad = style.padding;
    const core::Rect inner = box.inflated(static_cast<int16_t>(-pad), static_cast<int16_t>(-pad));
    if (inner.empty()) return;

    const uint16_t tw = font.measure(utf8, style.size_px, style.face);
    const uint16_t lh = font.line_height(style.size_px, style.face);

    int16_t x = inner.x;
    switch (style.h)
    {
        case HAlign::Center:
            x = static_cast<int16_t>(inner.x + (inner.w > tw ? (inner.w - tw) / 2 : 0));
            break;
        case HAlign::Right:
            x = static_cast<int16_t>(inner.x + (inner.w > tw ? (inner.w - tw) : 0));
            break;
        case HAlign::Left:
        default:
            break;
    }

    int16_t y_top = inner.y;
    switch (style.v)
    {
        case VAlign::Middle:
            y_top = static_cast<int16_t>(inner.y + (inner.h > lh ? (inner.h - lh) / 2 : 0));
            break;
        case VAlign::Bottom:
            y_top = static_cast<int16_t>(inner.y + (inner.h > lh ? (inner.h - lh) : 0));
            break;
        case VAlign::Top:
        default:
            break;
    }

    const core::Rect sub_clip = core::Rect::intersect(clip_, box);
    font.draw_text(fb_, x, y_top, utf8, style.size_px, style.face,
                   physical(style.ink), sub_clip);
}

} // namespace app::ebook::gfx
