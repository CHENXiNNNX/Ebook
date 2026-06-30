#pragma once

#include <cstdint>

namespace app::ebook::gfx {

/** UTF-8 解码；非法字节替换为 '?' 并前进 1 字节，保证循环可终止 */

constexpr uint8_t utf8_seq_len(uint8_t c0)
{
    if (c0 < 0x80)           return 1;
    if ((c0 & 0xE0) == 0xC0) return 2;
    if ((c0 & 0xF0) == 0xE0) return 3;
    if ((c0 & 0xF8) == 0xF0) return 4;
    return 0;
}

inline uint8_t utf8_decode(const char* p, uint32_t& cp)
{
    const uint8_t c0 = static_cast<uint8_t>(*p);
    if (c0 == 0) return 0;

    const uint8_t len = utf8_seq_len(c0);
    if (len == 0)
    {
        cp = '?';
        return 1;
    }

    static constexpr uint8_t kFirstByteMask[5] = {0, 0x7F, 0x1F, 0x0F, 0x07};
    uint32_t value = static_cast<uint32_t>(c0 & kFirstByteMask[len]);

    for (uint8_t i = 1; i < len; ++i)
    {
        const uint8_t c = static_cast<uint8_t>(p[i]);
        if ((c & 0xC0) != 0x80)
        {
            cp = '?';
            return 1;
        }
        value = (value << 6) | static_cast<uint32_t>(c & 0x3F);
    }
    cp = value;
    return len;
}

inline bool utf8_next(const char*& p, uint32_t& cp)
{
    const uint8_t n = utf8_decode(p, cp);
    if (n == 0) return false;
    p += n;
    return true;
}

} // namespace app::ebook::gfx
