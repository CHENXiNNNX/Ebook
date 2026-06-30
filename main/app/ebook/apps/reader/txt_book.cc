#include "apps/reader/txt_book.hpp"

#include <cstdio>
#include <cstring>

#include <esp_heap_caps.h>

#include "core/log.hpp"
#include "apps/reader/txt_index_cache.hpp"
#include "gfx/font.hpp"
#include "gfx/text_layout.hpp"
#include "gfx/utf8.hpp"
#include "text/path_encoding.hpp"

static const char* const TAG = "TxtBook";

namespace app::ebook::apps::reader {

namespace {

uint16_t glyph_advance(uint32_t cp, uint8_t font_px)
{
    const uint16_t a =
        gfx::Font::get_instance().advance(cp, font_px, gfx::FontFace::Text);
    return (a == 0) ? font_px : a;
}

void strip_cr(char* buf, uint32_t& len)
{
    if (buf == nullptr || len == 0)
        return;
    uint32_t w = 0;
    for (uint32_t r = 0; r < len; ++r)
    {
        if (buf[r] == '\r')
            continue;
        buf[w++] = buf[r];
    }
    len = w;
    buf[len] = '\0';
}

} // namespace

void TxtBook::close()
{
    if (page_offsets_ != nullptr)
    {
        heap_caps_free(page_offsets_);
        page_offsets_ = nullptr;
    }
    if (text_ != nullptr)
    {
        heap_caps_free(text_);
        text_ = nullptr;
    }
    text_len_    = 0;
    page_count_  = 0;
    page_        = 0;
    file_size_   = 0;
    path_[0]     = '\0';
    text_encoding_ = 0;
}

bool TxtBook::load_file(const char* path)
{
    FILE* fp = std::fopen(path, "rb");
    if (fp == nullptr)
        return false;

    if (std::fseek(fp, 0, SEEK_END) != 0)
    {
        std::fclose(fp);
        return false;
    }
    const long sz = std::ftell(fp);
    if (sz < 0 || static_cast<uint32_t>(sz) > kMaxFileBytes)
    {
        std::fclose(fp);
        return false;
    }
    if (std::fseek(fp, 0, SEEK_SET) != 0)
    {
        std::fclose(fp);
        return false;
    }

    file_size_ = static_cast<uint32_t>(sz);
    const size_t raw_sz = (sz == 0) ? 1U : static_cast<size_t>(sz);
    char* raw = static_cast<char*>(heap_caps_malloc(raw_sz + 1U, MALLOC_CAP_8BIT));
    if (raw == nullptr)
    {
        std::fclose(fp);
        return false;
    }
    if (sz > 0)
    {
        const size_t rd = std::fread(raw, 1, static_cast<size_t>(sz), fp);
        if (rd != static_cast<size_t>(sz))
        {
            heap_caps_free(raw);
            std::fclose(fp);
            return false;
        }
    }
    std::fclose(fp);
    raw[sz] = '\0';

    const text::TextEncoding enc = text::detect_file_encoding(path);
    text_encoding_ = static_cast<uint8_t>(enc);

    if (enc == text::TextEncoding::Utf8)
    {
        text_     = raw;
        text_len_ = static_cast<uint32_t>(sz);
        strip_cr(text_, text_len_);
        return true;
    }

    const size_t out_cap = static_cast<size_t>(sz) * 2U + 8U;
    char* utf8 = static_cast<char*>(heap_caps_malloc(out_cap, MALLOC_CAP_8BIT));
    if (utf8 == nullptr)
    {
        heap_caps_free(raw);
        return false;
    }
    const size_t out_len =
        text::convert_text_to_utf8(enc, raw, static_cast<size_t>(sz), utf8, out_cap);
    heap_caps_free(raw);
    if (out_len == 0)
    {
        heap_caps_free(utf8);
        return false;
    }
    text_     = utf8;
    text_len_ = static_cast<uint32_t>(out_len);
    strip_cr(text_, text_len_);
    return true;
}

uint32_t TxtBook::skip_line(uint32_t offset, const ReaderLayout& layout) const
{
    if (text_ == nullptr || offset >= text_len_)
        return text_len_;

    const char* p        = text_ + offset;
    const char* end      = text_ + text_len_;
    const uint16_t max_w = layout.text_width();
    const uint8_t  fpx   = layout.font_px();

    if (*p == '\n')
        return offset + 1U;

    uint16_t line_w      = 0;
    const char* line_end = p;

    while (p < end && *p != '\n')
    {
        uint32_t cp = 0;
        const uint8_t n = gfx::utf8_decode(p, cp);
        if (n == 0)
            break;

        const uint16_t adv = glyph_advance(cp, fpx);
        if (line_w + adv > max_w)
        {
            if (line_end == text_ + offset)
                line_end = p + n;
            break;
        }
        line_w += adv;
        p += n;
        line_end = p;
    }

    if (p < end && *p == '\n')
        return static_cast<uint32_t>((p + 1) - text_);
    return static_cast<uint32_t>(line_end - text_);
}

bool TxtBook::try_load_cache(const ReaderLayout& layout)
{
    if (path_[0] == '\0' || page_offsets_ == nullptr)
        return false;

    const TxtIndexLayout idx =
        layout.make_index_layout(text_encoding_, file_size_);
    uint16_t total = 0;
    if (!TxtIndexCache::load(path_, idx, page_offsets_, &total))
        return false;

    page_count_ = total;
    return page_count_ > 0;
}

void TxtBook::save_cache(const ReaderLayout& layout)
{
    if (path_[0] == '\0' || page_offsets_ == nullptr || page_count_ == 0)
        return;

    const TxtIndexLayout idx =
        layout.make_index_layout(text_encoding_, file_size_);
    (void)TxtIndexCache::save(path_, idx, page_offsets_, page_count_);
}

bool TxtBook::build_pages(const ReaderLayout& layout)
{
    if (text_ == nullptr)
        return false;

    if (page_offsets_ == nullptr)
    {
        page_offsets_ = static_cast<uint32_t*>(
            heap_caps_malloc(sizeof(uint32_t) * kMaxPages, MALLOC_CAP_8BIT));
        if (page_offsets_ == nullptr)
            return false;
    }

    if (try_load_cache(layout))
        return true;

    const uint8_t lines_per = layout.lines_per_page();
    uint32_t pos            = 0;
    page_count_             = 0;

    while (pos <= text_len_ && page_count_ < kMaxPages)
    {
        page_offsets_[page_count_++] = pos;
        if (pos >= text_len_)
            break;

        uint8_t lines         = 0;
        const uint32_t page_start = pos;
        while (lines < lines_per && pos < text_len_)
        {
            const uint32_t next = skip_line(pos, layout);
            if (next <= pos)
                break;
            pos = next;
            ++lines;
        }
        if (pos == page_start)
            break;
    }

    if (page_count_ == 0)
    {
        page_offsets_[0] = 0;
        page_count_      = 1;
    }

    save_cache(layout);
    EBOOK_LOGD(TAG, "built %u pages", static_cast<unsigned>(page_count_));
    return true;
}

bool TxtBook::open(const char* path, const ReaderLayout& layout)
{
    close();
    if (path == nullptr || path[0] == '\0')
        return false;

    (void)std::strncpy(path_, path, sizeof(path_) - 1);
    path_[sizeof(path_) - 1] = '\0';

    layout_ = layout;

    if (!load_file(path))
        return false;
    if (!build_pages(layout))
    {
        close();
        return false;
    }
    page_ = 0;
    return true;
}

bool TxtBook::set_layout(const ReaderLayout& layout)
{
    if (!is_open())
        return false;
    if (layout.font_px() == layout_.font_px())
        return false;

    const uint16_t old_page = page_;
    layout_ = layout;
    if (!build_pages(layout))
        return false;
    if (old_page < page_count_)
        page_ = old_page;
    else if (page_count_ > 0)
        page_ = static_cast<uint16_t>(page_count_ - 1);
    return true;
}

bool TxtBook::set_page(uint16_t p)
{
    if (page_count_ == 0)
        return false;
    if (p >= page_count_)
        p = static_cast<uint16_t>(page_count_ - 1);
    if (p == page_)
        return false;
    page_ = p;
    return true;
}

bool TxtBook::next_page()
{
    if (page_count_ == 0 || page_ + 1U >= page_count_)
        return false;
    ++page_;
    return true;
}

bool TxtBook::prev_page()
{
    if (page_ == 0)
        return false;
    --page_;
    return true;
}

void TxtBook::paint_page(gfx::Canvas& canvas, const core::Rect& area) const
{
    if (text_ == nullptr || page_count_ == 0 || page_ >= page_count_)
        return;

    const uint32_t start = page_offsets_[page_];
    const uint32_t end =
        (page_ + 1U < page_count_) ? page_offsets_[page_ + 1U] : text_len_;

    const char* p         = text_ + start;
    const char* lim       = text_ + end;
    const uint8_t fpx     = layout_.font_px();
    const uint16_t lh     = layout_.line_height();
    const uint8_t max_lines = layout_.lines_per_page();

    int16_t y = static_cast<int16_t>(area.y + ReaderLayout::kContentPadY);

    for (uint8_t i = 0; i < max_lines && p < lim && y + lh <= area.bottom(); ++i)
    {
        if (*p == '\n')
        {
            ++p;
            y = static_cast<int16_t>(y + lh);
            continue;
        }

        gfx::LineSlice slice{};
        slice.begin = p;
        uint16_t line_w = 0;
        const char* line_end = p;

        while (p < lim && *p != '\n')
        {
            uint32_t cp = 0;
            const uint8_t n = gfx::utf8_decode(p, cp);
            if (n == 0)
                break;
            const uint16_t adv = glyph_advance(cp, fpx);
            if (line_w + adv > area.w)
            {
                if (line_end == slice.begin)
                    line_end = p + n;
                break;
            }
            line_w += adv;
            p += n;
            line_end = p;
        }

        slice.end      = line_end;
        slice.width_px = line_w;

        char line_buf[128];
        const size_t ln = static_cast<size_t>(slice.end - slice.begin);
        const size_t cp_len = (ln < sizeof(line_buf) - 1) ? ln : (sizeof(line_buf) - 1);
        if (cp_len > 0)
            std::memcpy(line_buf, slice.begin, cp_len);
        line_buf[cp_len] = '\0';

        (void)canvas.text(area.x, y, line_buf, fpx);

        if (p < lim && *p == '\n')
            ++p;
        y = static_cast<int16_t>(y + lh);
    }
}

} // namespace app::ebook::apps::reader
