#pragma once

#include <algorithm>
#include <cstdint>

namespace app::bsp::driver::gdey027t91 {

struct Panel
{
    static constexpr uint16_t WIDTH = 176;
    static constexpr uint16_t HEIGHT = 264;
    static constexpr uint16_t STRIDE = WIDTH / 8;
    static constexpr uint32_t BUFFER_SIZE = static_cast<uint32_t>(STRIDE) * HEIGHT;

    static constexpr uint8_t RAM_X_END = static_cast<uint8_t>((WIDTH / 8U) - 1U);

    static constexpr uint32_t BUSY_TIMEOUT_FULL_MS = 15000;
    static constexpr uint32_t BUSY_TIMEOUT_FAST_MS = 6000;
    static constexpr uint32_t BUSY_TIMEOUT_PARTIAL_MS = 3000;
    static constexpr uint32_t BUSY_TIMEOUT_CMD_MS = 2000;
    static constexpr uint32_t RESET_SETTLE_MS = 10;
};

struct Window
{
    uint16_t x = 0;
    uint16_t y = 0;
    uint16_t w = 0;
    uint16_t h = 0;

    bool valid() const { return w > 0 && h > 0; }

    bool normalize()
    {
        if (w == 0 || h == 0 || x >= Panel::WIDTH || y >= Panel::HEIGHT)
            return false;

        uint16_t x2 = static_cast<uint16_t>(x + w);
        uint16_t y2 = static_cast<uint16_t>(y + h);
        x2 = std::min(x2, Panel::WIDTH);
        y2 = std::min(y2, Panel::HEIGHT);
        if (x2 <= x || y2 <= y)
            return false;

        x = static_cast<uint16_t>(x & ~7U);
        x2 = static_cast<uint16_t>((x2 + 7U) & ~7U);
        x2 = std::min(x2, Panel::WIDTH);
        w = static_cast<uint16_t>(x2 - x);
        h = static_cast<uint16_t>(y2 - y);
        return w > 0 && h > 0 && (w & 7U) == 0U;
    }
};

} // namespace app::bsp::driver::gdey027t91
