#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>

#include "panel.hpp"

namespace app::bsp::driver::gdey027t91 {

struct Framebuffer
{
    static constexpr uint8_t kWhite = 0xFF;
    static constexpr uint8_t kBlack = 0x00;

    static void fill(uint8_t* fb, uint32_t len, uint8_t color = kWhite)
    {
        if (fb != nullptr && len > 0)
            std::memset(fb, color, len);
    }

    static void set_pixel(uint8_t* fb, int16_t x, int16_t y, bool black)
    {
        if (fb == nullptr || x < 0 || y < 0 || x >= Panel::WIDTH || y >= Panel::HEIGHT)
            return;

        const uint32_t idx =
            (static_cast<uint32_t>(y) * Panel::STRIDE) + (static_cast<uint16_t>(x) / 8U);
        const uint8_t mask = static_cast<uint8_t>(0x80U >> (static_cast<uint16_t>(x) & 7U));
        if (black)
            fb[idx] &= static_cast<uint8_t>(~mask);
        else
            fb[idx] |= mask;
    }

    static void fill_rect(uint8_t* fb, uint16_t x, uint16_t y, uint16_t w, uint16_t h, bool black)
    {
        if (fb == nullptr || w == 0 || h == 0)
            return;

        const uint16_t x_end = std::min(static_cast<uint16_t>(x + w), Panel::WIDTH);
        const uint16_t y_end = std::min(static_cast<uint16_t>(y + h), Panel::HEIGHT);
        if (x >= Panel::WIDTH || y >= Panel::HEIGHT || x_end <= x || y_end <= y)
            return;

        const uint16_t rw = static_cast<uint16_t>(x_end - x);
        const uint8_t fill_val = black ? kBlack : kWhite;

        if ((x & 7U) == 0U && (rw & 7U) == 0U)
        {
            const uint16_t bx = static_cast<uint16_t>(x / 8U);
            const uint16_t bw = static_cast<uint16_t>(rw / 8U);
            for (uint16_t row = y; row < y_end; row++)
                std::memset(fb + (static_cast<uint32_t>(row) * Panel::STRIDE) + bx, fill_val, bw);
            return;
        }

        const uint16_t first_byte = x / 8U;
        const uint16_t last_byte = (x_end - 1U) / 8U;
        const uint8_t lead_bit = static_cast<uint8_t>(x & 7U);
        const uint8_t tail_bit = static_cast<uint8_t>(x_end & 7U);

        for (uint16_t row = y; row < y_end; row++)
        {
            uint8_t* rp = fb + static_cast<uint32_t>(row) * Panel::STRIDE;

            if (first_byte == last_byte)
            {
                uint8_t mask = 0;
                for (uint16_t col = x; col < x_end; col++)
                    mask |= static_cast<uint8_t>(0x80U >> (col & 7U));
                if (black)
                    rp[first_byte] &= static_cast<uint8_t>(~mask);
                else
                    rp[first_byte] |= mask;
                continue;
            }

            if (lead_bit != 0)
            {
                const uint8_t lead_mask = static_cast<uint8_t>(0xFFU >> lead_bit);
                if (black)
                    rp[first_byte] &= static_cast<uint8_t>(~lead_mask);
                else
                    rp[first_byte] |= lead_mask;
            }

            const uint16_t mid_start =
                (lead_bit != 0) ? static_cast<uint16_t>(first_byte + 1) : first_byte;
            if (mid_start < last_byte)
                std::memset(rp + mid_start, fill_val, last_byte - mid_start);

            if (tail_bit != 0)
            {
                const uint8_t tail_mask = static_cast<uint8_t>(0xFFU << (8U - tail_bit));
                if (black)
                    rp[last_byte] &= static_cast<uint8_t>(~tail_mask);
                else
                    rp[last_byte] |= tail_mask;
            }
            else
            {
                rp[last_byte] = fill_val;
            }
        }
    }

    static void draw_border(uint8_t* fb, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                            uint16_t thickness = 1)
    {
        if (thickness == 0)
            return;
        fill_rect(fb, x, y, w, thickness, true);
        fill_rect(fb, x, static_cast<uint16_t>(y + h - thickness), w, thickness, true);
        fill_rect(fb, x, y, thickness, h, true);
        fill_rect(fb, static_cast<uint16_t>(x + w - thickness), y, thickness, h, true);
    }

    static void blit_rect(uint8_t* dst, const uint8_t* src, int16_t x, int16_t y,
                          uint16_t w, uint16_t h, uint16_t stride = Panel::STRIDE)
    {
        if (dst == nullptr || src == nullptr || w == 0 || h == 0)
            return;

        const uint16_t bx = static_cast<uint16_t>(x) / 8U;
        const uint16_t bw = static_cast<uint16_t>((w + 7U) / 8U);
        for (int16_t row = y; row < static_cast<int16_t>(y + h); ++row)
        {
            if (row < 0)
                continue;
            const uint32_t off = static_cast<uint32_t>(row) * stride + bx;
            std::memcpy(dst + off, src + off, bw);
        }
    }

    static void invert_rect(uint8_t* fb, uint16_t x, uint16_t y, uint16_t w, uint16_t h)
    {
        if (fb == nullptr || w == 0 || h == 0)
            return;

        const uint16_t x_end = std::min(static_cast<uint16_t>(x + w), Panel::WIDTH);
        const uint16_t y_end = std::min(static_cast<uint16_t>(y + h), Panel::HEIGHT);

        for (uint16_t row = y; row < y_end; row++)
        {
            uint8_t* rp = fb + static_cast<uint32_t>(row) * Panel::STRIDE;
            for (uint16_t col = x; col < x_end; col++)
                rp[col / 8U] ^= static_cast<uint8_t>(0x80U >> (col & 7U));
        }
    }
};

} // namespace app::bsp::driver::gdey027t91
