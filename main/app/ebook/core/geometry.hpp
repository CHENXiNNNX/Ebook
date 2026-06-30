#pragma once

#include <cstdint>

#include "bsp/driver/gdey027t91/panel.hpp"

namespace app::ebook::core {

inline constexpr uint16_t kScreenW = ::app::bsp::driver::gdey027t91::Panel::WIDTH;
inline constexpr uint16_t kScreenH = ::app::bsp::driver::gdey027t91::Panel::HEIGHT;
inline constexpr uint16_t kStride  = ::app::bsp::driver::gdey027t91::Panel::STRIDE;
inline constexpr uint32_t kFbBytes = ::app::bsp::driver::gdey027t91::Panel::BUFFER_SIZE;

/** SSD1680 局刷窗口 x/w 须 8 像素对齐 */
inline constexpr uint8_t kPartialAlign = 8;

struct Point
{
    int16_t x = 0;
    int16_t y = 0;

    constexpr Point() = default;
    constexpr Point(int16_t px, int16_t py) : x(px), y(py) {}
};

struct Size
{
    uint16_t w = 0;
    uint16_t h = 0;

    constexpr Size() = default;
    constexpr Size(uint16_t sw, uint16_t sh) : w(sw), h(sh) {}

    constexpr bool empty() const { return w == 0 || h == 0; }
    constexpr uint32_t area() const { return static_cast<uint32_t>(w) * h; }
};

/** 矩形 [x, x+w) × [y, y+h)；constexpr，x/y 可负 */
struct Rect
{
    int16_t  x = 0;
    int16_t  y = 0;
    uint16_t w = 0;
    uint16_t h = 0;

    constexpr Rect() = default;
    constexpr Rect(int16_t rx, int16_t ry, uint16_t rw, uint16_t rh)
        : x(rx), y(ry), w(rw), h(rh) {}

    static constexpr Rect full() { return {0, 0, kScreenW, kScreenH}; }

    constexpr bool     empty()  const { return w == 0 || h == 0; }
    constexpr int16_t  right()  const { return static_cast<int16_t>(x + static_cast<int16_t>(w)); }
    constexpr int16_t  bottom() const { return static_cast<int16_t>(y + static_cast<int16_t>(h)); }
    constexpr Size     size()   const { return Size{w, h}; }
    constexpr uint32_t area()   const { return static_cast<uint32_t>(w) * h; }

    constexpr bool contains(int16_t px, int16_t py) const
    {
        return !empty() && px >= x && py >= y && px < right() && py < bottom();
    }

    constexpr bool intersects(const Rect& o) const
    {
        return !empty() && !o.empty() &&
               x < o.right() && o.x < right() &&
               y < o.bottom() && o.y < bottom();
    }

    static constexpr Rect intersect(const Rect& a, const Rect& b)
    {
        if (a.empty() || b.empty()) return {};
        const int16_t x1 = (a.x > b.x) ? a.x : b.x;
        const int16_t y1 = (a.y > b.y) ? a.y : b.y;
        const int16_t x2 = (a.right()  < b.right())  ? a.right()  : b.right();
        const int16_t y2 = (a.bottom() < b.bottom()) ? a.bottom() : b.bottom();
        if (x2 <= x1 || y2 <= y1) return {};
        return Rect{x1, y1,
                    static_cast<uint16_t>(x2 - x1),
                    static_cast<uint16_t>(y2 - y1)};
    }

    static constexpr Rect merge(const Rect& a, const Rect& b)
    {
        if (a.empty()) return b;
        if (b.empty()) return a;
        const int16_t x1 = (a.x < b.x) ? a.x : b.x;
        const int16_t y1 = (a.y < b.y) ? a.y : b.y;
        const int16_t x2 = (a.right()  > b.right())  ? a.right()  : b.right();
        const int16_t y2 = (a.bottom() > b.bottom()) ? a.bottom() : b.bottom();
        return Rect{x1, y1,
                    static_cast<uint16_t>(x2 - x1),
                    static_cast<uint16_t>(y2 - y1)};
    }

    constexpr Rect translated(int16_t dx, int16_t dy) const
    {
        return Rect{static_cast<int16_t>(x + dx), static_cast<int16_t>(y + dy), w, h};
    }

    constexpr Rect inflated(int16_t dx, int16_t dy) const
    {
        const int32_t nw = static_cast<int32_t>(w) + 2 * dx;
        const int32_t nh = static_cast<int32_t>(h) + 2 * dy;
        if (nw <= 0 || nh <= 0) return {};
        return Rect{static_cast<int16_t>(x - dx),
                    static_cast<int16_t>(y - dy),
                    static_cast<uint16_t>(nw),
                    static_cast<uint16_t>(nh)};
    }

    constexpr Rect clamped() const
    {
        if (empty()) return {};
        const int16_t x1 = (x < 0) ? 0 : x;
        const int16_t y1 = (y < 0) ? 0 : y;
        const int16_t x2 = (right()  > static_cast<int16_t>(kScreenW))
                            ? static_cast<int16_t>(kScreenW) : right();
        const int16_t y2 = (bottom() > static_cast<int16_t>(kScreenH))
                            ? static_cast<int16_t>(kScreenH) : bottom();
        if (x2 <= x1 || y2 <= y1) return {};
        return Rect{x1, y1,
                    static_cast<uint16_t>(x2 - x1),
                    static_cast<uint16_t>(y2 - y1)};
    }

    /** 裁剪到屏幕并将 x/w 对齐到 kPartialAlign（局刷提交要求） */
    constexpr Rect aligned() const
    {
        Rect c = clamped();
        if (c.empty()) return {};

        constexpr uint16_t mask = static_cast<uint16_t>(kPartialAlign - 1);
        int16_t x1 = static_cast<int16_t>(c.x & ~mask);
        int16_t x2 = static_cast<int16_t>((c.right() + mask) & ~mask);
        if (x2 > static_cast<int16_t>(kScreenW))
            x2 = static_cast<int16_t>(kScreenW);
        if (x2 <= x1) return {};

        return Rect{x1, c.y, static_cast<uint16_t>(x2 - x1), c.h};
    }
};

constexpr uint32_t screen_area_pct(uint8_t pct)
{
    return (static_cast<uint32_t>(kScreenW) * kScreenH * pct) / 100U;
}

constexpr bool rect_meets_area_pct(const Rect& r, uint8_t pct)
{
    if (r.empty() || pct == 0)
        return false;
    const uint32_t panel = static_cast<uint32_t>(kScreenW) * kScreenH;
    return r.area() * 100U >= panel * static_cast<uint32_t>(pct);
}

} // namespace app::ebook::core
