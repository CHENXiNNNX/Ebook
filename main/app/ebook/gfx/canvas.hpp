#pragma once

#include <cstdint>

#include "core/geometry.hpp"
#include "gfx/font.hpp"
#include "gfx/text_layout.hpp"

namespace app::ebook::gfx {

/**
 * @brief 1-bit 帧缓冲绘制（clip + dark 翻转；不持有 fb 生命周期）
 *
 * dark_=true 时 Ink 黑白对调，实现夜间模式无需事后 invert。
 */
class Canvas
{
  public:
    static Canvas full(uint8_t* fb, bool dark = false)
    {
        return Canvas{fb, core::Rect::full(), dark};
    }

    Canvas() = default;
    Canvas(uint8_t* fb, const core::Rect& clip, bool dark = false)
        : fb_(fb), clip_(clip.clamped()), dark_(dark) {}

    Canvas with_clip(const core::Rect& r) const
    {
        return Canvas{fb_, core::Rect::intersect(clip_, r), dark_};
    }

    Canvas with_dark(bool d) const
    {
        return Canvas{fb_, clip_, d};
    }

    bool empty() const { return fb_ == nullptr || clip_.empty(); }
    bool dark()  const { return dark_; }
    const core::Rect& clip() const { return clip_; }
    uint8_t* fb() const { return fb_; }

    void clear(Ink ink = Ink::White);
    void fill(const core::Rect& r, Ink ink = Ink::Black);
    void rect(const core::Rect& r, uint8_t thickness = 1, Ink ink = Ink::Black);
    void hline(int16_t x, int16_t y, uint16_t w, Ink ink = Ink::Black);
    void vline(int16_t x, int16_t y, uint16_t h, Ink ink = Ink::Black);
    void pixel(int16_t x, int16_t y, Ink ink = Ink::Black);

    /** 物理 XOR，不受 dark_ 影响 */
    void invert(const core::Rect& r);

    uint16_t glyph(int16_t x, int16_t y_top, uint32_t cp, uint8_t size_px,
                   FontFace face = FontFace::Text, Ink ink = Ink::Black);

    uint16_t text(int16_t x, int16_t y_top, const char* utf8, uint8_t size_px,
                  FontFace face = FontFace::Text, Ink ink = Ink::Black);

    struct TextStyle
    {
        uint8_t  size_px{12};
        FontFace face{FontFace::Text};
        Ink      ink{Ink::Black};
        HAlign   h{HAlign::Left};
        VAlign   v{VAlign::Top};
        uint8_t  padding{0};
    };

    void text_in(const core::Rect& box, const char* utf8, const TextStyle& style);

  private:
    Ink physical(Ink logical) const
    {
        if (!dark_) return logical;
        return (logical == Ink::Black) ? Ink::White : Ink::Black;
    }

    core::Rect clip_of(const core::Rect& r) const
    {
        return core::Rect::intersect(clip_, r);
    }

    uint8_t*   fb_{nullptr};
    core::Rect clip_{};
    bool       dark_{false};
};

} // namespace app::ebook::gfx
