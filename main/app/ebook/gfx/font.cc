#include "gfx/font.hpp"

#include <climits>
#include <cstdio>
#include <cstring>

#include <esp_heap_caps.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include "common/storage/storage.hpp"
#include "core/log.hpp"
#include "gfx/bitmap_font.hpp"
#include "gfx/utf8.hpp"

static const char* const TAG = "Font";

namespace app::ebook::gfx {

namespace {

constexpr const char* kFontFileText = "/fonts/simhei.ttf";
constexpr const char* kFontFileIcon = "/fonts/font_awesome_6.ttf";
constexpr uint8_t     kBitmapScaleMinPx = 24;
constexpr size_t      kProbeMax = 16;

uint8_t face_id(FontFace f) { return (f == FontFace::Icon) ? 1U : 0U; }

uint8_t* psram_alloc(size_t n)
{
    auto* p = static_cast<uint8_t*>(heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (p == nullptr) p = static_cast<uint8_t*>(heap_caps_malloc(n, MALLOC_CAP_8BIT));
    return p;
}

void psram_free(uint8_t* p) { if (p != nullptr) heap_caps_free(p); }

uint16_t scale_metric(uint16_t v, uint8_t from_px, uint8_t to_px)
{
    if (from_px == 0 || to_px == from_px) return v;
    return static_cast<uint16_t>(
        (static_cast<uint32_t>(v) * to_px + from_px / 2U) / from_px);
}

uint8_t pick_scale(uint8_t size_px, uint8_t base_px)
{
    if (base_px == 0 || size_px <= base_px || size_px < kBitmapScaleMinPx) return 1;
    return static_cast<uint8_t>(
        (static_cast<uint16_t>(size_px) + base_px / 2U) / base_px);
}

} // namespace

Font& Font::get_instance()
{
    static Font s;
    return s;
}

bool Font::init()
{
    std::lock_guard<std::mutex> lk(mtx_);
    if (ready_) return true;

    char path_text[64];
    char path_icon[64];
    (void)std::snprintf(path_text, sizeof(path_text), "%s%s",
                        ::app::common::storage::k_path_assets, kFontFileText);
    (void)std::snprintf(path_icon, sizeof(path_icon), "%s%s",
                        ::app::common::storage::k_path_assets, kFontFileIcon);

    if (FT_Init_FreeType(&lib_) != 0)
    {
        EBOOK_LOGE(TAG, "FT_Init_FreeType failed");
        return false;
    }

    const bool bitmap_ok = BitmapFontSet::get_instance().init();

    if (FT_New_Face(lib_, path_text, 0, &face_text_) != 0)
    {
        if (bitmap_ok)
            EBOOK_LOGW(TAG, "text TTF missing: %s (bitmap font only)", path_text);
        else
        {
            EBOOK_LOGE(TAG, "load text font failed: %s", path_text);
            BitmapFontSet::get_instance().deinit();
            FT_Done_FreeType(lib_);
            lib_ = nullptr;
            return false;
        }
    }

    if (FT_New_Face(lib_, path_icon, 0, &face_icon_) != 0)
    {
        EBOOK_LOGW(TAG, "load icon font failed: %s (icons disabled)", path_icon);
        face_icon_ = nullptr;
    }

    size_text_         = 0;
    size_icon_         = 0;
    bitmap_only_text_  = (face_text_ == nullptr && bitmap_ok);
    ready_ = true;
    return true;
}

void Font::deinit()
{
    std::lock_guard<std::mutex> lk(mtx_);
    if (!ready_) return;

    for (Slot& s : cache_)
    {
        if (s.bitmap != nullptr)
        {
            psram_free(s.bitmap);
            s.bitmap = nullptr;
        }
        s.key = 0;
        s.bitmap_bytes = 0;
    }
    if (face_text_ != nullptr) FT_Done_Face(face_text_);
    if (face_icon_ != nullptr) FT_Done_Face(face_icon_);
    if (lib_       != nullptr) FT_Done_FreeType(lib_);
    BitmapFontSet::get_instance().deinit();

    face_text_         = nullptr;
    face_icon_         = nullptr;
    lib_               = nullptr;
    ready_             = false;
    bitmap_only_text_  = false;
    access_seq_        = 0;
    stats_             = {};
}

uint32_t Font::make_key(uint32_t cp, uint8_t size_px, FontFace face)
{
    // key: face(4) | size(7) | cp(20)
    return 0x80000000U |
           (static_cast<uint32_t>(face_id(face)) << 27) |
           (static_cast<uint32_t>(size_px) << 20) |
           (cp & 0x000FFFFFU);
}

size_t Font::hash_slot(uint32_t key)
{
    uint32_t h = key;
    h ^= h >> 16;
    h *= 0x7feb352dU;
    h ^= h >> 15;
    h *= 0x846ca68bU;
    h ^= h >> 16;
    return h & (kCacheSlots - 1);
}

Font::Slot* Font::cache_find_locked(uint32_t key)
{
    const size_t start = hash_slot(key);
    for (size_t i = 0; i < kProbeMax; ++i)
    {
        Slot& s = cache_[(start + i) & (kCacheSlots - 1)];
        if (s.key == key) return &s;
        if (s.key == 0)   return nullptr;
    }
    return nullptr;
}

Font::Slot* Font::cache_replace_locked(uint32_t key)
{
    const size_t start = hash_slot(key);

    for (size_t i = 0; i < kProbeMax; ++i)
    {
        Slot& s = cache_[(start + i) & (kCacheSlots - 1)];
        if (s.key == 0) return &s;
    }
    Slot* victim    = nullptr;
    uint32_t oldest = UINT32_MAX;
    for (size_t i = 0; i < kProbeMax; ++i)
    {
        Slot& s = cache_[(start + i) & (kCacheSlots - 1)];
        if (s.lru < oldest) { oldest = s.lru; victim = &s; }
    }
    if (victim == nullptr) return nullptr;

    if (victim->bitmap != nullptr)
    {
        stats_.bitmap_bytes -= victim->bitmap_bytes;
        psram_free(victim->bitmap);
        victim->bitmap = nullptr;
        victim->bitmap_bytes = 0;
        ++stats_.evictions;
    }
    return victim;
}

FT_Face Font::active_face_locked(FontFace face)
{
    return (face == FontFace::Icon) ? face_icon_ : face_text_;
}

bool Font::select_size_locked(FontFace face, uint8_t size_px)
{
    if (size_px == 0) return false;
    FT_Face f = active_face_locked(face);
    if (f == nullptr) return false;

    uint8_t* cached = (face == FontFace::Icon) ? &size_icon_ : &size_text_;
    if (*cached == size_px) return true;
    if (FT_Set_Pixel_Sizes(f, 0, size_px) != 0) return false;
    *cached = size_px;
    return true;
}

Font::Slot* Font::render_into_locked(uint32_t cp, uint8_t size_px, FontFace face)
{
    if (!select_size_locked(face, size_px)) return nullptr;
    FT_Face f = active_face_locked(face);
    if (f == nullptr) return nullptr;

    if (FT_Load_Char(f, cp, FT_LOAD_RENDER | FT_LOAD_TARGET_MONO) != 0)
        return nullptr;

    const FT_Bitmap& bm = f->glyph->bitmap;
    const uint32_t key = make_key(cp, size_px, face);
    Slot* slot = cache_replace_locked(key);
    if (slot == nullptr) return nullptr;

    slot->key         = key;
    slot->lru         = ++access_seq_;
    slot->m.bearing_x = static_cast<int16_t>(f->glyph->bitmap_left);
    slot->m.bearing_y = static_cast<int16_t>(f->glyph->bitmap_top);
    slot->m.width     = static_cast<uint16_t>(bm.width);
    slot->m.height    = static_cast<uint16_t>(bm.rows);
    slot->m.advance   = static_cast<uint16_t>(f->glyph->advance.x >> 6);

    if (bm.buffer == nullptr || bm.width == 0 || bm.rows == 0)
    {
        slot->bitmap = nullptr;
        slot->bitmap_bytes = 0;
        return slot;
    }

    const uint16_t pitch  = static_cast<uint16_t>((bm.width + 7) / 8);
    const uint32_t nbytes = static_cast<uint32_t>(pitch) * bm.rows;
    uint8_t* buf = psram_alloc(nbytes);
    if (buf == nullptr)
    {
        slot->bitmap = nullptr;
        slot->bitmap_bytes = 0;
        return slot;
    }

    if (bm.pixel_mode == FT_PIXEL_MODE_MONO)
    {
        for (unsigned r = 0; r < bm.rows; ++r)
            std::memcpy(buf + r * pitch, bm.buffer + r * bm.pitch, pitch);
    }
    else
    {
        // 灰度兜底（MONO 目标下不应触发）
        std::memset(buf, 0, nbytes);
        for (unsigned r = 0; r < bm.rows; ++r)
            for (unsigned c = 0; c < bm.width; ++c)
                if (bm.buffer[r * static_cast<unsigned>(bm.pitch) + c] >= 128)
                    buf[r * pitch + (c / 8)] |= static_cast<uint8_t>(0x80U >> (c & 7U));
    }

    slot->bitmap       = buf;
    slot->bitmap_bytes = static_cast<uint16_t>(nbytes);
    stats_.bitmap_bytes += slot->bitmap_bytes;
    return slot;
}

void Font::blit_locked(uint8_t* fb, int16_t pen_x, int16_t baseline_y,
                       const GlyphMetrics& m, const uint8_t* bitmap,
                       Ink ink, const core::Rect& clip)
{
    if (fb == nullptr || bitmap == nullptr || m.width == 0 || m.height == 0)
        return;

    const int16_t gx0 = static_cast<int16_t>(pen_x + m.bearing_x);
    const int16_t gy0 = static_cast<int16_t>(baseline_y - m.bearing_y);
    const int16_t gx1 = static_cast<int16_t>(gx0 + m.width);
    const int16_t gy1 = static_cast<int16_t>(gy0 + m.height);

    const int16_t x0 = (gx0 > clip.x)        ? gx0 : clip.x;
    const int16_t y0 = (gy0 > clip.y)        ? gy0 : clip.y;
    const int16_t x1 = (gx1 < clip.right())  ? gx1 : clip.right();
    const int16_t y1 = (gy1 < clip.bottom()) ? gy1 : clip.bottom();
    if (x1 <= x0 || y1 <= y0) return;

    const uint16_t src_pitch = static_cast<uint16_t>((m.width + 7) / 8);
    const bool black = (ink == Ink::Black);

    for (int16_t py = y0; py < y1; ++py)
    {
        const int gly_row = py - gy0;
        const uint8_t* sp = bitmap + gly_row * src_pitch;
        const uint32_t row_off = static_cast<uint32_t>(py) * core::kStride;

        for (int16_t px = x0; px < x1; ++px)
        {
            const int gly_col = px - gx0;
            const uint8_t bit = sp[gly_col >> 3] & static_cast<uint8_t>(0x80U >> (gly_col & 7));
            if (bit == 0) continue;

            const uint32_t fb_idx  = row_off + (static_cast<uint16_t>(px) >> 3);
            const uint8_t  fb_mask = static_cast<uint8_t>(0x80U >> (static_cast<uint16_t>(px) & 7));
            if (black) fb[fb_idx] &= static_cast<uint8_t>(~fb_mask);
            else       fb[fb_idx] |= fb_mask;
        }
    }
}

void Font::blit_bitmap_glyph_locked(uint8_t* fb, int16_t pen_x, int16_t baseline_y,
                                    const GlyphMetrics& m, const uint8_t* bitmap,
                                    uint8_t size_px, Ink ink, const core::Rect& clip)
{
    // bitmap_only_text_ 且 ≥24px 时点阵整数倍放大
    const BitmapFontFace* near_bf = BitmapFontSet::get_instance().nearest(size_px);
    const uint8_t base_px = (near_bf != nullptr) ? near_bf->size_px() : size_px;
    const uint8_t scale = pick_scale(size_px, base_px);
    if (scale <= 1 || bitmap == nullptr || m.width == 0 || m.height == 0)
    {
        blit_locked(fb, pen_x, baseline_y, m, bitmap, ink, clip);
        return;
    }

    const int16_t bx  = static_cast<int16_t>(m.bearing_x * scale);
    const int16_t by  = static_cast<int16_t>(m.bearing_y * scale);
    const int16_t gw  = static_cast<int16_t>(m.width * scale);
    const int16_t gh  = static_cast<int16_t>(m.height * scale);
    const int16_t gx0 = static_cast<int16_t>(pen_x + bx);
    const int16_t gy0 = static_cast<int16_t>(baseline_y - by);
    const int16_t gx1 = static_cast<int16_t>(gx0 + gw);
    const int16_t gy1 = static_cast<int16_t>(gy0 + gh);

    const int16_t x0 = (gx0 > clip.x)        ? gx0 : clip.x;
    const int16_t y0 = (gy0 > clip.y)        ? gy0 : clip.y;
    const int16_t x1 = (gx1 < clip.right())  ? gx1 : clip.right();
    const int16_t y1 = (gy1 < clip.bottom()) ? gy1 : clip.bottom();
    if (x1 <= x0 || y1 <= y0) return;

    const uint16_t src_pitch = static_cast<uint16_t>((m.width + 7) / 8);
    const bool black = (ink == Ink::Black);

    for (uint16_t row = 0; row < m.height; ++row)
    {
        const uint8_t* sp = bitmap + row * src_pitch;
        const int16_t dy0 = static_cast<int16_t>(gy0 + row * scale);
        const int16_t dy1 = static_cast<int16_t>(dy0 + scale);
        const int16_t py0 = (dy0 > y0) ? dy0 : y0;
        const int16_t py1 = (dy1 < y1) ? dy1 : y1;
        if (py1 <= py0) continue;

        for (uint16_t col = 0; col < m.width; ++col)
        {
            const uint8_t bit = sp[col >> 3] & static_cast<uint8_t>(0x80U >> (col & 7));
            if (bit == 0) continue;

            const int16_t dx0 = static_cast<int16_t>(gx0 + col * scale);
            const int16_t dx1 = static_cast<int16_t>(dx0 + scale);
            const int16_t px0 = (dx0 > x0) ? dx0 : x0;
            const int16_t px1 = (dx1 < x1) ? dx1 : x1;
            if (px1 <= px0) continue;

            for (int16_t py = py0; py < py1; ++py)
            {
                const uint32_t row_off = static_cast<uint32_t>(py) * core::kStride;
                for (int16_t px = px0; px < px1; ++px)
                {
                    const uint32_t fb_idx  = row_off + (static_cast<uint16_t>(px) >> 3);
                    const uint8_t  fb_mask = static_cast<uint8_t>(0x80U >> (static_cast<uint16_t>(px) & 7));
                    if (black) fb[fb_idx] &= static_cast<uint8_t>(~fb_mask);
                    else       fb[fb_idx] |= fb_mask;
                }
            }
        }
    }
}

uint16_t Font::draw_glyph(uint8_t* fb, int16_t x, int16_t y_top, uint32_t cp,
                          uint8_t size_px, FontFace face, Ink ink, const core::Rect& clip)
{
    std::lock_guard<std::mutex> lk(mtx_);
    if (!ready_ || fb == nullptr) return 0;

    auto& bset = BitmapFontSet::get_instance();

    // 1) 预烘焙点阵（text 12/14/16）
    if (face == FontFace::Text)
    {
        if (const BitmapFontFace* bf = bset.get(size_px))
        {
            GlyphMetrics m{};
            const uint8_t* bm = nullptr;
            if (bf->lookup(cp, m, bm))
            {
                const int16_t baseline = static_cast<int16_t>(y_top + bf->ascent());
                blit_locked(fb, x, baseline, m, bm, ink, clip);
                return m.advance;
            }
        }
    }

    // 2) FT cache fallback
    const uint32_t key = make_key(cp, size_px, face);
    Slot* g = cache_find_locked(key);
    if (g != nullptr) { ++stats_.hits; g->lru = ++access_seq_; }
    else
    {
        ++stats_.misses;
        g = render_into_locked(cp, size_px, face);
        if (g == nullptr) return 0;
    }

    if (!select_size_locked(face, size_px)) return g->m.advance;
    FT_Face f = active_face_locked(face);
    if (f == nullptr) return g->m.advance;
    const int16_t baseline = static_cast<int16_t>(y_top + (f->size->metrics.ascender >> 6));
    blit_locked(fb, x, baseline, g->m, g->bitmap, ink, clip);
    return g->m.advance;
}

uint16_t Font::draw_text(uint8_t* fb, int16_t x, int16_t y_top, const char* utf8,
                         uint8_t size_px, FontFace face, Ink ink, const core::Rect& clip)
{
    if (fb == nullptr || utf8 == nullptr) return 0;
    std::lock_guard<std::mutex> lk(mtx_);
    if (!ready_) return 0;

    auto& bset = BitmapFontSet::get_instance();
    const BitmapFontFace* exact_bf =
        (face == FontFace::Text) ? bset.get(size_px) : nullptr;
    const BitmapFontFace* near_bf =
        (face == FontFace::Text && exact_bf == nullptr && bitmap_only_text_)
            ? bset.nearest(size_px) : nullptr;

    FT_Face f = active_face_locked(face);
    if (exact_bf == nullptr && f != nullptr)
        (void)select_size_locked(face, size_px);

    // 基线：点阵 / 邻近点阵 / FreeType
    int16_t baseline;
    if (exact_bf != nullptr)
        baseline = static_cast<int16_t>(y_top + exact_bf->ascent());
    else if (near_bf != nullptr)
        baseline = static_cast<int16_t>(
            y_top + scale_metric(near_bf->ascent(), near_bf->size_px(), size_px));
    else if (f != nullptr)
        baseline = static_cast<int16_t>(y_top + (f->size->metrics.ascender >> 6));
    else
        return 0;

    const int16_t clip_right = clip.right();
    int32_t pen_x = x;
    const char* p = utf8;

    while (*p != '\0')
    {
        uint32_t cp = 0;
        const uint8_t n = utf8_decode(p, cp);
        if (n == 0) break;
        p += n;

        GlyphMetrics m{};
        const uint8_t* bm = nullptr;

        // 1) 精确点阵
        if (exact_bf != nullptr && exact_bf->lookup(cp, m, bm))
        {
            blit_locked(fb, static_cast<int16_t>(pen_x), baseline, m, bm, ink, clip);
            pen_x += m.advance;
        }
        // 2) 邻近点阵（bitmap_only_text_）
        else if (near_bf != nullptr && near_bf->lookup(cp, m, bm))
        {
            if (size_px >= kBitmapScaleMinPx)
                blit_bitmap_glyph_locked(fb, static_cast<int16_t>(pen_x), baseline,
                                         m, bm, size_px, ink, clip);
            else
                blit_locked(fb, static_cast<int16_t>(pen_x), baseline, m, bm, ink, clip);

            const uint8_t base_px = near_bf->size_px();
            pen_x += (base_px == size_px)
                         ? m.advance
                         : scale_metric(m.advance, base_px, size_px);
        }
        // 3) FT cache
        else if (f != nullptr)
        {
            const uint32_t key = make_key(cp, size_px, face);
            Slot* g = cache_find_locked(key);
            if (g != nullptr) { ++stats_.hits; g->lru = ++access_seq_; }
            else
            {
                ++stats_.misses;
                g = render_into_locked(cp, size_px, face);
                if (g == nullptr) continue;
            }
            blit_locked(fb, static_cast<int16_t>(pen_x), baseline, g->m, g->bitmap, ink, clip);
            pen_x += g->m.advance;
        }

        if (pen_x >= clip_right + 64) break;
    }

    return static_cast<uint16_t>(pen_x - x);
}

uint16_t Font::advance(uint32_t cp, uint8_t size_px, FontFace face)
{
    auto& bset = BitmapFontSet::get_instance();

    if (face == FontFace::Text)
    {
        if (const BitmapFontFace* exact = bset.get(size_px))
        {
            const uint16_t adv = exact->advance(cp);
            if (adv > 0) return adv;
        }
        if (bitmap_only_text_)
        {
            const BitmapFontFace* near_bf = bset.nearest(size_px);
            if (near_bf != nullptr)
            {
                const uint16_t adv = near_bf->advance(cp);
                if (adv > 0)
                    return scale_metric(adv, near_bf->size_px(), size_px);
            }
        }
    }

    std::lock_guard<std::mutex> lk(mtx_);
    if (!ready_) return 0;

    const uint32_t key = make_key(cp, size_px, face);
    Slot* g = cache_find_locked(key);
    if (g != nullptr)
    {
        ++stats_.hits;
        g->lru = ++access_seq_;
        return g->m.advance;
    }

    ++stats_.misses;
    g = render_into_locked(cp, size_px, face);
    return (g != nullptr) ? g->m.advance : 0;
}

uint16_t Font::measure(const char* utf8, uint8_t size_px, FontFace face)
{
    if (utf8 == nullptr) return 0;

    auto& bset = BitmapFontSet::get_instance();
    const BitmapFontFace* exact_bf =
        (face == FontFace::Text) ? bset.get(size_px) : nullptr;
    const BitmapFontFace* near_bf =
        (face == FontFace::Text && exact_bf == nullptr && bitmap_only_text_)
            ? bset.nearest(size_px) : nullptr;

    std::lock_guard<std::mutex> lk(mtx_);
    if (!ready_) return 0;

    uint32_t total = 0;
    const char* p = utf8;
    while (*p != '\0')
    {
        uint32_t cp = 0;
        const uint8_t n = utf8_decode(p, cp);
        if (n == 0) break;
        p += n;

        if (exact_bf != nullptr)
        {
            const uint16_t adv = exact_bf->advance(cp);
            if (adv > 0) { total += adv; continue; }
        }
        else if (near_bf != nullptr)
        {
            const uint16_t adv = near_bf->advance(cp);
            if (adv > 0) { total += scale_metric(adv, near_bf->size_px(), size_px); continue; }
        }

        const uint32_t key = make_key(cp, size_px, face);
        Slot* g = cache_find_locked(key);
        if (g != nullptr)
        {
            ++stats_.hits;
            g->lru = ++access_seq_;
            total += g->m.advance;
        }
        else
        {
            ++stats_.misses;
            g = render_into_locked(cp, size_px, face);
            if (g != nullptr) total += g->m.advance;
        }
    }
    return (total > UINT16_MAX) ? UINT16_MAX : static_cast<uint16_t>(total);
}

uint16_t Font::line_height(uint8_t size_px, FontFace face)
{
    auto& bset = BitmapFontSet::get_instance();
    if (face == FontFace::Text)
    {
        if (const BitmapFontFace* exact = bset.get(size_px))
            return exact->line_height();
        if (bitmap_only_text_)
        {
            const BitmapFontFace* near_bf = bset.nearest(size_px);
            if (near_bf != nullptr)
                return scale_metric(near_bf->line_height(), near_bf->size_px(), size_px);
        }
    }

    std::lock_guard<std::mutex> lk(mtx_);
    if (!ready_ || !select_size_locked(face, size_px)) return size_px;
    FT_Face f = active_face_locked(face);
    if (f == nullptr) return size_px;
    return static_cast<uint16_t>(f->size->metrics.height >> 6);
}

uint16_t Font::ascent(uint8_t size_px, FontFace face)
{
    auto& bset = BitmapFontSet::get_instance();
    if (face == FontFace::Text)
    {
        if (const BitmapFontFace* exact = bset.get(size_px))
            return exact->ascent();
        if (bitmap_only_text_)
        {
            const BitmapFontFace* near_bf = bset.nearest(size_px);
            if (near_bf != nullptr)
                return scale_metric(near_bf->ascent(), near_bf->size_px(), size_px);
        }
    }

    std::lock_guard<std::mutex> lk(mtx_);
    if (!ready_ || !select_size_locked(face, size_px)) return size_px;
    FT_Face f = active_face_locked(face);
    if (f == nullptr) return size_px;
    return static_cast<uint16_t>(f->size->metrics.ascender >> 6);
}

} // namespace app::ebook::gfx
