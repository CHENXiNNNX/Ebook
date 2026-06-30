#include "gfx/text_layout.hpp"

#include <cstdio>
#include <cstring>

#include "gfx/utf8.hpp"

namespace app::ebook::gfx {

namespace {

constexpr uint32_t kEllipsisCp = 0x2026;
constexpr char     kEllipsisUtf8[3] = {static_cast<char>(0xE2),
                                       static_cast<char>(0x80),
                                       static_cast<char>(0xA6)};

uint16_t safe_advance(uint32_t cp, uint8_t size_px, FontFace face)
{
    const uint16_t a = Font::get_instance().advance(cp, size_px, face);
    return (a == 0) ? size_px : a;
}

} // namespace

uint16_t ellipsis_width(uint8_t size_px, FontFace face)
{
    return Font::get_instance().advance(kEllipsisCp, size_px, face);
}

uint8_t wrap_text(const char* utf8, uint8_t size_px, uint16_t max_width,
                  uint8_t max_lines, LineSlice* out, FontFace face)
{
    if (utf8 == nullptr || out == nullptr || max_lines == 0 || max_width == 0)
        return 0;

    const uint16_t ell_w = ellipsis_width(size_px, face);

    const char* cursor    = utf8;
    uint8_t     emitted   = 0;

    while (*cursor != '\0' && emitted < max_lines)
    {
        const bool last_line = (emitted + 1 == max_lines);
        const uint16_t budget = last_line
            ? static_cast<uint16_t>((max_width > ell_w) ? (max_width - ell_w) : 0)
            : max_width;

        LineSlice slice{};
        slice.begin = cursor;

        const char* line_end = cursor;     // 当前已确认能放下的尾指针
        uint32_t    line_w   = 0;

        const char* probe = cursor;
        while (*probe != '\0')
        {
            uint32_t cp = 0;
            const uint8_t n = utf8_decode(probe, cp);
            if (n == 0) break;

            const uint16_t adv = safe_advance(cp, size_px, face);

            if (line_w + adv > budget)
            {
                if (line_end == slice.begin)
                {
                    line_end = probe + n;
                    line_w   = adv;
                }
                break;
            }

            line_w += adv;
            probe   += n;
            line_end = probe;
        }

        if (line_end == slice.begin) break;

        slice.end      = line_end;
        slice.width_px = static_cast<uint16_t>(line_w);
        if (last_line && *line_end != '\0')
            slice.width_px = static_cast<uint16_t>(slice.width_px + ell_w);

        out[emitted++] = slice;
        if (last_line) break;
        cursor = line_end;
    }

    return emitted;
}

size_t truncate_text(const char* utf8, uint8_t size_px, uint16_t max_width,
                     char* out, size_t out_size, FontFace face)
{
    if (out == nullptr || out_size == 0) return 0;
    out[0] = '\0';
    if (utf8 == nullptr || max_width == 0) return 0;

    const uint16_t total = Font::get_instance().measure(utf8, size_px, face);
    if (total <= max_width)
    {
        const size_t need = std::strlen(utf8);
        const size_t copy = (need < out_size - 1) ? need : (out_size - 1);
        std::memcpy(out, utf8, copy);
        out[copy] = '\0';
        return copy;
    }

    const uint16_t ell_w = ellipsis_width(size_px, face);
    const uint16_t budget =
        static_cast<uint16_t>((max_width > ell_w) ? (max_width - ell_w) : 0);

    const char* p   = utf8;
    const char* cut = utf8;
    uint32_t    acc = 0;
    while (*p != '\0')
    {
        uint32_t cp = 0;
        const uint8_t n = utf8_decode(p, cp);
        if (n == 0) break;

        const uint16_t adv = safe_advance(cp, size_px, face);
        if (acc + adv > budget) break;
        acc += adv;
        p   += n;
        cut  = p;
    }

    const size_t prefix = static_cast<size_t>(cut - utf8);
    size_t pos = (prefix < out_size - 1) ? prefix : (out_size - 1);
    std::memcpy(out, utf8, pos);

    if (pos + sizeof(kEllipsisUtf8) < out_size)
    {
        std::memcpy(out + pos, kEllipsisUtf8, sizeof(kEllipsisUtf8));
        pos += sizeof(kEllipsisUtf8);
    }
    out[pos] = '\0';
    return pos;
}

void format_file_size(uint32_t bytes, char* out, size_t out_size)
{
    if (out == nullptr || out_size == 0)
        return;
    if (bytes < 1024U)
        (void)std::snprintf(out, out_size, "%lu B", static_cast<unsigned long>(bytes));
    else if (bytes < 1024U * 1024U)
        (void)std::snprintf(out, out_size, "%lu KB",
                            static_cast<unsigned long>(bytes / 1024U));
    else
        (void)std::snprintf(out, out_size, "%lu MB",
                            static_cast<unsigned long>(bytes / (1024U * 1024U)));
}

} // namespace app::ebook::gfx
