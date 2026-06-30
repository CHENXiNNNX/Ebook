#include "gfx/bitmap_font.hpp"

#include <cstdio>
#include <cstring>

#include <esp_heap_caps.h>

#include "common/storage/storage.hpp"
#include "core/log.hpp"

static const char* const TAG = "BitmapFont";

namespace app::ebook::gfx {

namespace {

constexpr char kMagic[4] = {'E', 'B', 'F', '1'};

#pragma pack(push, 1)
struct FileHeader
{
    char     magic[4];
    uint16_t version;
    uint8_t  size_px;
    uint8_t  ascent;
    uint8_t  line_height;
    uint8_t  reserved;
    uint16_t glyph_count;
    uint32_t lut_ofs;
    uint32_t dir_ofs;
    uint32_t bitmap_ofs;
    uint32_t file_size;
};
#pragma pack(pop)

static_assert(sizeof(FileHeader) == 28, "bitmap font header size");

uint8_t* alloc_psram_or_internal(size_t n)
{
    auto* p = static_cast<uint8_t*>(
        heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (p == nullptr)
        p = static_cast<uint8_t*>(heap_caps_malloc(n, MALLOC_CAP_8BIT));
    return p;
}

} // namespace

bool BitmapFontFace::init(const char* path)
{
    if (data_ != nullptr) return true;
    if (path == nullptr || path[0] == '\0') return false;

    FILE* f = std::fopen(path, "rb");
    if (f == nullptr)
    {
        EBOOK_LOGW(TAG, "open failed: %s", path);
        return false;
    }

    if (std::fseek(f, 0, SEEK_END) != 0) { std::fclose(f); return false; }
    const long sz = std::ftell(f);
    if (sz <= static_cast<long>(sizeof(FileHeader)))
    {
        std::fclose(f);
        EBOOK_LOGW(TAG, "file too small: %s", path);
        return false;
    }

    uint8_t* buf = alloc_psram_or_internal(static_cast<size_t>(sz));
    if (buf == nullptr)
    {
        std::fclose(f);
        EBOOK_LOGE(TAG, "alloc %u bytes failed", static_cast<unsigned>(sz));
        return false;
    }

    std::rewind(f);
    const size_t got = std::fread(buf, 1, static_cast<size_t>(sz), f);
    std::fclose(f);
    if (got != static_cast<size_t>(sz))
    {
        heap_caps_free(buf);
        EBOOK_LOGE(TAG, "read failed: %s", path);
        return false;
    }

    const auto* hdr = reinterpret_cast<const FileHeader*>(buf);
    const bool magic_ok    = std::memcmp(hdr->magic, kMagic, 4) == 0;
    const bool version_ok  = hdr->version == 1;
    const bool size_ok     = hdr->file_size == static_cast<uint32_t>(sz);
    const bool size_px_ok  = hdr->size_px != 0;
    if (!magic_ok || !version_ok || !size_ok || !size_px_ok)
    {
        heap_caps_free(buf);
        EBOOK_LOGW(TAG, "header mismatch: %s", path);
        return false;
    }

    data_        = buf;
    data_size_   = static_cast<size_t>(sz);
    size_px_     = hdr->size_px;
    ascent_      = hdr->ascent;
    line_height_ = hdr->line_height;
    glyph_count_ = hdr->glyph_count;
    lut_         = reinterpret_cast<const uint16_t*>(buf + hdr->lut_ofs);
    dir_         = reinterpret_cast<const DirEntry*>(buf + hdr->dir_ofs);
    pool_        = buf + hdr->bitmap_ofs;
    return true;
}

void BitmapFontFace::deinit()
{
    if (data_ != nullptr)
    {
        heap_caps_free(data_);
        data_ = nullptr;
    }
    data_size_   = 0;
    size_px_     = 0;
    ascent_      = 0;
    line_height_ = 0;
    glyph_count_ = 0;
    lut_         = nullptr;
    dir_         = nullptr;
    pool_        = nullptr;
}

bool BitmapFontFace::lookup(uint32_t cp, GlyphMetrics& m, const uint8_t*& bitmap) const
{
    bitmap = nullptr;
    if (data_ == nullptr || cp >= 65536U) return false;

    const uint16_t gid = lut_[cp];
    if (gid == 0 || gid > glyph_count_) return false;

    const DirEntry& e = dir_[gid - 1];
    m.advance   = e.advance;
    m.bearing_x = e.bearing_x;
    m.bearing_y = e.bearing_y;
    m.width     = e.width;
    m.height    = e.height;

    if (e.width != 0 && e.height != 0 && e.pitch != 0)
        bitmap = pool_ + e.bitmap_ofs;
    return true;
}

uint16_t BitmapFontFace::advance(uint32_t cp) const
{
    if (data_ == nullptr || cp >= 65536U) return 0;
    const uint16_t gid = lut_[cp];
    if (gid == 0 || gid > glyph_count_) return 0;
    return dir_[gid - 1].advance;
}

BitmapFontSet& BitmapFontSet::get_instance()
{
    static BitmapFontSet s;
    return s;
}

bool BitmapFontSet::init()
{
    if (loaded_ > 0) return any_ready();

    char path[80];
    for (size_t i = 0; i < kSizeCount && loaded_ < kSizeCount; ++i)
    {
        const uint8_t px = kSizes[i];
        (void)std::snprintf(path, sizeof(path), "%s/fonts/simhei_%u.bin",
                            ::app::common::storage::k_path_assets,
                            static_cast<unsigned>(px));
        if (faces_[loaded_].init(path))
            ++loaded_;
    }

    if (loaded_ == 0)
        EBOOK_LOGW(TAG, "no bitmap fonts loaded (FreeType fallback only)");
    else
        EBOOK_LOGI(TAG, "loaded %u/%u faces from /assets/fonts",
                   static_cast<unsigned>(loaded_),
                   static_cast<unsigned>(kSizeCount));
    return any_ready();
}

void BitmapFontSet::deinit()
{
    for (size_t i = 0; i < kSizeCount; ++i)
        faces_[i].deinit();
    loaded_ = 0;
}

bool BitmapFontSet::any_ready() const
{
    for (size_t i = 0; i < kSizeCount; ++i)
        if (faces_[i].ready()) return true;
    return false;
}

const BitmapFontFace* BitmapFontSet::get(uint8_t size_px) const
{
    for (size_t i = 0; i < kSizeCount; ++i)
        if (faces_[i].ready() && faces_[i].size_px() == size_px)
            return &faces_[i];
    return nullptr;
}

const BitmapFontFace* BitmapFontSet::nearest(uint8_t size_px) const
{
    // 优先「不小于 size_px 的最小」
    const BitmapFontFace* best = nullptr;
    uint8_t best_px = 255;
    for (size_t i = 0; i < kSizeCount; ++i)
    {
        if (!faces_[i].ready()) continue;
        const uint8_t px = faces_[i].size_px();
        if (px >= size_px && px < best_px)
        {
            best_px = px;
            best    = &faces_[i];
        }
    }
    if (best != nullptr) return best;

    // 退化：返回最大已加载
    for (size_t i = 0; i < kSizeCount; ++i)
    {
        if (!faces_[i].ready()) continue;
        if (best == nullptr || faces_[i].size_px() > best->size_px())
            best = &faces_[i];
    }
    return best;
}

} // namespace app::ebook::gfx
