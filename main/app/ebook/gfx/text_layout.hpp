#pragma once

#include <cstddef>
#include <cstdint>

#include "gfx/font.hpp"

namespace app::ebook::gfx {

enum class HAlign : uint8_t { Left, Center, Right };
enum class VAlign : uint8_t { Top,  Middle, Bottom };

struct LineSlice
{
    const char* begin{nullptr};
    const char* end{nullptr};
    uint16_t    width_px{0};
};

/** CJK 字符级换行；末行溢出时追加 U+2026 */
uint8_t wrap_text(const char* utf8, uint8_t size_px,
                  uint16_t max_width, uint8_t max_lines,
                  LineSlice* out, FontFace face = FontFace::Text);

/** 单行截断，必要时追加 U+2026 */
size_t truncate_text(const char* utf8, uint8_t size_px,
                     uint16_t max_width, char* out, size_t out_size,
                     FontFace face = FontFace::Text);

uint16_t ellipsis_width(uint8_t size_px, FontFace face = FontFace::Text);

void format_file_size(uint32_t bytes, char* out, size_t out_size);

} // namespace app::ebook::gfx
